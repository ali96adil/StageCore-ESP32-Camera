#pragma once

#include <string>

#include "esp_err.h"
#include "foundation_hub.h"
#include "foundation_identity.h"

namespace stagecore_camera::foundation {

esp_err_t RunInventoryRuntime(const VerifiedHub &hub,
                              const RuntimeCredential &credential,
                              const DeviceIdentity &identity,
                              const std::string &display_name);

}  // namespace stagecore_camera::foundation
