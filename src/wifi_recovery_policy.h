#pragma once

#include <cstdint>

namespace stagecore_camera::wifi_recovery {

constexpr uint32_t kInitialConnectTimeoutMs = 20000;
constexpr uint32_t kReconnectInitialDelayMs = 1000;
constexpr uint32_t kReconnectMaxDelayMs = 15000;
constexpr uint32_t kRecoveryPortalDelayMs = 180000;

inline uint32_t elapsed(uint32_t now, uint32_t then) {
  return now - then;
}

inline bool reconnect_due(uint32_t now, uint32_t last_attempt,
                          uint32_t delay_ms) {
  return elapsed(now, last_attempt) >= delay_ms;
}

inline uint32_t next_reconnect_delay(uint32_t current_ms) {
  if (current_ms < kReconnectInitialDelayMs) return kReconnectInitialDelayMs;
  if (current_ms >= kReconnectMaxDelayMs) return kReconnectMaxDelayMs;
  const uint32_t doubled = current_ms * 2U;
  return doubled > kReconnectMaxDelayMs ? kReconnectMaxDelayMs : doubled;
}

inline bool recovery_portal_due(uint32_t now, uint32_t offline_since,
                                bool portal_active) {
  return !portal_active
      && elapsed(now, offline_since) >= kRecoveryPortalDelayMs;
}

}  // namespace stagecore_camera::wifi_recovery
