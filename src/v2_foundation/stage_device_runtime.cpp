#include "stage_device_runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.2.0-dev.c1"
#endif

namespace stagecore {
namespace {

constexpr char kTag[] = "stagecam-v2-runtime";
constexpr char kProtocolVersion[] = "stagecore.device/2";
constexpr char kProfileID[] = "stagecore.esp32-camera";
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kAssignmentBit = BIT1;
constexpr EventBits_t kDisconnectedBit = BIT2;
constexpr EventBits_t kProtocolErrorBit = BIT3;
constexpr size_t kMaxInboundBytes = 8192;
constexpr int kReadyTimeoutMs = 5000;
constexpr int kObservationPeriodMs = 10000;

struct RuntimeContext {
  EventGroupHandle_t events = nullptr;
  std::string device_id;
  std::string inbound;
  int expected_payload = 0;
  int64_t assignment_epoch = 0;
  int64_t connection_generation = 0;
};

const char *reset_reason_text() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

bool positive_wire_integer(const cJSON *root, const char *key, int64_t *out) {
  if (out == nullptr) return false;
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
      value->valuedouble < 1.0 || value->valuedouble > 9007199254740991.0 ||
      std::floor(value->valuedouble) != value->valuedouble) {
    return false;
  }
  *out = static_cast<int64_t>(value->valuedouble);
  return true;
}

bool absent_or_empty_string(const cJSON *root, const char *key) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  return item == nullptr ||
         (cJSON_IsString(item) && item->valuestring != nullptr &&
          item->valuestring[0] == '\0');
}

std::string print_json(cJSON *root) {
  if (root == nullptr) return {};
  char *text = cJSON_PrintUnformatted(root);
  std::string out = text != nullptr ? text : "";
  if (text != nullptr) cJSON_free(text);
  return out;
}

cJSON *capabilities_json() {
  // C1 intentionally advertises no maintenance, stream, flash, OTA, Cue or
  // show authority. It proves only secure identity + authenticated v2 inventory.
  return cJSON_CreateArray();
}

cJSON *observed_state_json() {
  cJSON *state = cJSON_CreateObject();
  if (state == nullptr) return nullptr;
  cJSON_AddNumberToObject(state, "schema_version", 1);
  cJSON_AddStringToObject(state, "firmware_version", STAGECORE_FW_VERSION);
  cJSON_AddBoolToObject(state, "foundation_candidate", true);
  cJSON_AddBoolToObject(state, "camera_stream_enabled", false);
  cJSON_AddBoolToObject(state, "flash_output_enabled", false);
  cJSON_AddNumberToObject(
      state, "uptime_seconds",
      static_cast<double>(esp_timer_get_time() / 1000000LL));
  cJSON_AddStringToObject(state, "reset_reason", reset_reason_text());

  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    cJSON_AddNumberToObject(state, "wifi_rssi_dbm", ap.rssi);
  }
  return state;
}

cJSON *network_state_json(const VerifiedHub &hub) {
  cJSON *network = cJSON_CreateObject();
  if (network == nullptr) return nullptr;
  cJSON_AddStringToObject(network, "transport", "TLS_WEBSOCKET");
  cJSON_AddStringToObject(network, "protocol", kProtocolVersion);
  cJSON_AddStringToObject(network, "hub_id", hub.hub_id.c_str());
  cJSON_AddBoolToObject(network, "certificate_pinned", true);
  return network;
}

std::string make_hello(const VerifiedHub &hub,
                       const DeviceIdentity &identity,
                       const std::string &display_name) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};

  cJSON_AddStringToObject(root, "type", "device.hello");
  // The Stage Device hello envelope remains schema 1 while protocol_version
  // selects stagecore.device/2 semantics.
  cJSON_AddNumberToObject(root, "schema_version", 1);
  cJSON_AddStringToObject(root, "device_id", identity.device_id().c_str());
  cJSON_AddStringToObject(root, "profile_id", kProfileID);
  cJSON_AddStringToObject(root, "device_kind", "GENERIC");
  cJSON_AddStringToObject(root, "display_name", display_name.c_str());
  cJSON_AddStringToObject(root, "platform", "esp32");
  cJSON_AddStringToObject(root, "architecture", "xtensa");
  cJSON_AddStringToObject(root, "client_version", STAGECORE_FW_VERSION);
  cJSON_AddStringToObject(root, "protocol_version", kProtocolVersion);
  cJSON_AddStringToObject(root, "readiness", "BLOCKER");

  cJSON *caps = capabilities_json();
  cJSON *observed = observed_state_json();
  cJSON *network = network_state_json(hub);
  if (caps == nullptr || observed == nullptr || network == nullptr) {
    if (caps != nullptr) cJSON_Delete(caps);
    if (observed != nullptr) cJSON_Delete(observed);
    if (network != nullptr) cJSON_Delete(network);
    cJSON_Delete(root);
    return {};
  }
  cJSON_AddItemToObject(root, "capabilities", caps);
  cJSON_AddItemToObject(root, "observed_state", observed);
  cJSON_AddItemToObject(root, "network_state", network);

  const std::string out = print_json(root);
  cJSON_Delete(root);
  return out;
}

