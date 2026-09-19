// StageCore ESP32-CAM: initial hardware probe, not a live streaming firmware.
// Assumes the standard AI Thinker camera pin map pending physical qualification.
// No Wi-Fi credentials or network services are used in this build.
#include <Arduino.h>
#include "esp_camera.h"

namespace {
constexpr int kPwdn = 32;
constexpr int kReset = -1;
constexpr int kXclk = 0;
constexpr int kSiod = 26;
constexpr int kSioc = 27;
constexpr int kD7 = 35;
constexpr int kD6 = 34;
constexpr int kD5 = 39;
constexpr int kD4 = 36;
constexpr int kD3 = 21;
constexpr int kD2 = 19;
constexpr int kD1 = 18;
constexpr int kD0 = 5;
constexpr int kVsync = 25;
constexpr int kHref = 23;
constexpr int kPclk = 22;

bool cameraReady = false;

void printHardware() {
  Serial.printf("chip=%s revision=%u\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("flash_bytes=%u\n", static_cast<unsigned>(ESP.getFlashChipSize()));
  Serial.printf("psram_detected=%s psram_bytes=%u\n",
                psramFound() ? "yes" : "no",
                static_cast<unsigned>(ESP.getPsramSize()));
  Serial.printf("free_heap_bytes=%u\n", static_cast<unsigned>(ESP.getFreeHeap()));
}

void initializeCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = kD0;
  config.pin_d1 = kD1;
  config.pin_d2 = kD2;
  config.pin_d3 = kD3;
  config.pin_d4 = kD4;
  config.pin_d5 = kD5;
  config.pin_d6 = kD6;
  config.pin_d7 = kD7;
  config.pin_xclk = kXclk;
  config.pin_pclk = kPclk;
  config.pin_vsync = kVsync;
  config.pin_href = kHref;
  config.pin_sscb_sda = kSiod;
  config.pin_sscb_scl = kSioc;
  config.pin_pwdn = kPwdn;
  config.pin_reset = kReset;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = psramFound() ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("camera_init=FAIL esp_err=0x%x\n", static_cast<unsigned>(err));
    Serial.println("STOP: verify board/sensor/power before changing camera pins.");
    return;
  }

  cameraReady = true;
  Serial.println("camera_init=PASS");
  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    Serial.printf("sensor_pid=0x%04x\n", static_cast<unsigned>(sensor->id.PID));
  }
}

void captureOnce() {
  camera_fb_t* frame = esp_camera_fb_get();
  if (frame == nullptr) {
    Serial.println("capture=FAIL");
    return;
  }

  Serial.printf("capture=PASS bytes=%u width=%u height=%u\n",
                static_cast<unsigned>(frame->len),
                static_cast<unsigned>(frame->width),
                static_cast<unsigned>(frame->height));
  esp_camera_fb_return(frame);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\nSTAGECORE_CAM_PROBE_v0");
  printHardware();
  initializeCamera();
}

void loop() {
  if (cameraReady) {
    captureOnce();
  }
  delay(5000);
}
