#include <Arduino.h>
#include "esp_camera.h"
#include <BleGamepad.h>

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
// BLE Gamepad
// Change "IR Gun P1" to "IR Gun P2" for the second controller.
// Windows pairs each name as a separate device.
// ===========================
BleGamepadConfiguration bleGamepadConfig;
BleGamepad bleGamepad("IR Gun P2", "IRGun", 100);

// ===========================
// Trigger switch
// SS5GL wired: GPIO13 → Common, 3.3V → NO, GND → NC
// HIGH = pressed, LOW = released
// ===========================
#define TRIGGER_PIN        13
#define DEBOUNCE_DELAY_MS  50   // ms — increase if phantom presses occur

// Debounce state
static bool          _triggerLastRaw      = LOW;
static bool          _triggerStableState  = LOW;
static unsigned long _triggerLastChangeMs = 0;

// ===========================
// Tracking state
// ===========================
static float g_normX        = 0.5f;
static float g_normY        = 0.5f;
static int   g_blobCount    = 0;
static bool  g_tracking     = false;
static bool  g_triggerPressed = false;

// ── setup() ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== IR Gun — BLE Mode ===");

  // ── Trigger pin ───────────────────────────────────────────────────────────
  // INPUT_PULLDOWN holds GPIO13 at LOW when switch is disconnected.
  // When switch is connected, direct rail connection overrides the pulldown.
  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);

  // ── BLE Gamepad init ──────────────────────────────────────────────────────
  // Config must be set before begin()
  bleGamepadConfig.setAutoReport(false);     // manual sendReport() each frame
  bleGamepadConfig.setControllerType(CONTROLLER_TYPE_GAMEPAD);
  bleGamepadConfig.setButtonCount(2);        // BUTTON_1=trigger, BUTTON_2=lost
  bleGamepadConfig.setWhichAxes(
    true,   // X  — aim horizontal
    true,   // Y  — aim vertical
    false, false, false, false, false, false
  );
  bleGamepadConfig.setAxesMin(-32767);
  bleGamepadConfig.setAxesMax(32767);

  bleGamepad.begin(&bleGamepadConfig);
  Serial.println("BLE advertising as \"IR Gun P1\"");
  Serial.println("Pair via Windows Bluetooth settings, then check joy.cpl");

  // ── Camera config ─────────────────────────────────────────────────────────
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

  // GRAYSCALE + QVGA — fast enough for 30+ fps blob detection
  // Drop to QQVGA (160×120) if BLE stack causes frame rate issues
  config.pixel_format  = PIXFORMAT_GRAYSCALE;
  config.frame_size    = FRAMESIZE_QVGA;      // 320 × 240
  config.grab_mode     = CAMERA_GRAB_LATEST;
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

  // ── Sensor tuning for IR ──────────────────────────────────────────────────
  sensor_t *s = esp_camera_sensor_get();

  s->set_exposure_ctrl(s, 0);    // AEC off — lock to manual value
  s->set_aec2(s, 0);             // AEC DSP off
  s->set_aec_value(s, 1536);     // max exposure — tuned for dim IR LEDs

  s->set_gain_ctrl(s, 0);        // AGC off
  s->set_agc_gain(s, 0);         // gain minimum

  s->set_whitebal(s, 0);         // AWB off — irrelevant for grayscale
  s->set_brightness(s, 0);
  s->set_contrast(s, 2);         // boost contrast to sharpen blobs vs background

  // Orientation — AI-Thinker lens is physically rotated 180°
  // vflip=1 corrects vertical. hmirror=0 = natural left/right.
  // Toggle either if orientation is wrong for your gun mounting angle.
  s->set_vflip(s, 1);
  s->set_hmirror(s, 0);

  // ── LED flash pin ─────────────────────────────────────────────────────────
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#endif

  Serial.println("Camera ready");
  Serial.println("Waiting for BLE connection...");
}

