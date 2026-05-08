// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// ─────────────────────────────────────────────────────────────────────────────
//  IR Gun modifications:
//    • Added /pos  endpoint  → returns {"x":0.xxxx,"y":0.xxxx,"tracking":true}
//    • Added /debug endpoint → returns all blob data as JSON for diagnostics
//    • max_uri_handlers bumped to 16 (was already 16, kept)
//    • Streaming endpoint kept intact for visual verification during setup
// ─────────────────────────────────────────────────────────────────────────────

#include "Arduino.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"
#include "board_config.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// ── Shared tracking state (defined in CameraWebServer.ino) ───────────────────
extern volatile float g_normX;
extern volatile float g_normY;
extern volatile int   g_blobCount;
extern volatile bool  g_tracking;
extern volatile bool  g_triggerPressed;

#define G_MAX_BLOBS 16
extern volatile float g_blobCx[G_MAX_BLOBS];
extern volatile float g_blobCy[G_MAX_BLOBS];
extern volatile int   g_blobArea[G_MAX_BLOBS];
extern volatile float g_cornerTLx, g_cornerTLy;
extern volatile float g_cornerTRx, g_cornerTRy;
extern volatile float g_cornerBLx, g_cornerBLy;
extern volatile float g_cornerBRx, g_cornerBRy;
extern volatile int   g_frameW;
extern volatile int   g_frameH;

// ── LED flash ─────────────────────────────────────────────────────────────────
#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255
int led_duty = 0;
bool isStreaming = false;
#endif

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

typedef struct {
  size_t size;
  size_t index;
  size_t count;
  int sum;
  int *values;
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
  memset(filter, 0, sizeof(ra_filter_t));
  filter->values = (int *)malloc(sample_size * sizeof(int));
  if (!filter->values) return NULL;
  memset(filter->values, 0, sample_size * sizeof(int));
  filter->size = sample_size;
  return filter;
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static int ra_filter_run(ra_filter_t *filter, int value) {
  if (!filter->values) return value;
  filter->sum -= filter->values[filter->index];
  filter->values[filter->index] = value;
  filter->sum += filter->values[filter->index];
  filter->index++;
  filter->index = filter->index % filter->size;
  if (filter->count < filter->size) filter->count++;
  return filter->sum / filter->count;
}
#endif

#if defined(LED_GPIO_NUM)
void enable_led(bool en) {
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) {
    duty = CONFIG_LED_MAX_INTENSITY;
  }
  ledcWrite(LED_GPIO_NUM, duty);
  log_i("Set LED intensity to %d", duty);
}
#endif

// ── /pos handler ─────────────────────────────────────────────────────────────
//
//  Returns a small JSON object with the current normalised aim position.
//  Poll this from Unity (or a browser) to get real-time coordinates.
//
//  Response: {"x":0.5123,"y":0.4871,"tracking":true,"blobs":8}
//
//  x: 0.0 = left edge of monitor,  1.0 = right edge
//  y: 0.0 = top  edge of monitor,  1.0 = bottom edge
//  tracking: false if <4 IR blobs visible (gun pointed off-screen or occluded)
//
static esp_err_t pos_handler(httpd_req_t *req) {
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"x\":%.4f,\"y\":%.4f,\"tracking\":%s,\"blobs\":%d,\"fire\":%s}",
    (float)g_normX,
    (float)g_normY,
    g_tracking       ? "true" : "false",
    (int)g_blobCount,
    g_triggerPressed ? "true" : "false"
  );
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, buf);
}

