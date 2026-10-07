#pragma once

#include <string>

#include "device_identity.h"
#include "esp_err.h"
#include "hub_discovery.h"
#include "hub_security.h"

namespace stagecore {

esp_err_t run_camera_foundation_runtime(const VerifiedHub &hub,
                                        const RuntimeCredential &credential,
                                        const DeviceIdentity &identity,
                                        const std::string &display_name);

}  // namespace stagecore
