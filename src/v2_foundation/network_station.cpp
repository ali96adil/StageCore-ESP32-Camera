#include "network_station.h"

#include <cstdio>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifndef STAGECORE_CAMERA_V2_FOUNDATION_C2
#define STAGECORE_CAMERA_V2_FOUNDATION_C2 0
#endif

#if STAGECORE_CAMERA_V2_FOUNDATION_C2
#include "esp_timer.h"
#include "wifi_recovery_policy.h"
#endif

namespace stagecore {
namespace {

constexpr char kTag[] = "stagecam-v2-net";
constexpr EventBits_t kConnectedBit = BIT0;

EventGroupHandle_t g_events = nullptr;
esp_event_handler_instance_t g_wifi_handler = nullptr;
esp_event_handler_instance_t g_ip_handler = nullptr;
bool g_handlers_registered = false;
bool g_wifi_initialized = false;

#if STAGECORE_CAMERA_V2_FOUNDATION_C2
esp_timer_handle_t g_reconnect_timer = nullptr;
uint32_t g_reconnect_delay_ms =
    stagecore_camera::wifi_recovery::kReconnectInitialDelayMs;

void stop_reconnect_timer() {
  if (g_reconnect_timer == nullptr) return;
  const esp_err_t err = esp_timer_stop(g_reconnect_timer);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "unable to stop reconnect timer: %s",
             esp_err_to_name(err));
  }
}

void reconnect_timer_callback(void *) {
  const esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
    ESP_LOGW(kTag, "scheduled Stage LAN reconnect failed: %s",
             esp_err_to_name(err));
  }
}

void schedule_reconnect() {
  if (g_reconnect_timer == nullptr) return;
  stop_reconnect_timer();
  const uint32_t delay_ms = g_reconnect_delay_ms;
  const esp_err_t err = esp_timer_start_once(
      g_reconnect_timer, static_cast<uint64_t>(delay_ms) * 1000ULL);
  if (err == ESP_OK) {
    g_reconnect_delay_ms =
        stagecore_camera::wifi_recovery::next_reconnect_delay(delay_ms);
  } else {
    ESP_LOGW(kTag, "unable to schedule Stage LAN reconnect: %s",
             esp_err_to_name(err));
  }
}
#endif

void event_handler(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
#if STAGECORE_CAMERA_V2_FOUNDATION_C2
    g_reconnect_delay_ms =
        stagecore_camera::wifi_recovery::kReconnectInitialDelayMs;
#endif
    const esp_err_t err = esp_wifi_connect();
#if STAGECORE_CAMERA_V2_FOUNDATION_C2
    if (err != ESP_OK) schedule_reconnect();
#else
    (void)err;
#endif
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_events != nullptr) xEventGroupClearBits(g_events, kConnectedBit);
#if STAGECORE_CAMERA_V2_FOUNDATION_C2
    schedule_reconnect();
#else
    // C1 keeps its already-qualified retry behavior unchanged.
    (void)esp_wifi_connect();
#endif
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
#if STAGECORE_CAMERA_V2_FOUNDATION_C2
    g_reconnect_delay_ms =
        stagecore_camera::wifi_recovery::kReconnectInitialDelayMs;
    stop_reconnect_timer();
#endif
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

#if STAGECORE_CAMERA_V2_FOUNDATION_C2
  if (g_reconnect_timer == nullptr) {
    const esp_timer_create_args_t timer_args{
        .callback = &reconnect_timer_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stagecam-wifi",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&timer_args, &g_reconnect_timer);
    if (err != ESP_OK) return err;
  }
  g_reconnect_delay_ms =
      stagecore_camera::wifi_recovery::kReconnectInitialDelayMs;
#endif

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