// ── /trackdata handler ───────────────────────────────────────────────────────
//
//  Lightweight JSON endpoint polled by the /track dashboard page.
//  Returns aim coords, corner positions, and all blob centroids in one shot.
//
static esp_err_t trackdata_handler(httpd_req_t *req) {
  static char json[1024];
  char *p   = json;
  char *end = json + sizeof(json);

  p += snprintf(p, end - p,
    "{\"tracking\":%s,\"blobs\":%d,\"w\":%d,\"h\":%d,"
    "\"x\":%.4f,\"y\":%.4f,\"fire\":%s,"
    "\"corners\":{"
      "\"tl\":[%.1f,%.1f],\"tr\":[%.1f,%.1f],"
      "\"bl\":[%.1f,%.1f],\"br\":[%.1f,%.1f]},"
    "\"blobList\":[",
    g_tracking ? "true" : "false",
    (int)g_blobCount,
    (int)g_frameW, (int)g_frameH,
    (float)g_normX, (float)g_normY,
    g_triggerPressed ? "true" : "false",
    (float)g_cornerTLx, (float)g_cornerTLy,
    (float)g_cornerTRx, (float)g_cornerTRy,
    (float)g_cornerBLx, (float)g_cornerBLy,
    (float)g_cornerBRx, (float)g_cornerBRy
  );

  int n = (int)g_blobCount < G_MAX_BLOBS ? (int)g_blobCount : G_MAX_BLOBS;
  for (int i = 0; i < n && p < end - 40; i++) {
    if (i > 0) *p++ = ',';
    p += snprintf(p, end - p, "[%.1f,%.1f,%d]",
                  (float)g_blobCx[i], (float)g_blobCy[i], (int)g_blobArea[i]);
  }
  p += snprintf(p, end - p, "]}");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, json);
}

