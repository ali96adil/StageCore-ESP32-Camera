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
constexpr char kHubIDKey[] = "hub_id";
constexpr char kHubFingerprintKey[] = "hub_fp";
constexpr char kHubTLSKey[] = "hub_tls";
constexpr char kSetupAPPasswordKey[] = "setup_ap_pass";

esp_err_t read_string(nvs_handle_t handle, const char *key, std::string *value,
                      bool *found = nullptr) {
  if (value == nullptr) return ESP_ERR_INVALID_ARG;
  if (found != nullptr) *found = false;
  size_t length = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    value->clear();
    return ESP_OK;
  }
  if (err != ESP_OK) return err;
  if (found != nullptr) *found = true;
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

esp_err_t erase_key_if_present(nvs_handle_t handle, const char *key) {
  const esp_err_t err = nvs_erase_key(handle, key);
  return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

}  // namespace

bool WifiConfig::complete() const {
  return !ssid.empty() && ssid.size() <= 32 &&
         (password.empty() || (password.size() >= 8 && password.size() <= 63));
}

bool HubBinding::complete() const {
  return hub_id.size() == 36 && !fingerprint.empty() && tls_sha256.size() == 64;
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

esp_err_t load_setup_ap_password(std::string *password) {
  if (password == nullptr) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kV2Namespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    password->clear();
    return ESP_OK;
  }
  if (err != ESP_OK) return err;
  err = read_string(handle, kSetupAPPasswordKey, password);
  nvs_close(handle);
  if (err != ESP_OK) return err;
  if (!password->empty() && (password->size() < 8 || password->size() > 63)) {
    password->clear();
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

esp_err_t save_setup_ap_password(const std::string &password) {
  if (password.size() < 8 || password.size() > 63) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kV2Namespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = write_string(handle, kSetupAPPasswordKey, password);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t clear_setup_ap_password() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kV2Namespace, NVS_READWRITE, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  err = erase_key_if_present(handle, kSetupAPPasswordKey);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t load_hub_binding(HubBinding *binding) {
  if (binding == nullptr) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kV2Namespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *binding = HubBinding{};
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  HubBinding loaded;
  bool id_found = false, fp_found = false, tls_found = false;
  err = read_string(handle, kHubIDKey, &loaded.hub_id, &id_found);
  if (err == ESP_OK) {
    err = read_string(handle, kHubFingerprintKey, &loaded.fingerprint, &fp_found);
  }
  if (err == ESP_OK) {
    err = read_string(handle, kHubTLSKey, &loaded.tls_sha256, &tls_found);
  }
  nvs_close(handle);
  if (err != ESP_OK) return err;

  const int present = static_cast<int>(id_found) +
                      static_cast<int>(fp_found) +
                      static_cast<int>(tls_found);
  if (present == 0) {
    *binding = HubBinding{};
    return ESP_OK;
  }
  if (present != 3 || !loaded.complete()) return ESP_ERR_INVALID_STATE;
  *binding = std::move(loaded);
  return ESP_OK;
}

esp_err_t save_hub_binding(const HubBinding &binding) {
  if (!binding.complete()) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kV2Namespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = write_string(handle, kHubIDKey, binding.hub_id);
  if (err == ESP_OK) err = write_string(handle, kHubFingerprintKey, binding.fingerprint);
  if (err == ESP_OK) err = write_string(handle, kHubTLSKey, binding.tls_sha256);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

}  // namespace stagecore
