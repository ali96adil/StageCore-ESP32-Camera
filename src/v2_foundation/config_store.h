#pragma once

#include <string>

#include "esp_err.h"
#include "foundation_store.h"

namespace stagecore {

struct WifiConfig {
  std::string ssid;
  std::string password;

  bool complete() const;
};

FoundationStore &foundation_store();

esp_err_t load_wifi_config(WifiConfig *config);
esp_err_t save_wifi_config(const WifiConfig &config);
esp_err_t load_setup_ap_password(std::string *password);
esp_err_t save_setup_ap_password(const std::string &password);
esp_err_t clear_setup_ap_password();
esp_err_t load_hub_binding(HubBinding *binding);
esp_err_t save_hub_binding(const HubBinding &binding);

}  // namespace stagecore
