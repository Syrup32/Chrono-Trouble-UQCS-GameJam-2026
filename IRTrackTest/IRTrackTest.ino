#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>

// ===========================
// Hostname — board reachable at http://irgun.local/
// Change this string if you have two guns on the same network
// (e.g. "irgun2" for the second controller)
// ===========================
#define MDNS_HOSTNAME "irgun"

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// IR tracking modules
// ===========================
#include "ir_tracker.h"
#include "ir_position.h"

// ===========================
// WiFi credentials
// ===========================
const char *ssid     = "Samsung Smart Fridge";
const char *password = "pqyt1007";

// ===========================
// Trigger switch
// SS5GL wired: GPIO13 → Common, 3.3V → NO, GND → NC
// HIGH = pressed, LOW = released
// ===========================
#define TRIGGER_PIN          13
#define DEBOUNCE_DELAY_MS    50   // ms to wait for signal to stabilise
                                  // increase if you still get phantom presses

volatile bool g_triggerPressed = false;  // debounced state exposed to HTTP

// Debounce state — not shared, only used in loop()
static bool     _triggerLastRaw      = LOW;
static bool     _triggerStableState  = LOW;
static unsigned long _triggerLastChangeMs = 0;

// ===========================
// Global tracking state
// (shared with app_httpd.cpp via extern)
// ===========================
volatile float g_normX     = 0.5f;  // normalised aim X (0=left,  1=right)
volatile float g_normY     = 0.5f;  // normalised aim Y (0=top,   1=bottom)
volatile int   g_blobCount = 0;     // blobs seen this frame
volatile bool  g_tracking  = false; // true when ≥4 blobs found

// Per-blob pixel positions (up to MAX_BLOBS=16) for the live dashboard
#define G_MAX_BLOBS 16
volatile float  g_blobCx[G_MAX_BLOBS];
volatile float  g_blobCy[G_MAX_BLOBS];
volatile int    g_blobArea[G_MAX_BLOBS];

// Corner positions in pixel space (filled when tracking=true)
volatile float  g_cornerTLx, g_cornerTLy;
volatile float  g_cornerTRx, g_cornerTRy;
volatile float  g_cornerBLx, g_cornerBLy;
volatile float  g_cornerBRx, g_cornerBRy;

// Frame dimensions (set once on first frame)
volatile int    g_frameW = 320;
volatile int    g_frameH = 240;

// ===========================
// Forward declarations
// ===========================
void startCameraServer();
void setupLedFlash();

