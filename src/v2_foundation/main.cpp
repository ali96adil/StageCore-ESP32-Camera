#include <string>

#include "config_store.h"
#include "device_identity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hub_discovery.h"
#include "hub_security.h"
#include "network_station.h"
#include "nvs_flash.h"
#include "stage_device_runtime.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.2.0-dev.c1"
#endif

namespace {

constexpr char kTag[] = "stagecam-v2-c1";

[[noreturn]] void hold_safe_failure(const char *reason) {
  ESP_LOGE(kTag, "safe failure: %s", reason);
  ESP_LOGE(kTag,
           "C1 contains no camera stream or flash-output execution path");
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

std::string display_name(const std::string &device_id) {
  std::string suffix;
  for (char ch : device_id) {
    if (ch != '-') suffix.push_back(ch);
  }
  if (suffix.size() > 6) suffix = suffix.substr(suffix.size() - 6);
  return "StageCore Camera " + suffix;
}

}  // namespace

extern "C" void app_main(void) {
  // Never auto-erase NVS: it contains the proven stream image's Wi-Fi
  // provisioning and the candidate's persistent device/Hub identities.
  const esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err != ESP_OK) {
    hold_safe_failure("NVS unavailable; refusing destructive auto-recovery");
  }

  ESP_LOGW(kTag,
           "StageCore ESP32 Camera secure Foundation C1 %s: "
           "SOURCE-ONLY candidate; no camera stream/flash execution",
           STAGECORE_FW_VERSION);

  stagecore::DeviceIdentity identity;
  if (identity.LoadOrCreate() != ESP_OK) {
    hold_safe_failure("persistent UUID/P-256 identity unavailable");
  }
  const std::string name = display_name(identity.device_id());
  ESP_LOGI(kTag, "device_id=%s", identity.device_id().c_str());

  stagecore::WifiConfig wifi;
  if (stagecore::load_wifi_config(&wifi) != ESP_OK) {
    hold_safe_failure("existing Camera Wi-Fi configuration unavailable");
  }
  if (!wifi.complete()) {
    hold_safe_failure(
        "no saved Camera Stage LAN credentials; provision with proven stream image first");
  }

  esp_err_t network = stagecore::connect_station(wifi, 30000);
  while (network != ESP_OK) {
    ESP_LOGW(kTag, "Stage LAN unavailable; inventory candidate remains inert");
    network = stagecore::wait_for_station_connection(30000);
  }

  while (true) {
    if (stagecore::wait_for_station_connection(0) != ESP_OK) {
      ESP_LOGW(kTag, "Stage LAN disconnected; waiting for saved network");
      while (stagecore::wait_for_station_connection(30000) != ESP_OK) {
        ESP_LOGW(kTag, "Stage LAN still unavailable");
      }
    }

    stagecore::VerifiedHub hub;
    if (stagecore::discover_and_verify_hub(&hub) != ESP_OK) {
      ESP_LOGW(kTag, "verified StageCore Hub unavailable; retrying");
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }

    stagecore::RuntimeCredential credential;
    if (stagecore::ensure_paired_and_authenticate(
            hub, &identity, name, &credential) != ESP_OK) {
      ESP_LOGW(kTag, "Hub pairing/authentication unavailable; retrying");
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }

    const esp_err_t runtime =
        stagecore::run_camera_foundation_runtime(
            hub, credential, identity, name);
    ESP_LOGW(kTag, "v2 inventory runtime ended: %s",
             esp_err_to_name(runtime));
    credential = stagecore::RuntimeCredential{};
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
