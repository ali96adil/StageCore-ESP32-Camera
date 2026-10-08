#include "provisioning.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "config_store.h"
#include "foundation_contract.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_station.h"

namespace stagecore {
namespace {

constexpr char kTag[] = "stagecam-v2-setup";
static_assert(sizeof(kDefaultSetupAPPassword) - 1 >= 8 &&
                  sizeof(kDefaultSetupAPPassword) - 1 <= 63,
              "StageCore setup AP password must be 8-63 bytes");

struct PortalContext {
  bool recovery = false;
};

std::string effective_setup_ap_password() {
  std::string password;
  const esp_err_t err =
      foundation_store().EffectiveSetupAPPassword(&password);
  if (err == ESP_OK) return password;
  ESP_LOGW(kTag,
           "stored Setup AP credential unavailable; using canonical fallback");
  return kDefaultSetupAPPassword;
}

std::string id_suffix(const std::string &device_id) {
  std::string compact;
  for (char ch : device_id) {
    if (std::isxdigit(static_cast<unsigned char>(ch))) {
      compact.push_back(ch);
    }
  }
  if (compact.size() > 6) compact = compact.substr(compact.size() - 6);
  return compact;
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return -1;
}

bool url_decode(const std::string &input, std::string *output) {
  if (output == nullptr) return false;
  output->clear();
  output->reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '+') {
      output->push_back(' ');
      continue;
    }
    if (input[i] == '%') {
      if (i + 2 >= input.size()) return false;
      const int hi = hex_value(input[i + 1]);
      const int lo = hex_value(input[i + 2]);
      if (hi < 0 || lo < 0) return false;
      const char value = static_cast<char>((hi << 4) | lo);
      if (value == '\0') return false;
      output->push_back(value);
      i += 2;
      continue;
    }
    if (input[i] == '\0') return false;
    output->push_back(input[i]);
  }
  return true;
}

