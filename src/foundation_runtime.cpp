#include "foundation_runtime.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.2.0-dev.1"
#endif

namespace stagecore_camera::foundation {
namespace {

constexpr char kTag[] = "cam-found-v2";
constexpr char kProtocolVersion[] = "stagecore.device/2";
constexpr char kProfileID[] = "stagecore.esp32-camera";
constexpr size_t kMaxInboundBytes = 8192;
constexpr int kConnectTimeoutMs = 5000;
constexpr int kObservationPeriodMs = 10000;

constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kAssignmentBit = BIT1;
constexpr EventBits_t kDisconnectedBit = BIT2;
constexpr EventBits_t kProtocolErrorBit = BIT3;

struct RuntimeContext {
  EventGroupHandle_t events = nullptr;
  std::string device_id;
  std::string inbound;
  int expected_payload = 0;
  bool assignment_received = false;
  int64_t connection_generation = 0;
};

bool exact_positive_integer(const cJSON *root, const char *key, int64_t *out) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(value) || out == nullptr ||
      !std::isfinite(value->valuedouble) ||
      value->valuedouble < 1.0 ||
      value->valuedouble > 9007199254740991.0 ||
      std::floor(value->valuedouble) != value->valuedouble) {
    return false;
  }
  *out = static_cast<int64_t>(value->valuedouble);
  return true;
}

std::string print_json(cJSON *root) {
  if (root == nullptr) return {};
  char *text = cJSON_PrintUnformatted(root);
  std::string out = text != nullptr ? text : "";
  if (text != nullptr) cJSON_free(text);
  return out;
}

cJSON *capabilities_json() {
  cJSON *caps = cJSON_CreateArray();
  if (caps == nullptr) return nullptr;
  cJSON_AddItemToArray(
      caps, cJSON_CreateString("camera.foundation.inventory"));
  return caps;
}

cJSON *observed_state_json() {
  cJSON *state = cJSON_CreateObject();
  if (state == nullptr) return nullptr;
  cJSON_AddNumberToObject(state, "schema_version", 1);
  cJSON_AddStringToObject(state, "firmware_version", STAGECORE_FW_VERSION);
  cJSON_AddBoolToObject(state, "foundation_candidate", true);
  cJSON_AddBoolToObject(state, "camera_services_enabled", false);
  cJSON_AddBoolToObject(state, "flash_safe_off", true);
  cJSON_AddNumberToObject(
      state, "uptime_seconds",
      static_cast<double>(esp_timer_get_time() / 1000000LL));
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
                             const DeviceIdentity &identity) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "type", "device.observation");
  cJSON_AddNumberToObject(root, "schema_version", 2);
  cJSON_AddStringToObject(root, "device_id", identity.device_id().c_str());
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

bool parse_assignment(RuntimeContext *context, const std::string &text) {
  if (context == nullptr || context->assignment_received) return false;

  cJSON *root = cJSON_ParseWithLength(text.data(), text.size());
  if (root == nullptr) return false;

  const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
  const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  const cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device_id");
  const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
  const cJSON *commands =
      cJSON_GetObjectItemCaseSensitive(root, "commands_enabled");
  const cJSON *blackout =
      cJSON_GetObjectItemCaseSensitive(root, "blackout_required");

  int64_t epoch = 0;
  int64_t generation = 0;
  const bool ok =
      cJSON_IsString(type) && type->valuestring != nullptr &&
      std::strcmp(type->valuestring, "assignment.state") == 0 &&
      cJSON_IsNumber(schema) && schema->valueint == 2 &&
      cJSON_IsString(device) && device->valuestring != nullptr &&
      context->device_id == device->valuestring &&
      cJSON_IsString(state) && state->valuestring != nullptr &&
      std::strcmp(state->valuestring, "UNASSIGNED") == 0 &&
      cJSON_IsFalse(commands) &&
      cJSON_IsTrue(blackout) &&
      exact_positive_integer(root, "assignment_epoch", &epoch) &&
      exact_positive_integer(root, "connection_generation", &generation);

  if (ok) {
    context->assignment_received = true;
    context->connection_generation = generation;
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
        context->inbound.append(
            data->data_ptr, static_cast<size_t>(data->data_len));
      }
      if (!data->fin ||
          static_cast<int>(context->inbound.size()) !=
              context->expected_payload) {
        break;
      }
      if (!parse_assignment(context, context->inbound)) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
      } else {
        xEventGroupSetBits(context->events, kAssignmentBit);
      }
      context->inbound.clear();
      context->expected_payload = 0;
      break;
    }
    default:
      break;
  }
}

esp_err_t send_text(esp_websocket_client_handle_t client,
                    const std::string &text) {
  if (client == nullptr || text.empty()) return ESP_ERR_INVALID_ARG;
  const int sent = esp_websocket_client_send_text(
      client, text.data(), static_cast<int>(text.size()),
      pdMS_TO_TICKS(5000));
  return sent == static_cast<int>(text.size()) ? ESP_OK : ESP_FAIL;
}

}  // namespace

esp_err_t RunInventoryRuntime(const VerifiedHub &hub,
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

  char uri[192] = {};
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
  config.cert_pem =
      reinterpret_cast<const char *>(hub.certificate_der.data());
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

  esp_websocket_client_handle_t client =
      esp_websocket_client_init(&config);
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
      pdFALSE, pdFALSE, pdMS_TO_TICKS(kConnectTimeoutMs));
  if ((bits & kConnectedBit) == 0) {
    err = (bits & kProtocolErrorBit) ? ESP_ERR_INVALID_RESPONSE
                                     : ESP_ERR_TIMEOUT;
    goto cleanup;
  }

  {
    const std::string hello = make_hello(hub, identity, display_name);
    err = send_text(client, hello);
    if (err != ESP_OK) goto cleanup;
  }

  bits = xEventGroupWaitBits(
      context.events,
      kAssignmentBit | kDisconnectedBit | kProtocolErrorBit,
      pdFALSE, pdFALSE, pdMS_TO_TICKS(kConnectTimeoutMs));
  if ((bits & kAssignmentBit) == 0) {
    err = (bits & kProtocolErrorBit) ? ESP_ERR_INVALID_RESPONSE
                                     : ESP_ERR_TIMEOUT;
    goto cleanup;
  }

  ESP_LOGI(kTag,
           "authenticated inventory-only v2 socket accepted; "
           "camera and flash authority remain disabled");

  {
    const std::string observation = make_observation(hub, identity);
    err = send_text(client, observation);
    if (err != ESP_OK) goto cleanup;
  }

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
      if ((bits & kDisconnectedBit) ||
          !esp_websocket_client_is_connected(client)) {
        err = ESP_ERR_INVALID_STATE;
        break;
      }

      const int64_t now = esp_timer_get_time();
      if (now - last_observation_us >=
          static_cast<int64_t>(kObservationPeriodMs) * 1000LL) {
        const std::string observation = make_observation(hub, identity);
        err = send_text(client, observation);
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

}  // namespace stagecore_camera::foundation
