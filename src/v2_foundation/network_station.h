#pragma once

#include "config_store.h"
#include "esp_err.h"

namespace stagecore_camera_v2 {

esp_err_t connect_station(const WifiConfig &config, int timeout_ms);
esp_err_t wait_for_station_connection(int timeout_ms);

}  // namespace stagecore_camera_v2
