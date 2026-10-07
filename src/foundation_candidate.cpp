#ifdef STAGECORE_FOUNDATION_V2_CANDIDATE
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "foundation_hub.h"
#include "foundation_identity.h"
#include "foundation_runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
using stagecore_camera::foundation::DeviceIdentity;
using stagecore_camera::foundation::RuntimeCredential;
using stagecore_camera::foundation::VerifiedHub;

constexpr char kTag[] = "cam-found-main";
constexpr char kCameraConfigNamespace[] = "stagecore-cam";
constexpr char kSSIDKey[] = "ssid";
constexpr char kPasswordKey[] = "password";
constexpr gpio_num_t kFlashGPIO = GPIO_NUM_4;
constexpr EventBits_t kConnectedBit = BIT0;

EventGroupHandle_t g_wifi_events = nullptr;

esp_err_t read_string(nvs_handle_t handle, const char *key,
                      std::string *value) {
  if (value == nullptr) return ESP_ERR_INVALID_ARG;
  size_t length = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
  if (err != ESP_OK) return err;
  std::vector<char> buffer(length);
  err = nvs_get_str(handle, key, buffer.data(), &length);
  if (err == ESP_OK) *value = buffer.data();
  return err;
}

esp_err_t load_existing_camera_wifi(std::string *ssid,
                                    std::string *password) {
  if (ssid == nullptr || password == nullptr) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kCameraConfigNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) return err;
  err = read_string(handle, kSSIDKey, ssid);
  if (err == ESP_OK) {
    const esp_err_t password_err = read_string(handle, kPasswordKey, password);
    if (password_err == ESP_ERR_NVS_NOT_FOUND) {
      password->clear();
    } else {
      err = password_err;
    }
  }
  nvs_close(handle);
  if (err == ESP_OK && (ssid->empty() || ssid->size() > 32 ||
      (!password->empty() &&
       (password->size() < 8 || password->size() > 63)))) {
    return ESP_ERR_INVALID_STATE;
  }
  return err;
}

void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    (void)esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_wifi_events != nullptr) {
      xEventGroupClearBits(g_wifi_events, kConnectedBit);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    if (g_wifi_events != nullptr) {
      xEventGroupSetBits(g_wifi_events, kConnectedBit);
    }
  }
}

esp_err_t connect_existing_camera_wifi(const std::string &ssid,
                                       const std::string &password) {
  if (ssid.empty()) return ESP_ERR_INVALID_ARG;

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  if (esp_netif_create_default_wifi_sta() == nullptr) return ESP_FAIL;
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init);
  if (err != ESP_OK) return err;

  g_wifi_events = xEventGroupCreate();
  if (g_wifi_events == nullptr) return ESP_ERR_NO_MEM;

  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr));

  wifi_config_t wifi{};
  std::snprintf(reinterpret_cast<char *>(wifi.sta.ssid),
                sizeof(wifi.sta.ssid), "%s", ssid.c_str());
  std::snprintf(reinterpret_cast<char *>(wifi.sta.password),
                sizeof(wifi.sta.password), "%s", password.c_str());
  wifi.sta.threshold.authmode =
      password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
  if (err == ESP_OK) err = esp_wifi_start();
  return err;
}

std::string display_name_for(const std::string &device_id) {
  std::string compact;
  for (char ch : device_id) {
    if (ch != '-') compact.push_back(ch);
  }
  if (compact.size() > 6) compact = compact.substr(compact.size() - 6);
  return compact.empty() ? "StageCore Camera Candidate"
                         : "StageCore Camera " + compact;
}

[[noreturn]] void hold_failure(const char *reason) {
  ESP_LOGE(kTag, "candidate halted safely: %s", reason);
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

}  // namespace

extern "C" void app_main() {
  // The C1 candidate has no camera/flash authority. Hold the known flash LED
  // output low before any network or identity work.
  ESP_ERROR_CHECK(gpio_reset_pin(kFlashGPIO));
  ESP_ERROR_CHECK(gpio_set_direction(kFlashGPIO, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(kFlashGPIO, 0));

  const esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err != ESP_OK) {
    // Never erase persistent identity/credentials automatically in a candidate.
    hold_failure("NVS unavailable");
  }

  std::string ssid;
  std::string password;
  const esp_err_t wifi_config =
      load_existing_camera_wifi(&ssid, &password);
  if (wifi_config != ESP_OK) {
    hold_failure(
        "no valid existing Camera Wi-Fi credentials; use the proven stream "
        "firmware setup portal first");
  }

  DeviceIdentity identity;
  if (identity.LoadOrCreate() != ESP_OK) {
    hold_failure("persistent Stage Device identity unavailable");
  }
  const std::string display_name =
      display_name_for(identity.device_id());

  if (connect_existing_camera_wifi(ssid, password) != ESP_OK) {
    hold_failure("Stage LAN initialization failed");
  }

  while (true) {
    const EventBits_t bits = xEventGroupWaitBits(
        g_wifi_events, kConnectedBit, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));
    if ((bits & kConnectedBit) == 0) {
      ESP_LOGW(kTag, "waiting for Stage LAN");
      continue;
    }

    VerifiedHub hub;
    if (stagecore_camera::foundation::DiscoverAndVerifyHub(&hub) != ESP_OK) {
      ESP_LOGW(kTag, "verified StageCore Hub unavailable");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    RuntimeCredential credential;
    if (stagecore_camera::foundation::EnsurePairedAndAuthenticate(
            hub, &identity, display_name, &credential) != ESP_OK) {
      ESP_LOGW(kTag, "pairing/authentication not ready");
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }

    const esp_err_t runtime =
        stagecore_camera::foundation::RunInventoryRuntime(
            hub, credential, identity, display_name);
    ESP_LOGW(kTag, "inventory runtime ended: %s",
             esp_err_to_name(runtime));
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
#endif  // STAGECORE_FOUNDATION_V2_CANDIDATE
