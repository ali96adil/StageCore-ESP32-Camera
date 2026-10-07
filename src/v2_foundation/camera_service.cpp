#include "camera_service.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "network_station.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.2.0-dev.c3"
#endif

namespace stagecore {
namespace {

constexpr char kTag[] = "stagecam-v2-camera";
constexpr uint16_t kControlPort = 80;
constexpr uint16_t kStreamPort = 81;
constexpr gpio_num_t kFlashPin = GPIO_NUM_4;
constexpr uint32_t kFrameIntervalMs = 80;
constexpr char kBoundary[] = "stagecoreframe";

std::string g_camera_id;
httpd_handle_t g_control_server = nullptr;
httpd_handle_t g_stream_server = nullptr;
esp_event_handler_instance_t g_wifi_disconnect_handler = nullptr;
std::atomic<bool> g_stream_active{false};
std::atomic<bool> g_flash_on{false};
bool g_camera_ready = false;
bool g_services_started = false;
bool g_psram_available = false;

void send_text(httpd_req_t *req, const char *status, const char *mime,
               const char *body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, mime);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_sendstr(req, body);
}

void set_flash(bool on) {
  (void)gpio_set_level(kFlashPin, on ? 1 : 0);
  g_flash_on.store(on);
}

bool stage_lan_connected() {
  return wait_for_station_connection(0) == ESP_OK;
}

camera_config_t make_camera_config() {
  camera_config_t config = {};
  // Hardware-probe-qualified AI Thinker ESP32-CAM / OV2640 mapping.
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = g_psram_available ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = g_psram_available ? 2 : 1;
  config.fb_location =
      g_psram_available ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  return config;
}

esp_err_t flash_handler(httpd_req_t *req) {
  if (!stage_lan_connected()) {
    set_flash(false);
    send_text(req, "503 Service Unavailable", "text/plain", "not connected");
    return ESP_OK;
  }
  if (req->content_len <= 0 || req->content_len >= 32) {
    send_text(req, "400 Bad Request", "text/plain",
              "expected {\"on\":true|false}");
    return ESP_OK;
  }

  char body[32] = {};
  size_t received = 0;
  while (received < static_cast<size_t>(req->content_len)) {
    const int count = httpd_req_recv(
        req, body + received, req->content_len - received);
    if (count <= 0) {
      send_text(req, "400 Bad Request", "text/plain", "incomplete request");
      return ESP_OK;
    }
    received += static_cast<size_t>(count);
  }
  body[received] = '\0';

  if (std::strcmp(body, "{\"on\":true}") == 0) {
    set_flash(true);
  } else if (std::strcmp(body, "{\"on\":false}") == 0) {
    set_flash(false);
  } else {
    send_text(req, "400 Bad Request", "text/plain",
              "expected {\"on\":true|false}");
    return ESP_OK;
  }

  char json[48];
  std::snprintf(json, sizeof(json), "{\"flash_on\":%s}",
                g_flash_on.load() ? "true" : "false");
  send_text(req, "200 OK", "application/json", json);
  return ESP_OK;
}

esp_err_t health_handler(httpd_req_t *req) {
  if (!stage_lan_connected()) {
    set_flash(false);
    send_text(req, "503 Service Unavailable", "text/plain", "not connected");
    return ESP_OK;
  }

  wifi_ap_record_t ap = {};
  const int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
  const char *state = g_camera_ready ? "ready" : "camera_error";
  const unsigned width =
      g_camera_ready ? (g_psram_available ? 640U : 320U) : 0U;
  const unsigned height =
      g_camera_ready ? (g_psram_available ? 480U : 240U) : 0U;
  const unsigned long uptime_s =
      static_cast<unsigned long>(esp_timer_get_time() / 1000000LL);

  char json[512];
  std::snprintf(
      json, sizeof(json),
      "{\"schema_version\":0,\"camera_id\":\"%s\","
      "\"firmware_version\":\"%s\",\"state\":\"%s\","
      "\"stream\":{\"path\":\"/api/v0/stream\",\"port\":81,"
      "\"format\":\"mjpeg\",\"width\":%u,\"height\":%u,"
      "\"target_fps\":12},\"wifi\":{\"rssi_dbm\":%d},"
      "\"uptime_s\":%lu,\"stream_active\":%s,\"flash_on\":%s}",
      g_camera_id.c_str(), STAGECORE_FW_VERSION, state, width, height, rssi,
      uptime_s, g_stream_active.load() ? "true" : "false",
      g_flash_on.load() ? "true" : "false");
  send_text(req, "200 OK", "application/json", json);
  return ESP_OK;
}

esp_err_t stream_handler(httpd_req_t *req) {
  if (!g_camera_ready || !stage_lan_connected()) {
    send_text(req, "503 Service Unavailable", "text/plain",
              "camera unavailable");
    return ESP_OK;
  }
  if (g_stream_active.exchange(true)) {
    send_text(req, "503 Service Unavailable", "text/plain",
              "one upstream stream only; connect through the StageCore relay");
    return ESP_OK;
  }

  httpd_resp_set_type(
      req, "multipart/x-mixed-replace;boundary=stagecoreframe");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  esp_err_t err = ESP_OK;
  while (stage_lan_connected() && err == ESP_OK) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      ESP_LOGW(kTag, "camera capture failed; closing stream");
      err = ESP_FAIL;
      break;
    }