// ── /track handler ───────────────────────────────────────────────────────────
//
//  Serves a self-contained HTML dashboard that polls /trackdata at ~20 Hz
//  and displays:
//    • Live normalised aim coordinates
//    • Tracking status indicator
//    • Blob count and per-blob table (cx, cy, area)
//    • Canvas visualiser showing the camera FOV with blob dots,
//      corner quad outline, and crosshair aim point
//
static const char TRACK_HTML[] =
"<!DOCTYPE html><html lang='en'><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>IR Gun — Live Tracker</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#0f0f0f;color:#e8e6e0;font-family:'Courier New',monospace;font-size:13px;padding:24px}"
"h1{font-size:18px;font-weight:normal;color:#e85d24;margin-bottom:20px;letter-spacing:.05em}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;max-width:900px}"
"@media(max-width:600px){.grid{grid-template-columns:1fr}}"
".card{background:#161616;border:1px solid rgba(255,255,255,.08);border-radius:4px;padding:16px}"
".card h2{font-size:10px;letter-spacing:.12em;text-transform:uppercase;color:#5a5856;margin-bottom:12px}"
".status{display:inline-block;padding:3px 10px;border-radius:2px;font-size:11px;letter-spacing:.08em;text-transform:uppercase;margin-bottom:12px}"
".ok{background:rgba(29,158,117,.15);color:#1d9e75;border:1px solid rgba(29,158,117,.3)}"
".lost{background:rgba(232,93,36,.15);color:#e85d24;border:1px solid rgba(232,93,36,.3)}"
".coord{font-size:28px;color:#f2a623;margin:4px 0}"
".label{font-size:10px;color:#5a5856;text-transform:uppercase;letter-spacing:.1em}"
"table{width:100%;border-collapse:collapse;margin-top:8px}"
"th{font-size:10px;letter-spacing:.1em;text-transform:uppercase;color:#5a5856;text-align:left;padding:4px 6px;border-bottom:1px solid rgba(255,255,255,.08)}"
"td{padding:4px 6px;border-bottom:1px solid rgba(255,255,255,.04);color:#9a9890}"
"td:first-child{color:#e8e6e0}"
"canvas{width:100%;background:#0a0a0a;border-radius:3px;display:block}"
".wide{grid-column:1/-1}"
"</style></head><body>"
"<h1>IR GUN — LIVE TRACKER</h1>"
"<div class='grid'>"
"  <div class='card'>"
"    <h2>Tracking Status</h2>"
"    <div id='statusBadge' class='status lost'>LOST</div>"
"    <div class='label'>Aim Position (normalised)</div>"
"    <div class='coord' id='aimX'>X: —</div>"
"    <div class='coord' id='aimY'>Y: —</div>"
"    <div style='margin-top:12px'>"
"      <span class='label'>Blobs detected: </span>"
"      <span id='blobCount' style='color:#f2a623'>—</span>"
"    </div>"
"    <div style='margin-top:12px'>"
"      <span class='label'>Trigger: </span>"
"      <span id='fireState' class='status lost'>OPEN</span>"
"    </div>"
"  </div>"
"  <div class='card'>"
"    <h2>Corner Positions (px)</h2>"
"    <table>"
"      <tr><th>Corner</th><th>X</th><th>Y</th></tr>"
"      <tr><td>TL</td><td id='tlx'>—</td><td id='tly'>—</td></tr>"
"      <tr><td>TR</td><td id='trx'>—</td><td id='try'>—</td></tr>"
"      <tr><td>BL</td><td id='blx'>—</td><td id='bly'>—</td></tr>"
"      <tr><td>BR</td><td id='brx'>—</td><td id='bry'>—</td></tr>"
"    </table>"
"  </div>"
"  <div class='card wide'>"
"    <h2>Camera View — Blob Visualiser</h2>"
"    <canvas id='vis' height='240'></canvas>"
"  </div>"
"  <div class='card wide'>"
"    <h2>All Blobs</h2>"
"    <table><tr><th>#</th><th>CX (px)</th><th>CY (px)</th><th>Area</th></tr>"
"    <tbody id='blobTable'></tbody></table>"
"  </div>"
"</div>"
"<script>"
"const vis=document.getElementById('vis');"
"const ctx=vis.getContext('2d');"
"let fw=320,fh=240;"
"function draw(d){"
"  vis.width=fw; vis.height=fh;"
"  ctx.fillStyle='#0a0a0a'; ctx.fillRect(0,0,fw,fh);"
// Corner quad
"  if(d.tracking&&d.corners){"
"    const c=d.corners;"
"    ctx.strokeStyle='rgba(59,139,212,0.6)'; ctx.lineWidth=1.5;"
"    ctx.beginPath();"
"    ctx.moveTo(c.tl[0],c.tl[1]); ctx.lineTo(c.tr[0],c.tr[1]);"
"    ctx.lineTo(c.br[0],c.br[1]); ctx.lineTo(c.bl[0],c.bl[1]);"
"    ctx.closePath(); ctx.stroke();"
// Corner dots
"    [[c.tl,'TL'],[c.tr,'TR'],[c.bl,'BL'],[c.br,'BR']].forEach(([p,l])=>{"
"      ctx.fillStyle='#3b8bd4'; ctx.beginPath(); ctx.arc(p[0],p[1],5,0,Math.PI*2); ctx.fill();"
"      ctx.fillStyle='#9a9890'; ctx.font='9px monospace'; ctx.fillText(l,p[0]+6,p[1]-4);"
"    });"
"  }"
// All blobs
"  (d.blobList||[]).forEach((b,i)=>{"
"    ctx.strokeStyle='rgba(242,166,35,0.8)'; ctx.lineWidth=1;"
"    ctx.beginPath(); ctx.arc(b[0],b[1],Math.sqrt(b[2])*0.8,0,Math.PI*2); ctx.stroke();"
"    ctx.fillStyle='rgba(242,166,35,0.3)'; ctx.fill();"
"  });"
// Aim crosshair
"  if(d.tracking){"
"    const ax=d.x*fw, ay=d.y*fh;"
"    ctx.strokeStyle='#e85d24'; ctx.lineWidth=1.5;"
"    ctx.beginPath(); ctx.moveTo(ax-12,ay); ctx.lineTo(ax+12,ay); ctx.stroke();"
"    ctx.beginPath(); ctx.moveTo(ax,ay-12); ctx.lineTo(ax,ay+12); ctx.stroke();"
"    ctx.strokeStyle='rgba(232,93,36,0.4)'; ctx.lineWidth=1;"
"    ctx.beginPath(); ctx.arc(ax,ay,10,0,Math.PI*2); ctx.stroke();"
"  }"
"}"
"function set(id,v){document.getElementById(id).textContent=v;}"
"async function poll(){"
"  try{"
"    const r=await fetch('/trackdata'); const d=await r.json();"
"    fw=d.w||320; fh=d.h||240;"
"    const ok=d.tracking;"
"    const badge=document.getElementById('statusBadge');"
"    badge.textContent=ok?'TRACKING':'LOST';"
"    badge.className='status '+(ok?'ok':'lost');"
"    set('aimX','X: '+(ok?d.x.toFixed(4):'—'));"
"    set('aimY','Y: '+(ok?d.y.toFixed(4):'—'));"
"    set('blobCount',d.blobs);"
"    const fs=document.getElementById('fireState');"
"    fs.textContent=d.fire?'FIRED':'OPEN';"
"    fs.className='status '+(d.fire?'ok':'lost');"
"    if(ok&&d.corners){"
"      const c=d.corners;"
"      set('tlx',c.tl[0].toFixed(1)); set('tly',c.tl[1].toFixed(1));"
"      set('trx',c.tr[0].toFixed(1)); set('try',c.tr[1].toFixed(1));"
"      set('blx',c.bl[0].toFixed(1)); set('bly',c.bl[1].toFixed(1));"
"      set('brx',c.br[0].toFixed(1)); set('bry',c.br[1].toFixed(1));"
"    }"
"    const tb=document.getElementById('blobTable');"
"    tb.innerHTML=(d.blobList||[]).map((b,i)=>"
"      `<tr><td>${i}</td><td>${b[0].toFixed(1)}</td><td>${b[1].toFixed(1)}</td><td>${b[2]}</td></tr>`"
"    ).join('');"
"    draw(d);"
"  } catch(e){}"
"  setTimeout(poll,50);"
"}"
"poll();"
"</script></body></html>";

