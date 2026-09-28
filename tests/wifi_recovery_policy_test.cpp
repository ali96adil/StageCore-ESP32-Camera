#include "wifi_recovery_policy.h"

#include <cassert>
#include <cstdint>

using namespace stagecore_camera::wifi_recovery;

int main() {
  assert(!reconnect_due(999, 0, 1000));
  assert(reconnect_due(1000, 0, 1000));

  assert(next_reconnect_delay(0) == 1000);
  assert(next_reconnect_delay(1000) == 2000);
  assert(next_reconnect_delay(8000) == 15000);
  assert(next_reconnect_delay(15000) == 15000);

  assert(!recovery_portal_due(kRecoveryPortalDelayMs - 1, 0, false));
  assert(recovery_portal_due(kRecoveryPortalDelayMs, 0, false));
  assert(!recovery_portal_due(kRecoveryPortalDelayMs + 1, 0, true));

  // uint32_t subtraction keeps retry timing valid across millis() wrap.
  const uint32_t before_wrap = 0xfffffff0U;
  const uint32_t after_wrap = 0x00000020U;
  assert(elapsed(after_wrap, before_wrap) == 48U);
  assert(reconnect_due(after_wrap, before_wrap, 40U));

  return 0;
}