std::string make_observation(const VerifiedHub &hub,
                             const std::string &device_id) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "type", "device.observation");
  cJSON_AddNumberToObject(root, "schema_version", 2);
  cJSON_AddStringToObject(root, "device_id", device_id.c_str());
  cJSON_AddStringToObject(root, "readiness", "BLOCKER");

  cJSON *observed = observed_state_json();
  cJSON *network = network_state_json(hub);
  if (observed == nullptr || network == nullptr) {
    if (observed != nullptr) cJSON_Delete(observed);
    if (network != nullptr) cJSON_Delete(network);
    cJSON_Delete(root);
    return {};
  }
  cJSON_AddItemToObject(root, "observed_state", observed);
  cJSON_AddItemToObject(root, "network_state", network);
  const std::string out = print_json(root);
  cJSON_Delete(root);
  return out;
}

bool handle_complete_text(RuntimeContext *context, const std::string &text) {
  if (context == nullptr) return false;
  cJSON *root = cJSON_ParseWithLength(text.data(), text.size());
  if (root == nullptr) return false;

  const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
  const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  const cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device_id");
  bool ok = cJSON_IsString(type) && type->valuestring != nullptr &&
            cJSON_IsNumber(schema) && schema->valuedouble == 2.0 &&
            cJSON_IsString(device) && device->valuestring != nullptr &&
            context->device_id == device->valuestring;

  // C1 is deliberately inventory-only. Only an UNASSIGNED Hub-owned v2
  // assignment is accepted. Any output/command/maintenance frame fails closed.
  if (ok && std::strcmp(type->valuestring, "assignment.state") == 0) {
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    const cJSON *commands =
        cJSON_GetObjectItemCaseSensitive(root, "commands_enabled");
    const cJSON *blackout =
        cJSON_GetObjectItemCaseSensitive(root, "blackout_required");
    int64_t epoch = 0;
    int64_t generation = 0;
    ok = cJSON_IsString(state) && state->valuestring != nullptr &&
         std::strcmp(state->valuestring, "UNASSIGNED") == 0 &&
         absent_or_empty_string(root, "project_id") &&
         positive_wire_integer(root, "assignment_epoch", &epoch) &&
         positive_wire_integer(root, "connection_generation", &generation) &&
         cJSON_IsFalse(commands) && cJSON_IsTrue(blackout) &&
         context->assignment_epoch == 0;
    if (ok) {
      context->assignment_epoch = epoch;
      context->connection_generation = generation;
      xEventGroupSetBits(context->events, kAssignmentBit);
    }
  } else {
    ok = false;
  }

  cJSON_Delete(root);
  return ok;
}

void runtime_event_handler(void *arg, esp_event_base_t,
                           int32_t event_id, void *event_data) {
  auto *context = static_cast<RuntimeContext *>(arg);
  if (context == nullptr || context->events == nullptr) return;

  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      xEventGroupSetBits(context->events, kConnectedBit);
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
      xEventGroupSetBits(context->events, kDisconnectedBit);
      break;
    case WEBSOCKET_EVENT_DATA: {
      auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
      if (data == nullptr) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
        break;
      }
      if (data->op_code != 0x1 && data->op_code != 0x0) break;
      if (data->payload_len <= 0 ||
          static_cast<size_t>(data->payload_len) > kMaxInboundBytes ||
          data->payload_offset < 0 || data->data_len < 0) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
        break;
      }
      if (data->payload_offset == 0) {
        context->inbound.clear();
        context->expected_payload = data->payload_len;
        context->inbound.reserve(static_cast<size_t>(data->payload_len));
      }
      if (context->expected_payload != data->payload_len ||
          static_cast<int>(context->inbound.size()) != data->payload_offset ||
          context->inbound.size() + static_cast<size_t>(data->data_len) >
              kMaxInboundBytes) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
        break;
      }
      if (data->data_ptr != nullptr && data->data_len > 0) {
        context->inbound.append(data->data_ptr,
                                static_cast<size_t>(data->data_len));
      }
      const bool complete =
          data->fin &&
          static_cast<int>(context->inbound.size()) == context->expected_payload;
      if (complete && !handle_complete_text(context, context->inbound)) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
      }
      break;
    }
    default:
      break;
  }
}

