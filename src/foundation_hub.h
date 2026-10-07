#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"
#include "foundation_identity.h"

namespace stagecore_camera::foundation {

struct VerifiedHub {
  std::string hub_id;
  std::string display_name;
  std::string fingerprint;
  std::string tls_sha256;
  std::string address;
  uint16_t port = 0;
  std::vector<unsigned char> certificate_der;
};

struct RuntimeCredential {
  std::string session_id;
  std::string token;
};

esp_err_t DiscoverAndVerifyHub(VerifiedHub *hub);

esp_err_t EnsurePairedAndAuthenticate(const VerifiedHub &hub,
                                      DeviceIdentity *identity,
                                      const std::string &display_name,
                                      RuntimeCredential *credential);

}  // namespace stagecore_camera::foundation