bool form_value(const std::string &body, const char *key,
                std::string *value) {
  if (key == nullptr || value == nullptr) return false;
  size_t start = 0;
  while (start <= body.size()) {
    const size_t end = body.find('&', start);
    const size_t stop = end == std::string::npos ? body.size() : end;
    const size_t eq = body.find('=', start);
    if (eq != std::string::npos && eq < stop &&
        body.substr(start, eq - start) == key) {
      return url_decode(body.substr(eq + 1, stop - eq - 1), value);
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return false;
}

esp_err_t root_handler(httpd_req_t *req) {
  auto *context = static_cast<PortalContext *>(req->user_ctx);
  const bool recovery = context != nullptr && context->recovery;

  std::string page =
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>StageCore Camera Setup</title>"
      "<style>body{font-family:system-ui;max-width:620px;margin:40px auto;"
      "padding:0 18px}label{display:block;margin:14px 0 5px}"
      "input{width:100%;padding:10px;box-sizing:border-box}"
      "button{margin-top:20px;padding:11px 18px}small{color:#666}</style>"
      "</head><body><h1>StageCore Camera</h1>";

  if (recovery) {
    page +=
        "<p>Stage LAN recovery. Camera capture, MJPEG and flash output are "
        "disabled in this Foundation candidate.</p>";
  } else {
    page +=
        "<p>First-run Stage LAN setup. Camera capture, MJPEG and flash output "
        "remain disabled in this Foundation candidate.</p>";
  }

  page +=
      "<form method='post' action='/save'>"
      "<label>Wi-Fi SSID</label>"
      "<input name='ssid' maxlength='32' required>"
      "<label>Wi-Fi password</label>"
      "<input name='password' type='password' maxlength='63'>"
      "<small>Password may be empty only for an open Stage LAN. "
      "Setup/Recovery AP credentials are managed separately by StageCore.</small>"
      "<button type='submit'>Save and restart</button></form>"
      "</body></html>";

  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t save_handler(httpd_req_t *req) {
  if (req->content_len == 0 || req->content_len > 1024) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid form");
    return ESP_FAIL;
  }

  std::string body(req->content_len, '\0');
  size_t received = 0;
  while (received < req->content_len) {
    const int rc = httpd_req_recv(
        req, body.data() + received, req->content_len - received);
    if (rc <= 0) {
      httpd_resp_send_err(
          req, HTTPD_500_INTERNAL_SERVER_ERROR, "receive failed");
      return ESP_FAIL;
    }
    received += static_cast<size_t>(rc);
  }

  WifiConfig config;
  if (!form_value(body, "ssid", &config.ssid) ||
      !form_value(body, "password", &config.password) ||
      !config.complete()) {
    httpd_resp_send_err(
        req, HTTPD_400_BAD_REQUEST,
        "SSID must be 1-32 bytes; password must be empty or 8-63 bytes");
    return ESP_FAIL;
  }

  const esp_err_t err = save_wifi_config(config);
  std::fill(config.password.begin(), config.password.end(), '\0');
  if (err != ESP_OK) {
    httpd_resp_send_err(
        req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    return err;
  }

  const char *response =
      "<html><body><h1>Saved</h1>"
      "<p>Camera Foundation candidate is restarting.</p></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
  vTaskDelay(pdMS_TO_TICKS(800));
  esp_restart();
  return ESP_OK;
}

esp_err_t start_portal_server(PortalContext *context,
                              httpd_handle_t *server) {
  if (context == nullptr || server == nullptr) return ESP_ERR_INVALID_ARG;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 2;
  config.lru_purge_enable = true;
  esp_err_t err = httpd_start(server, &config);
  if (err != ESP_OK) return err;

  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = root_handler;
  root.user_ctx = context;
  err = httpd_register_uri_handler(*server, &root);
  if (err != ESP_OK) {
    httpd_stop(*server);
    *server = nullptr;
    return err;
  }

  httpd_uri_t save{};
  save.uri = "/save";
  save.method = HTTP_POST;
  save.handler = save_handler;
  save.user_ctx = context;
  err = httpd_register_uri_handler(*server, &save);
  if (err != ESP_OK) {
    httpd_stop(*server);
    *server = nullptr;
    return err;
  }
  return ESP_OK;
}

esp_err_t start_setup_ap(const std::string &device_id, bool recovery) {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == nullptr &&
      esp_netif_create_default_wifi_ap() == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  wifi_mode_t current_mode = WIFI_MODE_NULL;
  const bool wifi_initialized = esp_wifi_get_mode(&current_mode) == ESP_OK;
  if (!wifi_initialized) {
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
  }

  const std::string ssid = "StageCore-CAM-" + id_suffix(device_id);
  const std::string password = effective_setup_ap_password();

  wifi_config_t ap{};
  std::snprintf(
      reinterpret_cast<char *>(ap.ap.ssid), sizeof(ap.ap.ssid),
      "%s", ssid.c_str());
  ap.ap.ssid_len = static_cast<uint8_t>(ssid.size());
  std::snprintf(
      reinterpret_cast<char *>(ap.ap.password), sizeof(ap.ap.password),
      "%s", password.c_str());
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap.ap.max_connection = 2;

  err = esp_wifi_set_mode(recovery ? WIFI_MODE_APSTA : WIFI_MODE_AP);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
  if (err != ESP_OK) return err;
  if (!wifi_initialized) return esp_wifi_start();
  return ESP_OK;
}

}  // namespace

[[noreturn]] void run_camera_provisioning_portal(
    const std::string &device_id) {
  const esp_err_t network = start_setup_ap(device_id, false);
  if (network != ESP_OK) {
    ESP_LOGE(kTag, "unable to start first-run Setup AP: %s",
             esp_err_to_name(network));
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  PortalContext context;
  context.recovery = false;
  httpd_handle_t server = nullptr;
  const esp_err_t server_err = start_portal_server(&context, &server);
  if (server_err != ESP_OK) {
    ESP_LOGE(kTag, "unable to start first-run setup portal: %s",
             esp_err_to_name(server_err));
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  const std::string ssid = "StageCore-CAM-" + id_suffix(device_id);
  ESP_LOGW(kTag, "FIRST-RUN SETUP AVAILABLE");
  ESP_LOGW(kTag, "join Wi-Fi SSID: %s", ssid.c_str());
  ESP_LOGW(kTag, "open http://192.168.4.1/");
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

esp_err_t run_camera_recovery_portal(
    const std::string &device_id) {
  esp_err_t err = start_setup_ap(device_id, true);
  if (err != ESP_OK) return err;

  PortalContext context;
  context.recovery = true;
  httpd_handle_t server = nullptr;
  err = start_portal_server(&context, &server);
  if (err != ESP_OK) {
    (void)esp_wifi_set_mode(WIFI_MODE_STA);
    return err;
  }

  const std::string ssid = "StageCore-CAM-" + id_suffix(device_id);
  ESP_LOGW(kTag, "STAGE LAN RECOVERY AVAILABLE");
  ESP_LOGW(kTag, "join Wi-Fi SSID: %s", ssid.c_str());
  ESP_LOGW(kTag, "open http://192.168.4.1/");

  while (wait_for_station_connection(1000) != ESP_OK) {
    // Existing STA reconnect remains active while the protected AP is present.
  }

  httpd_stop(server);
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) {
    ESP_LOGI(kTag, "Stage LAN recovered; Recovery AP disabled");
  }
  return err;
}

}  // namespace stagecore