static esp_err_t track_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, TRACK_HTML, strlen(TRACK_HTML));
}

// ── /debug handler ───────────────────────────────────────────────────────────
//
//  Captures a fresh frame, runs the blob detector, and returns all blob
//  centroids + areas as JSON. Use this during initial setup to verify
//  your IR threshold and that all 8 LEDs are being detected cleanly.
//
//  Response: {"w":320,"h":240,"threshold":210,"blobs":[{"cx":12.3,"cy":45.6,"area":18}, ...]}
//
#include "ir_tracker.h"
static esp_err_t debug_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  BlobResult blobs = findBlobs(fb);
  int W = (int)fb->width;
  int H = (int)fb->height;
  esp_camera_fb_return(fb);

  // Build JSON — static buffer, safe for up to MAX_BLOBS=16 blobs
  static char json[1024];
  char *p   = json;
  char *end = json + sizeof(json);

  p += snprintf(p, end - p,
    "{\"w\":%d,\"h\":%d,\"threshold\":%d,\"raw\":%d,\"blobs\":[",
    W, H, IR_THRESHOLD, blobs.rawCount);

  for (int i = 0; i < blobs.count && p < end - 60; i++) {
    if (i > 0) *p++ = ',';
    p += snprintf(p, end - p, "{\"cx\":%.1f,\"cy\":%.1f,\"area\":%d}",
                  blobs.blobs[i].cx, blobs.blobs[i].cy, blobs.blobs[i].area);
  }
  p += snprintf(p, end - p, "]}");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, json);
}

// ── Existing handlers (unchanged) ────────────────────────────────────────────

