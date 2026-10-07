#pragma once

#include <string>

#include "esp_err.h"

namespace stagecore {

[[noreturn]] void run_camera_provisioning_portal(
    const std::string &device_id);

esp_err_t run_camera_recovery_portal(
    const std::string &device_id);

}  // namespace stagecore