    char header[96];
    const int header_len = std::snprintf(
        header, sizeof(header),
        "\r\n--%s\r\nContent-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n",
        kBoundary, static_cast<unsigned>(frame->len));
    if (header_len <= 0 ||
        header_len >= static_cast<int>(sizeof(header))) {
      err = ESP_FAIL;
    } else {
      err = httpd_resp_send_chunk(req, header, header_len);
      if (err == ESP_OK) {
        err = httpd_resp_send_chunk(
            req, reinterpret_cast<const char *>(frame->buf), frame->len);
      }
    }
    esp_camera_fb_return(frame);

    if (err == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(kFrameIntervalMs));
    }
  }

  g_stream_active.store(false);
  return err;
}

esp_err_t start_control_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = kControlPort;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 2;
  config.stack_size = 8192;
  esp_err_t err = httpd_start(&g_control_server, &config);
  if (err != ESP_OK) return err;

  httpd_uri_t health = {};
  health.uri = "/api/v0/health";
  health.method = HTTP_GET;
  health.handler = health_handler;
  err = httpd_register_uri_handler(g_control_server, &health);
  if (err != ESP_OK) return err;

  httpd_uri_t flash = {};
  flash.uri = "/api/v0/flash";
  flash.method = HTTP_POST;
  flash.handler = flash_handler;
  return httpd_register_uri_handler(g_control_server, &flash);
}

esp_err_t start_stream_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = kStreamPort;
  config.ctrl_port = 32769;
  config.max_uri_handlers = 1;
  config.stack_size = 8192;
  esp_err_t err = httpd_start(&g_stream_server, &config);
  if (err != ESP_OK) return err;

  httpd_uri_t stream = {};
  stream.uri = "/api/v0/stream";
  stream.method = HTTP_GET;
  stream.handler = stream_handler;
  return httpd_register_uri_handler(g_stream_server, &stream);
}

void camera_wifi_event(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    // Hardware-safe invariant: loss of Stage LAN can never leave flash ON.
    set_flash(false);
  }
}

esp_err_t ensure_wifi_disconnect_failsafe() {
  if (g_wifi_disconnect_handler != nullptr) return ESP_OK;
  return esp_event_handler_instance_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &camera_wifi_event, nullptr,
      &g_wifi_disconnect_handler);
}

esp_err_t advertise_camera_service() {
  esp_err_t err = mdns_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  err = mdns_hostname_set(g_camera_id.c_str());
  if (err != ESP_OK) return err;

  (void)mdns_service_remove("_stagecore-camera", "_tcp");
  mdns_txt_item_t txt[] = {{"stream-port", "81"}};
  return mdns_service_add(
      nullptr, "_stagecore-camera", "_tcp", kControlPort, txt, 1);
}

}  // namespace

esp_err_t initialize_camera_hardware() {
  uint8_t mac[6] = {};
  esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (err != ESP_OK) return err;

  char id[32];
  std::snprintf(id, sizeof(id), "stagecam-%02x%02x%02x",
                mac[3], mac[4], mac[5]);
  g_camera_id = id;

  gpio_config_t flash = {};
  flash.pin_bit_mask = 1ULL << static_cast<unsigned>(kFlashPin);
  flash.mode = GPIO_MODE_OUTPUT;
  flash.pull_up_en = GPIO_PULLUP_DISABLE;
  flash.pull_down_en = GPIO_PULLDOWN_DISABLE;
  flash.intr_type = GPIO_INTR_DISABLE;
  err = gpio_config(&flash);
  if (err != ESP_OK) return err;
  set_flash(false);

  g_psram_available =
      heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
  camera_config_t config = make_camera_config();
  err = esp_camera_init(&config);
  g_camera_ready = err == ESP_OK;
  ESP_LOGI(kTag, "camera_id=%s psram=%s init=%s",
           g_camera_id.c_str(), g_psram_available ? "yes" : "no",
           g_camera_ready ? "PASS" : esp_err_to_name(err));
  return err;
}

esp_err_t start_camera_network_services() {
  if (g_services_started) return ESP_OK;
  if (g_camera_id.empty() || !stage_lan_connected()) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = ensure_wifi_disconnect_failsafe();
  if (err != ESP_OK) return err;
  err = advertise_camera_service();
  if (err != ESP_OK) return err;
  err = start_control_server();
  if (err == ESP_OK) err = start_stream_server();
  if (err != ESP_OK) {
    stop_camera_network_services();
    return err;
  }

  g_services_started = true;
  ESP_LOGI(kTag,
           "camera service ready: http://%s.local/api/v0/health ; "
           "stream http://%s.local:81/api/v0/stream",
           g_camera_id.c_str(), g_camera_id.c_str());
  return ESP_OK;
}

void stop_camera_network_services() {
  // Always force the physical output safe before stopping any network surface.
  set_flash(false);
  if (g_stream_server != nullptr) {
    httpd_stop(g_stream_server);
    g_stream_server = nullptr;
  }
  if (g_control_server != nullptr) {
    httpd_stop(g_control_server);
    g_control_server = nullptr;
  }
  if (!g_camera_id.empty()) {
    (void)mdns_service_remove("_stagecore-camera", "_tcp");
  }
  g_stream_active.store(false);
  g_services_started = false;
}

}  // namespace stagecore