static esp_err_t bmp_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_start = esp_timer_get_time();
#endif
  fb = esp_camera_fb_get();
  if (!fb) { log_e("Camera capture failed"); httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/x-windows-bmp");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char ts[32];
  snprintf(ts, 32, "%" PRIu32 ".%06" PRIu32, (uint32_t)fb->timestamp.tv_sec, (uint32_t)fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);
  uint8_t *buf = NULL; size_t buf_len = 0;
  bool converted = frame2bmp(fb, &buf, &buf_len);
  esp_camera_fb_return(fb);
  if (!converted) { log_e("BMP Conversion failed"); httpd_resp_send_500(req); return ESP_FAIL; }
  res = httpd_resp_send(req, (const char *)buf, buf_len);
  free(buf);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_end = esp_timer_get_time();
  log_i("BMP: %" PRId32 "ms, %" PRIu32 "B", (int32_t)((fr_end - fr_start) / 1000), (uint32_t)buf_len);
#endif
  return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) j->len = 0;
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) return 0;
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_start = esp_timer_get_time();
#endif
#if defined(LED_GPIO_NUM)
  enable_led(true);
  vTaskDelay(150 / portTICK_PERIOD_MS);
  fb = esp_camera_fb_get();
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif
  if (!fb) { log_e("Camera capture failed"); httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char ts[32];
  snprintf(ts, 32, "%" PRIu32 ".%06" PRIu32, (uint32_t)fb->timestamp.tv_sec, (uint32_t)fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  size_t fb_len = 0;
#endif
  if (fb->format == PIXFORMAT_JPEG) {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = fb->len;
#endif
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  } else {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = jchunk.len;
#endif
  }
  esp_camera_fb_return(fb);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_end = esp_timer_get_time();
  log_i("JPG: %" PRIu32 "B %" PRId32 "ms", (uint32_t)fb_len, (int32_t)((fr_end - fr_start) / 1000));
#endif
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];
  static int64_t last_frame = 0;
  if (!last_frame) last_frame = esp_timer_get_time();

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

#if defined(LED_GPIO_NUM)
  isStreaming = true; enable_led(true);
#endif

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      log_e("Camera capture failed"); res = ESP_FAIL;
    } else {
      _timestamp.tv_sec  = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      if (fb->format != PIXFORMAT_JPEG) {
        bool ok = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb); fb = NULL;
        if (!ok) { log_e("JPEG compression failed"); res = ESP_FAIL; }
      } else {
        _jpg_buf_len = fb->len; _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    if (fb) { esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; }
    else if (_jpg_buf) { free(_jpg_buf); _jpg_buf = NULL; }
    if (res != ESP_OK) { log_e("Send frame failed"); break; }
    int64_t fr_end   = esp_timer_get_time();
    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
    log_i("MJPG: %" PRIu32 "B %" PRId32 "ms (%.1ffps), AVG: %" PRIu32 "ms (%.1ffps)",
          (uint32_t)_jpg_buf_len, (int32_t)frame_time, 1000.0 / frame_time,
          avg_frame_time, 1000.0 / avg_frame_time);
#endif
  }
#if defined(LED_GPIO_NUM)
  isStreaming = false; enable_led(false);