// ── loop() ───────────────────────────────────────────────────────────────────
void loop() {

  // ── Trigger debounce ──────────────────────────────────────────────────────
  bool rawTrigger = (digitalRead(TRIGGER_PIN) == HIGH);
  if (rawTrigger != _triggerLastRaw) {
    _triggerLastChangeMs = millis();
    _triggerLastRaw      = rawTrigger;
  }
  if ((millis() - _triggerLastChangeMs) >= DEBOUNCE_DELAY_MS) {
    _triggerStableState = _triggerLastRaw;
  }
  g_triggerPressed = _triggerStableState;

  // ── Camera frame ──────────────────────────────────────────────────────────
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    delay(5);
    return;
  }

  if (fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("ERR: frame not grayscale — check pixel_format config");
    esp_camera_fb_return(fb);
    delay(1000);
    return;
  }

  // ── Blob detection & position solve ───────────────────────────────────────
  BlobResult     blobs = findBlobs(fb);
  TrackingResult track = solveAim(blobs, (int)fb->width, (int)fb->height);
  esp_camera_fb_return(fb);   // release ASAP — needed for next frame

  g_blobCount = blobs.count;
  g_tracking  = track.valid;
  if (track.valid) {
    g_normX = track.norm.x;
    g_normY = track.norm.y;
  }
  // If tracking lost, g_normX/Y holds last known position — crosshair
  // stays put rather than snapping to 0,0 on a dropped frame

  // ── BLE HID report ────────────────────────────────────────────────────────
  if (bleGamepad.isConnected()) {

    // Map normalised 0.0–1.0 to axis range 0–32767
    bleGamepad.setX((int16_t)(g_normX * 32767.0f));
    bleGamepad.setY((int16_t)(g_normY * 32767.0f));

    // BUTTON_1 — trigger / fire
    if (g_triggerPressed) bleGamepad.press(BUTTON_1);
    else                  bleGamepad.release(BUTTON_1);

    // BUTTON_2 — tracking lost = reload signal for Unity
    if (!g_tracking) bleGamepad.press(BUTTON_2);
    else             bleGamepad.release(BUTTON_2);

    bleGamepad.sendReport();
  }

  // ── Serial — throttled to ~4Hz ────────────────────────────────────────────
  static unsigned long _lastSerialMs = 0;
  if (millis() - _lastSerialMs >= 250) {
    _lastSerialMs = millis();

    uint8_t buttons = (g_triggerPressed ? 0x01 : 0x00)
                    | (!g_tracking      ? 0x02 : 0x00);
    int16_t aimX    = (int16_t)(g_normX * 32767.0f);
    int16_t aimY    = (int16_t)(g_normY * 32767.0f);

    Serial.println("──── HID REPORT ─────────────────────────");
    Serial.printf("  BLE      : %s\n",
                  bleGamepad.isConnected() ? "CONNECTED" : "waiting...");
    Serial.printf("  Buttons  : 0x%02X  (TRIGGER:%d  LOST:%d)\n",
                  buttons,
                  (buttons & 0x01) ? 1 : 0,
                  (buttons & 0x02) ? 1 : 0);
    Serial.printf("  Aim X    : %5d  (norm: %.4f)\n", aimX, g_normX);
    Serial.printf("  Aim Y    : %5d  (norm: %.4f)\n", aimY, g_normY);
    Serial.printf("  Tracking : %d  Blobs: %d\n",
                  g_tracking ? 1 : 0, g_blobCount);
    Serial.printf("  Raw bytes: %02X %02X %02X %02X %02X %02X %02X\n",
                  buttons,
                  (uint8_t)(aimX & 0xFF),         (uint8_t)((aimX >> 8) & 0xFF),
                  (uint8_t)(aimY & 0xFF),         (uint8_t)((aimY >> 8) & 0xFF),
                  (uint8_t)(g_tracking ? 1 : 0),  (uint8_t)g_blobCount);
    Serial.println("─────────────────────────────────────────");
  }
}