esp_err_t send_text(esp_websocket_client_handle_t client,
                    const std::string &message) {
  if (client == nullptr || message.empty()) return ESP_ERR_INVALID_ARG;
  const int sent = esp_websocket_client_send_text(
      client, message.data(), static_cast<int>(message.size()),
      pdMS_TO_TICKS(3000));
  return sent == static_cast<int>(message.size()) ? ESP_OK : ESP_FAIL;
}

}  // namespace

esp_err_t run_camera_foundation_runtime(const VerifiedHub &hub,
                                        const RuntimeCredential &credential,
                                        const DeviceIdentity &identity,
                                        const std::string &display_name) {
  if (hub.certificate_der.empty() || hub.address.empty() || hub.port == 0 ||
      credential.token.empty() || identity.device_id().empty() ||
      display_name.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  RuntimeContext context;
  context.events = xEventGroupCreate();
  context.device_id = identity.device_id();
  if (context.events == nullptr) return ESP_ERR_NO_MEM;

  char uri[192];
  std::snprintf(uri, sizeof(uri),
                "wss://%s:%u/api/v1/stage-devices/runtime",
                hub.address.c_str(), hub.port);
  const std::string headers =
      "Authorization: StageCoreSession " + credential.token + "\r\n";

  esp_websocket_client_config_t config = {};
  config.uri = uri;
  config.disable_auto_reconnect = true;
  config.user_context = &context;
  config.buffer_size = 4096;
  config.cert_pem = reinterpret_cast<const char *>(hub.certificate_der.data());
  config.cert_len = hub.certificate_der.size();
  config.subprotocol = kProtocolVersion;
  config.headers = headers.c_str();
  config.skip_cert_common_name_check = true;
  config.network_timeout_ms = 5000;
  config.ping_interval_sec = 10;
  config.pingpong_timeout_sec = 20;
  config.keep_alive_enable = true;
  config.keep_alive_idle = 10;
  config.keep_alive_interval = 5;
  config.keep_alive_count = 3;

  esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
  if (client == nullptr) {
    vEventGroupDelete(context.events);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = esp_websocket_register_events(
      client, WEBSOCKET_EVENT_ANY, &runtime_event_handler, &context);
  if (err != ESP_OK) {
    esp_websocket_client_destroy(client);
    vEventGroupDelete(context.events);
    return err;
  }

  err = esp_websocket_client_start(client);
  if (err != ESP_OK) {
    esp_websocket_client_destroy(client);
    vEventGroupDelete(context.events);
    return err;
  }

  EventBits_t bits = xEventGroupWaitBits(
      context.events,
      kConnectedBit | kDisconnectedBit | kProtocolErrorBit,
      pdFALSE, pdFALSE, pdMS_TO_TICKS(kReadyTimeoutMs));
  if ((bits & kConnectedBit) == 0) {
    err = (bits & kProtocolErrorBit) ? ESP_ERR_INVALID_RESPONSE
                                     : ESP_ERR_TIMEOUT;
    goto cleanup;
  }

  err = send_text(client, make_hello(hub, identity, display_name));
  if (err != ESP_OK) goto cleanup;

  bits = xEventGroupWaitBits(
      context.events,
      kAssignmentBit | kDisconnectedBit | kProtocolErrorBit,
      pdFALSE, pdFALSE, pdMS_TO_TICKS(kReadyTimeoutMs));
  if ((bits & kAssignmentBit) == 0) {
    err = (bits & kProtocolErrorBit) ? ESP_ERR_INVALID_RESPONSE
                                     : ESP_ERR_TIMEOUT;
    goto cleanup;
  }

  ESP_LOGI(kTag,
           "authenticated v2 inventory-only connection established; "
           "epoch=%lld generation=%lld",
           static_cast<long long>(context.assignment_epoch),
           static_cast<long long>(context.connection_generation));

  err = send_text(client, make_observation(hub, identity.device_id()));
  if (err != ESP_OK) goto cleanup;

  {
    int64_t last_observation_us = esp_timer_get_time();
    while (true) {
      bits = xEventGroupWaitBits(
          context.events, kDisconnectedBit | kProtocolErrorBit,
          pdFALSE, pdFALSE, pdMS_TO_TICKS(250));
      if (bits & kProtocolErrorBit) {
        err = ESP_ERR_INVALID_RESPONSE;
        break;
      }
      if (bits & kDisconnectedBit) {
        err = ESP_ERR_INVALID_STATE;
        break;
      }
      const int64_t now = esp_timer_get_time();
      if (now - last_observation_us >=
          static_cast<int64_t>(kObservationPeriodMs) * 1000LL) {
        err = send_text(client, make_observation(hub, identity.device_id()));
        if (err != ESP_OK) break;
        last_observation_us = now;
      }
    }
  }

cleanup:
  (void)esp_websocket_client_stop(client);
  esp_websocket_client_destroy(client);
  vEventGroupDelete(context.events);
  return err;
}

}  // namespace stagecore