#endif
  return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
  char *buf = NULL;
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) { *obuf = buf; return ESP_OK; }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf = NULL;
  char variable[32]; char value[32];
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
      httpd_query_key_value(buf, "val", value,    sizeof(value))    != ESP_OK) {
    free(buf); httpd_resp_send_404(req); return ESP_FAIL;
  }
  free(buf);
  int val = atoi(value);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;
  if      (!strcmp(variable, "framesize"))      { if (s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val); }
  else if (!strcmp(variable, "quality"))        res = s->set_quality(s, val);
  else if (!strcmp(variable, "contrast"))       res = s->set_contrast(s, val);
  else if (!strcmp(variable, "brightness"))     res = s->set_brightness(s, val);
  else if (!strcmp(variable, "saturation"))     res = s->set_saturation(s, val);
  else if (!strcmp(variable, "gainceiling"))    res = s->set_gainceiling(s, (gainceiling_t)val);
  else if (!strcmp(variable, "colorbar"))       res = s->set_colorbar(s, val);
  else if (!strcmp(variable, "awb"))            res = s->set_whitebal(s, val);
  else if (!strcmp(variable, "agc"))            res = s->set_gain_ctrl(s, val);
  else if (!strcmp(variable, "aec"))            res = s->set_exposure_ctrl(s, val);
  else if (!strcmp(variable, "hmirror"))        res = s->set_hmirror(s, val);
  else if (!strcmp(variable, "vflip"))          res = s->set_vflip(s, val);
  else if (!strcmp(variable, "awb_gain"))       res = s->set_awb_gain(s, val);
  else if (!strcmp(variable, "agc_gain"))       res = s->set_agc_gain(s, val);
  else if (!strcmp(variable, "aec_value"))      res = s->set_aec_value(s, val);
  else if (!strcmp(variable, "aec2"))           res = s->set_aec2(s, val);
  else if (!strcmp(variable, "dcw"))            res = s->set_dcw(s, val);
  else if (!strcmp(variable, "bpc"))            res = s->set_bpc(s, val);
  else if (!strcmp(variable, "wpc"))            res = s->set_wpc(s, val);
  else if (!strcmp(variable, "raw_gma"))        res = s->set_raw_gma(s, val);
  else if (!strcmp(variable, "lenc"))           res = s->set_lenc(s, val);
  else if (!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
  else if (!strcmp(variable, "wb_mode"))        res = s->set_wb_mode(s, val);
  else if (!strcmp(variable, "ae_level"))       res = s->set_ae_level(s, val);
#if defined(LED_GPIO_NUM)
  else if (!strcmp(variable, "led_intensity"))  { led_duty = val; if (isStreaming) enable_led(true); }
#endif
  else { log_i("Unknown command: %s", variable); res = -1; }
  if (res < 0) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static int print_reg(char *p, char *end, sensor_t *s, uint16_t reg, uint32_t mask) {
  return snprintf(p, end - p, "\"0x%04x\":%d,", reg, s->get_reg(s, reg, mask));
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];
  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response, *end = json_response + sizeof(json_response);
  *p++ = '{';
  if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
    for (int reg = 0x3400; reg < 0x3406; reg += 2) p += print_reg(p, end, s, reg, 0xFFF);
    p += print_reg(p, end, s, 0x3406, 0xFF);
    p += print_reg(p, end, s, 0x3500, 0xFFFF0);
    p += print_reg(p, end, s, 0x3503, 0xFF);
    p += print_reg(p, end, s, 0x350a, 0x3FF);
    p += print_reg(p, end, s, 0x350c, 0xFFFF);
    for (int reg = 0x5480; reg <= 0x5490; reg++) p += print_reg(p, end, s, reg, 0xFF);
    for (int reg = 0x5380; reg <= 0x538b; reg++) p += print_reg(p, end, s, reg, 0xFF);
    for (int reg = 0x5580; reg < 0x558a; reg++)  p += print_reg(p, end, s, reg, 0xFF);
    p += print_reg(p, end, s, 0x558a, 0x1FF);
  } else if (s->id.PID == OV2640_PID) {
    p += print_reg(p, end, s, 0xd3,  0xFF);
    p += print_reg(p, end, s, 0x111, 0xFF);
    p += print_reg(p, end, s, 0x132, 0xFF);
  }
  p += snprintf(p, end-p, "\"xclk\":%u,",          s->xclk_freq_hz / 1000000);
  p += snprintf(p, end-p, "\"pixformat\":%u,",      s->pixformat);
  p += snprintf(p, end-p, "\"framesize\":%u,",      s->status.framesize);
  p += snprintf(p, end-p, "\"quality\":%u,",        s->status.quality);
  p += snprintf(p, end-p, "\"brightness\":%d,",     s->status.brightness);
  p += snprintf(p, end-p, "\"contrast\":%d,",       s->status.contrast);
  p += snprintf(p, end-p, "\"saturation\":%d,",     s->status.saturation);
  p += snprintf(p, end-p, "\"sharpness\":%d,",      s->status.sharpness);
  p += snprintf(p, end-p, "\"special_effect\":%u,", s->status.special_effect);
  p += snprintf(p, end-p, "\"wb_mode\":%u,",        s->status.wb_mode);
  p += snprintf(p, end-p, "\"awb\":%u,",            s->status.awb);
  p += snprintf(p, end-p, "\"awb_gain\":%u,",       s->status.awb_gain);
  p += snprintf(p, end-p, "\"aec\":%u,",            s->status.aec);
  p += snprintf(p, end-p, "\"aec2\":%u,",           s->status.aec2);
  p += snprintf(p, end-p, "\"ae_level\":%d,",       s->status.ae_level);
  p += snprintf(p, end-p, "\"aec_value\":%u,",      s->status.aec_value);
  p += snprintf(p, end-p, "\"agc\":%u,",            s->status.agc);
  p += snprintf(p, end-p, "\"agc_gain\":%u,",       s->status.agc_gain);
  p += snprintf(p, end-p, "\"gainceiling\":%u,",    s->status.gainceiling);
  p += snprintf(p, end-p, "\"bpc\":%u,",            s->status.bpc);
  p += snprintf(p, end-p, "\"wpc\":%u,",            s->status.wpc);
  p += snprintf(p, end-p, "\"raw_gma\":%u,",        s->status.raw_gma);
  p += snprintf(p, end-p, "\"lenc\":%u,",           s->status.lenc);
  p += snprintf(p, end-p, "\"hmirror\":%u,",        s->status.hmirror);
  p += snprintf(p, end-p, "\"vflip\":%u,",          s->status.vflip);
  p += snprintf(p, end-p, "\"dcw\":%u,",            s->status.dcw);
  p += snprintf(p, end-p, "\"colorbar\":%u",        s->status.colorbar);
