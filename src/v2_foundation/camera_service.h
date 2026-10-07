#pragma once

#include "esp_err.h"

namespace stagecore {

esp_err_t initialize_camera_hardware();
esp_err_t start_camera_network_services();
void stop_camera_network_services();

}  // namespace stagecore
