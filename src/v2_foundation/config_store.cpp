#include "config_store.h"

#include <utility>
#include <vector>

#include "nvs.h"

namespace stagecore {
namespace {

constexpr char kCameraNamespace[] = "stagecore-cam";
constexpr char kV2Namespace[] = "stagecore_camv2";
constexpr char kSSIDKey[] = "ssid";
constexpr char kPasswordKey[] = "password";

esp_err_t read_string(nvs_handle_t handle, const char *key, std::string *value) {
  if (value == nullptr) return ESP_ERR_INVALID_ARG;
  size_t length = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    value->clear();
    return ESP_OK;
  }
  if (err != ESP_OK) return err;
  std::vector<char> buffer(length == 0 ? 1 : length);
  if (length == 0) {
    value->clear();
    return ESP_OK;
  }
  err = nvs_get_str(handle, key, buffer.data(), &length);
  if (err == ESP_OK) *value = buffer.data();
  return err;
}

esp_err_t write_string(nvs_handle_t handle, const char *key,
                       const std::string &value) {
  return nvs_set_str(handle, key, value.c_str());
}

}  // namespace

FoundationStore &foundation_store() {
  static FoundationStore store(kV2Namespace);
  return store;
}

bool WifiConfig::complete() const {
  return !ssid.empty() && ssid.size() <= 32 &&
         (password.empty() || (password.size() >= 8 && password.size() <= 63));
}

esp_err_t load_wifi_config(WifiConfig *config) {
  if (config == nullptr) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kCameraNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *config = WifiConfig{};
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  WifiConfig loaded;
  err = read_string(handle, kSSIDKey, &loaded.ssid);
  if (err == ESP_OK) err = read_string(handle, kPasswordKey, &loaded.password);
  nvs_close(handle);
  if (err == ESP_OK) *config = std::move(loaded);
  return err;
}

esp_err_t save_wifi_config(const WifiConfig &config) {
  if (!config.complete()) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kCameraNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = write_string(handle, kSSIDKey, config.ssid);
  if (err == ESP_OK) err = write_string(handle, kPasswordKey, config.password);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t load_setup_ap_password(std::string *password) {
  return foundation_store().LoadSetupAPPasswordOverride(password);
}

esp_err_t save_setup_ap_password(const std::string &password) {
  return foundation_store().SaveSetupAPPasswordOverride(password);
}

esp_err_t clear_setup_ap_password() {
  return foundation_store().ResetSetupAPPasswordToDefault();
}

esp_err_t load_hub_binding(HubBinding *binding) {
  return foundation_store().LoadHubBinding(binding);
}

esp_err_t save_hub_binding(const HubBinding &binding) {
  return foundation_store().SaveHubBinding(binding);
}

}  // namespace stagecore