#if defined(LED_GPIO_NUM)
  p += snprintf(p, end-p, ",\"led_intensity\":%u", led_duty);
#else
  p += snprintf(p, end-p, ",\"led_intensity\":%d", -1);
#endif
  *p++ = '}'; *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static int parse_get_var(char *buf, const char *key, int def) {
  char _int[16];
  if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK) return def;
  return atoi(_int);
}

static esp_err_t xclk_handler(httpd_req_t *req) {
  char *buf = NULL; char _xclk[32];
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK) { free(buf); httpd_resp_send_404(req); return ESP_FAIL; }
  free(buf);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_xclk(s, LEDC_TIMER_0, atoi(_xclk));
  if (res) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t reg_handler(httpd_req_t *req) {
  char *buf = NULL; char _reg[32], _mask[32], _val[32];
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  if (httpd_query_key_value(buf, "reg",  _reg,  sizeof(_reg))  != ESP_OK ||
      httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK ||
      httpd_query_key_value(buf, "val",  _val,  sizeof(_val))  != ESP_OK) {
    free(buf); httpd_resp_send_404(req); return ESP_FAIL;
  }
  free(buf);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_reg(s, atoi(_reg), atoi(_mask), atoi(_val));
  if (res) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t greg_handler(httpd_req_t *req) {
  char *buf = NULL; char _reg[32], _mask[32];
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  if (httpd_query_key_value(buf, "reg",  _reg,  sizeof(_reg))  != ESP_OK ||
      httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK) {
    free(buf); httpd_resp_send_404(req); return ESP_FAIL;
  }
  free(buf);
  sensor_t *s = esp_camera_sensor_get();
  int val = s->get_reg(s, atoi(_reg), atoi(_mask));
  char buf2[20]; snprintf(buf2, sizeof(buf2), "%d", val);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, buf2);
}

static esp_err_t pll_handler(httpd_req_t *req) {
  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  int bypass = parse_get_var(buf,"bypass",0), mul   = parse_get_var(buf,"mul",0),
      sys    = parse_get_var(buf,"sys",0),    root  = parse_get_var(buf,"root",0),
      pre    = parse_get_var(buf,"pre",0),    seld5 = parse_get_var(buf,"seld5",0),
      pclken = parse_get_var(buf,"pclken",0), pclk  = parse_get_var(buf,"pclk",0);
  free(buf);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
  if (res) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t win_handler(httpd_req_t *req) {
  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  int sx = parse_get_var(buf,"sx",0), sy = parse_get_var(buf,"sy",0),
      ex = parse_get_var(buf,"ex",0), ey = parse_get_var(buf,"ey",0),
      offx = parse_get_var(buf,"offx",0), offy  = parse_get_var(buf,"offy",0),
      tx   = parse_get_var(buf,"tx",0),   ty    = parse_get_var(buf,"ty",0),
      ox   = parse_get_var(buf,"ox",0),   oy    = parse_get_var(buf,"oy",0);
  bool scale   = parse_get_var(buf,"scale",0)   == 1;
  bool binning = parse_get_var(buf,"binning",0) == 1;
  free(buf);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_res_raw(s, sx, sy, ex, ey, offx, offy, tx, ty, ox, oy, scale, binning);
  if (res) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    if      (s->id.PID == OV3660_PID) return httpd_resp_send(req, (const char *)index_ov3660_html_gz, index_ov3660_html_gz_len);
    else if (s->id.PID == OV5640_PID) return httpd_resp_send(req, (const char *)index_ov5640_html_gz, index_ov5640_html_gz_len);
    else                              return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
  }
  log_e("Camera sensor not found");
  return httpd_resp_send_500(req);
}

