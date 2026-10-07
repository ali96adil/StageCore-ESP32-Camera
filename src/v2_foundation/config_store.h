#pragma once

#include <string>

#include "esp_err.h"

namespace stagecore_camera_v2 {

struct WifiConfig {
  std::string ssid;
  std::string password;

  bool complete() const;
};

struct HubBinding {
  std::string hub_id;
  std::string fingerprint;
  std::string tls_sha256;

  bool complete() const;
};

esp_err_t load_wifi_config(WifiConfig *config);
esp_err_t load_hub_binding(HubBinding *binding);
esp_err_t save_hub_binding(const HubBinding &binding);

}  // namespace stagecore_camera_v2