// ── setup() ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);   // reduce noise — our prints are cleaner
  Serial.println("\n\n=== IR Gun Tracker ===");

  // ── Trigger pin ───────────────────────────────────────────────────────
  // No pull resistor needed when switch is connected — it always ties to a rail.
  // INPUT_PULLDOWN keeps the pin firmly LOW when the switch is disconnected,
  // preventing floating reads during development.
  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);

  // ── Camera config ──────────────────────────────────────────────────────
  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sccb_sda  = SIOD_GPIO_NUM;
  config.pin_sccb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;

  // ── Key change from stock sketch ──────────────────────────────────────
  //  PIXFORMAT_GRAYSCALE  → single byte per pixel, no decode needed
  //  FRAMESIZE_QVGA       → 320×240, fast enough for 30+ fps blob scan
  //  Reduce to QQVGA (160×120) if you need lower latency; increase to
  //  HVGA (480×320) if you need more spatial resolution for the solve.
  config.pixel_format  = PIXFORMAT_GRAYSCALE;
  config.frame_size    = FRAMESIZE_QVGA;       // 320 × 240
  config.grab_mode     = CAMERA_GRAB_LATEST;   // always the freshest frame
  config.fb_location   = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality  = 12;
  config.fb_count      = 2;

  if (!psramFound()) {
    Serial.println("WARN: No PSRAM — falling back to DRAM, single buffer");
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED: 0x%x\n", err);
    return;
  }

  // ── Sensor tuning for IR ───────────────────────────────────────────────
  sensor_t *s = esp_camera_sensor_get();

  // Kill auto-exposure — the IR LEDs are very bright; AE will try to
  // compensate and wash the scene grey. Lock exposure low.
  s->set_exposure_ctrl(s, 0);      // AEC off
  s->set_aec2(s, 0);               // AEC DSP off
  s->set_aec_value(s, 1536);        // manual exposure — start low, tune up
                                   // Range 0-1200 on OV2640; lower = darker bg

  s->set_gain_ctrl(s, 0);          // AGC off
  s->set_agc_gain(s, 0);           // gain to minimum

  s->set_whitebal(s, 0);           // AWB off — grayscale, doesn't matter much
  s->set_brightness(s, 0);
  s->set_contrast(s, 2);           // boost contrast to sharpen blobs vs bg

  // Orientation correction for AI-Thinker board.
  // The lens is physically rotated 180° on this module, so both axes need
  // correcting. If the stream still appears wrong after flashing, toggle
  // one or both of these between 0 and 1 — the correct pair depends on
  // exactly how your module is mounted inside the gun shell.
  //
  //  Current behaviour:
  //    vflip  1 = image right-side up vertically
  //    hmirror 0 = panning left moves crosshair left (natural)
  //
  //  If vertical is flipped:  change vflip  to 0
  //  If horizontal is mirrored: change hmirror to 1
  s->set_vflip(s, 1);
  s->set_hmirror(s, 0);

  // ── LED flash ─────────────────────────────────────────────────────────
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  // ── WiFi ──────────────────────────────────────────────────────────────
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());

  // ── mDNS — advertise a fixed hostname on the local network ────────────
  if (MDNS.begin(MDNS_HOSTNAME)) {
    // Advertise the HTTP service so browsers and Unity can discover it
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS started — hostname: http://%s.local/\n", MDNS_HOSTNAME);
  } else {
    Serial.println("WARN: mDNS failed to start — use IP address instead");
  }

  // ── HTTP server ───────────────────────────────────────────────────────
  startCameraServer();

  Serial.println("──────────────────────────────────────────");
  Serial.printf("  Camera:    http://%s.local/\n",      MDNS_HOSTNAME);
  Serial.printf("  Dashboard: http://%s.local/track\n", MDNS_HOSTNAME);
  Serial.printf("  Position:  http://%s.local/pos\n",   MDNS_HOSTNAME);
  Serial.printf("  Debug:     http://%s.local/debug\n", MDNS_HOSTNAME);
  Serial.println("──────────────────────────────────────────");
  Serial.println("Serial: AIM x y printed each frame");
}