// ── startCameraServer() ──────────────────────────────────────────────────────

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 18;

  httpd_uri_t index_uri   = { .uri = "/",           .method = HTTP_GET, .handler = index_handler,   .user_ctx = NULL };
  httpd_uri_t status_uri  = { .uri = "/status",     .method = HTTP_GET, .handler = status_handler,  .user_ctx = NULL };
  httpd_uri_t cmd_uri     = { .uri = "/control",    .method = HTTP_GET, .handler = cmd_handler,     .user_ctx = NULL };
  httpd_uri_t capture_uri = { .uri = "/capture",    .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL };
  httpd_uri_t bmp_uri     = { .uri = "/bmp",        .method = HTTP_GET, .handler = bmp_handler,     .user_ctx = NULL };
  httpd_uri_t xclk_uri    = { .uri = "/xclk",       .method = HTTP_GET, .handler = xclk_handler,    .user_ctx = NULL };
  httpd_uri_t reg_uri     = { .uri = "/reg",        .method = HTTP_GET, .handler = reg_handler,     .user_ctx = NULL };
  httpd_uri_t greg_uri    = { .uri = "/greg",       .method = HTTP_GET, .handler = greg_handler,    .user_ctx = NULL };
  httpd_uri_t pll_uri     = { .uri = "/pll",        .method = HTTP_GET, .handler = pll_handler,     .user_ctx = NULL };
  httpd_uri_t win_uri     = { .uri = "/resolution", .method = HTTP_GET, .handler = win_handler,     .user_ctx = NULL };

  // ── New IR tracking endpoints ──────────────────────────────────────────
  httpd_uri_t pos_uri       = { .uri = "/pos",       .method = HTTP_GET, .handler = pos_handler,       .user_ctx = NULL };
  httpd_uri_t trackdata_uri = { .uri = "/trackdata", .method = HTTP_GET, .handler = trackdata_handler, .user_ctx = NULL };
  httpd_uri_t track_uri     = { .uri = "/track",     .method = HTTP_GET, .handler = track_handler,     .user_ctx = NULL };
  httpd_uri_t debug_uri     = { .uri = "/debug",     .method = HTTP_GET, .handler = debug_handler,     .user_ctx = NULL };

  ra_filter_init(&ra_filter, 20);

  log_i("Starting web server on port: '%u'", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &bmp_uri);
    httpd_register_uri_handler(camera_httpd, &xclk_uri);
    httpd_register_uri_handler(camera_httpd, &reg_uri);
    httpd_register_uri_handler(camera_httpd, &greg_uri);
    httpd_register_uri_handler(camera_httpd, &pll_uri);
    httpd_register_uri_handler(camera_httpd, &win_uri);
    // IR endpoints
    httpd_register_uri_handler(camera_httpd, &pos_uri);
    httpd_register_uri_handler(camera_httpd, &trackdata_uri);
    httpd_register_uri_handler(camera_httpd, &track_uri);
    httpd_register_uri_handler(camera_httpd, &debug_uri);
  }

  config.server_port += 1;
  config.ctrl_port   += 1;
  log_i("Starting stream server on port: '%u'", config.server_port);
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setupLedFlash() {
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#else
  log_i("LED flash disabled — LED_GPIO_NUM undefined");
#endif
}