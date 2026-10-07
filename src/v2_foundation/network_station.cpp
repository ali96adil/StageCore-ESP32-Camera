#include "network_station.h"

#include <cstdio>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace stagecore {
namespace {

constexpr char kTag[] = "stagecam-v2-net";
constexpr EventBits_t kConnectedBit = BIT0;

EventGroupHandle_t g_events = nullptr;
esp_event_handler_instance_t g_wifi_handler = nullptr;
esp_event_handler_instance_t g_ip_handler = nullptr;
bool g_handlers_registered = false;
bool g_wifi_initialized = false;

void event_handler(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    (void)esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_events != nullptr) xEventGroupClearBits(g_events, kConnectedBit);
    // C1 has no Recovery AP. Keep retrying the already-provisioned Stage LAN;
    // the proven Arduino stream image remains the provisioning/rollback image.
    (void)esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    if (g_events != nullptr) xEventGroupSetBits(g_events, kConnectedBit);
  }
}

}  // namespace

esp_err_t connect_station(const WifiConfig &config, int timeout_ms) {
  if (!config.complete()) return ESP_ERR_INVALID_ARG;

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  if (g_events == nullptr) g_events = xEventGroupCreate();
  if (g_events == nullptr) return ESP_ERR_NO_MEM;
  xEventGroupClearBits(g_events, kConnectedBit);

  if (!g_wifi_initialized) {
    if (esp_netif_create_default_wifi_sta() == nullptr) return ESP_FAIL;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    g_wifi_initialized = true;
  }

  if (!g_handlers_registered) {
    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr, &g_wifi_handler);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr, &g_ip_handler);
    if (err != ESP_OK) return err;
    g_handlers_registered = true;
  }

  wifi_config_t wifi{};
  std::snprintf(reinterpret_cast<char *>(wifi.sta.ssid), sizeof(wifi.sta.ssid),
                "%s", config.ssid.c_str());
  std::snprintf(reinterpret_cast<char *>(wifi.sta.password),
                sizeof(wifi.sta.password), "%s", config.password.c_str());
  wifi.sta.threshold.authmode =
      config.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
  if (err == ESP_OK) err = esp_wifi_start();
  if (err != ESP_OK) return err;

  return wait_for_station_connection(timeout_ms);
}

esp_err_t wait_for_station_connection(int timeout_ms) {
  if (g_events == nullptr) return ESP_ERR_INVALID_STATE;
  const EventBits_t bits = xEventGroupWaitBits(
      g_events, kConnectedBit, pdFALSE, pdFALSE,
      timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
  return (bits & kConnectedBit) ? ESP_OK : ESP_ERR_TIMEOUT;
}

}  // namespace stagecore