// ── loop() ──────────────────────────────────────────────────────────────────
//
// The tracking loop runs here on Core 1 (Arduino default).
// The HTTP server runs in a separate FreeRTOS task on Core 0.
// Reads the camera, finds blobs, solves aim, prints to Serial and updates
// the shared globals that app_httpd.cpp serves via /pos.
//
void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    delay(5);
    return;
  }

  // Safety check — we need grayscale raw pixels
  if (fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("ERR: frame is not grayscale — check pixel_format config");
    esp_camera_fb_return(fb);
    delay(1000);
    return;
  }

  // ── Blob detection ─────────────────────────────────────────────────────
  BlobResult blobs = findBlobs(fb);

  // ── Position solve ─────────────────────────────────────────────────────
  TrackingResult track = solveAim(blobs, (int)fb->width, (int)fb->height);

  esp_camera_fb_return(fb);   // return buffer ASAP — needed for next frame

  // ── Update shared state (read by HTTP handlers) ───────────────────────
  g_frameW         = (int)fb->width;
  g_frameH         = (int)fb->height;
  g_blobCount      = blobs.count;
  g_tracking       = track.valid;
  // ── Trigger debounce ───────────────────────────────────────────────────
  bool rawTrigger = (digitalRead(TRIGGER_PIN) == HIGH);
  if (rawTrigger != _triggerLastRaw) {
    // Signal changed — reset the stability timer
    _triggerLastChangeMs = millis();
    _triggerLastRaw = rawTrigger;
  }
  if ((millis() - _triggerLastChangeMs) >= DEBOUNCE_DELAY_MS) {
    // Signal has been stable long enough — accept it
    _triggerStableState = _triggerLastRaw;
  }
  g_triggerPressed = _triggerStableState;

  // Copy per-blob data for the dashboard
  int n = blobs.count < G_MAX_BLOBS ? blobs.count : G_MAX_BLOBS;
  for (int i = 0; i < n; i++) {
    g_blobCx[i]   = blobs.blobs[i].cx;
    g_blobCy[i]   = blobs.blobs[i].cy;
    g_blobArea[i] = blobs.blobs[i].area;
  }

  if (track.valid) {
    g_normX      = track.norm.x;
    g_normY      = track.norm.y;
    g_cornerTLx  = track.tl.x;  g_cornerTLy = track.tl.y;
    g_cornerTRx  = track.tr.x;  g_cornerTRy = track.tr.y;
    g_cornerBLx  = track.bl.x;  g_cornerBLy = track.bl.y;
    g_cornerBRx  = track.br.x;  g_cornerBRy = track.br.y;
  }

  // ── Serial output — simulated HID report (throttled to ~4Hz) ─────────
  static unsigned long _lastSerialMs = 0;
  if (millis() - _lastSerialMs >= 250) {
    _lastSerialMs = millis();

    uint8_t  buttons   = (g_triggerPressed ? 0x01 : 0x00)
                       | (!g_tracking      ? 0x02 : 0x00);
    int16_t  aimX      = g_tracking ? (int16_t)(g_normX * 32767) : 0;
    int16_t  aimY      = g_tracking ? (int16_t)(g_normY * 32767) : 0;
    uint8_t  tracking  = g_tracking ? 1 : 0;
    uint8_t  blobCount = (uint8_t)g_blobCount;

    Serial.println("──── HID REPORT ────────────────────────");
    Serial.printf("  Buttons  : 0x%02X  (TRIGGER:%d  LOST:%d)\n",
                  buttons,
                  (buttons & 0x01) ? 1 : 0,
                  (buttons & 0x02) ? 1 : 0);
    Serial.printf("  Aim X    : %5d  (norm: %.4f)\n", aimX, g_normX);
    Serial.printf("  Aim Y    : %5d  (norm: %.4f)\n", aimY, g_normY);
    Serial.printf("  Tracking : %d\n", tracking);
    Serial.printf("  Blobs    : %d\n", blobCount);
    Serial.printf("  Raw bytes: %02X %02X %02X %02X %02X %02X %02X\n",
                  buttons,
                  (uint8_t)(aimX & 0xFF), (uint8_t)((aimX >> 8) & 0xFF),
                  (uint8_t)(aimY & 0xFF), (uint8_t)((aimY >> 8) & 0xFF),
                  tracking,
                  blobCount);
    Serial.println("────────────────────────────────────────");
  }

  // ── Optional: print corner pixel positions for calibration ────────────
  // Uncomment to see raw corner detection (useful when first tuning threshold):
  /*
  if (track.valid) {
    Serial.printf("  TL(%.0f,%.0f) TR(%.0f,%.0f) BL(%.0f,%.0f) BR(%.0f,%.0f)\n",
      track.tl.x, track.tl.y,
      track.tr.x, track.tr.y,
      track.bl.x, track.bl.y,
      track.br.x, track.br.y);
  }
  */

  // No explicit delay — grab_mode CAMERA_GRAB_LATEST already throttles us
  // to the sensor frame rate (~30fps at QVGA). Add delay(N) here only if
  // you want to reduce CPU load at the cost of latency.
}
