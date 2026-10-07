#include "foundation_hub.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <vector>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mdns.h"
#include "nvs.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.2.0-dev.1"
#endif

namespace stagecore_camera::foundation {
namespace {

constexpr char kTag[] = "cam-found-hub";
constexpr char kService[] = "_stagecore-hub";
constexpr char kProto[] = "_tcp";
constexpr uint32_t kDiscoveryTimeoutMs = 2500;
constexpr size_t kMaxDiscoveryResults = 8;
constexpr int kHttpTimeoutMs = 5000;
constexpr int kPairingPollMs = 1000;
constexpr int kPairingPollLimit = 300;
constexpr char kTrustNamespace[] = "stagecam_v2";
constexpr char kHubIDKey[] = "hub_id";
constexpr char kHubFPKey[] = "hub_fp";
constexpr char kHubTLSKey[] = "hub_tls";

struct Candidate {
  std::string hub_id;
  std::string display_name;
  std::string fingerprint;
  std::string tls_sha256;
  std::string address;
  uint16_t port = 0;
};

struct HubBinding {
  std::string hub_id;
  std::string fingerprint;
  std::string tls_sha256;

  bool complete() const {
    return hub_id.size() == 36 && !fingerprint.empty() &&
           tls_sha256.size() == 64;
  }
};

struct HttpResponse {
  int status = 0;
  std::string body;
};

std::string lower_ascii(std::string value) {
  for (char &ch : value) {
    ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool valid_uuid_shape(const std::string &value) {
  if (value.size() != 36) return false;
  for (size_t i = 0; i < value.size(); ++i) {
    const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash) {
      if (value[i] != '-') return false;
    } else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
      return false;
    }
  }
  return true;
}

bool valid_hex_sha256(const std::string &value) {
  if (value.size() != 64) return false;
  for (char ch : value) {
    if (!((ch >= '0' && ch <= '9') ||
          (ch >= 'a' && ch <= 'f'))) {
      return false;
    }
  }
  return true;
}

esp_err_t read_string(nvs_handle_t handle, const char *key,
                      std::string *value, bool *found = nullptr) {
  if (value == nullptr) return ESP_ERR_INVALID_ARG;
  if (found != nullptr) *found = false;

  size_t length = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    value->clear();
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  std::vector<char> buffer(length);
  err = nvs_get_str(handle, key, buffer.data(), &length);
  if (err == ESP_OK) {
    *value = buffer.data();
    if (found != nullptr) *found = true;
  }
  return err;
}

esp_err_t load_binding(HubBinding *binding) {
  if (binding == nullptr) return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kTrustNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *binding = HubBinding{};
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  HubBinding loaded;
  bool id_found = false;
  bool fp_found = false;
  bool tls_found = false;
  err = read_string(handle, kHubIDKey, &loaded.hub_id, &id_found);
  if (err == ESP_OK) err = read_string(handle, kHubFPKey, &loaded.fingerprint, &fp_found);
  if (err == ESP_OK) err = read_string(handle, kHubTLSKey, &loaded.tls_sha256, &tls_found);
  nvs_close(handle);
  if (err != ESP_OK) return err;

  const int present = static_cast<int>(id_found) +
                      static_cast<int>(fp_found) +
                      static_cast<int>(tls_found);
  if (present == 0) {
    *binding = HubBinding{};
    return ESP_OK;
  }
  if (present != 3 || !loaded.complete()) return ESP_ERR_INVALID_STATE;

  loaded.hub_id = lower_ascii(loaded.hub_id);
  loaded.tls_sha256 = lower_ascii(loaded.tls_sha256);
  *binding = std::move(loaded);
  return ESP_OK;
}

esp_err_t save_binding(const HubBinding &binding) {
  if (!binding.complete()) return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kTrustNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_str(handle, kHubIDKey, binding.hub_id.c_str());
  if (err == ESP_OK) err = nvs_set_str(handle, kHubFPKey, binding.fingerprint.c_str());
  if (err == ESP_OK) err = nvs_set_str(handle, kHubTLSKey, binding.tls_sha256.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

std::string txt_value(const mdns_result_t *result, const char *key) {
  if (result == nullptr || key == nullptr) return {};
  for (size_t i = 0; i < result->txt_count; ++i) {
    if (result->txt[i].key == nullptr ||
        std::strcmp(result->txt[i].key, key) != 0 ||
        result->txt[i].value == nullptr) {
      continue;
    }
    if (result->txt_value_len != nullptr) {
      return std::string(result->txt[i].value, result->txt_value_len[i]);
    }
    return result->txt[i].value;
  }
  return {};
}

bool extract_ipv4(const mdns_result_t *result, std::string *address) {
  if (result == nullptr || address == nullptr) return false;
  for (const mdns_ip_addr_t *item = result->addr;
       item != nullptr; item = item->next) {
    if (!IP_IS_V4(&item->addr)) continue;
    char text[IPADDR_STRLEN_MAX] = {};
    if (esp_ip4addr_ntoa(&item->addr.u_addr.ip4, text, sizeof(text)) != nullptr) {
      *address = text;
      return true;
    }
  }
  return false;
}

bool parse_candidate(const mdns_result_t *result, Candidate *candidate) {
  if (result == nullptr || candidate == nullptr || result->port == 0) {
    return false;
  }

  Candidate parsed;
  parsed.hub_id = lower_ascii(txt_value(result, "hub_id"));
  parsed.display_name = txt_value(result, "name");
  parsed.fingerprint = txt_value(result, "hub_fp");
  parsed.tls_sha256 = lower_ascii(txt_value(result, "tls_sha256"));

  const std::string version = txt_value(result, "v");
  const std::string port_text = txt_value(result, "port");
  const std::string api_path = txt_value(result, "api_path");
  const std::string runtime_path = txt_value(result, "runtime_path");

  char *end = nullptr;
  const long advertised_port = std::strtol(port_text.c_str(), &end, 10);
  if (version != "1" ||
      !valid_uuid_shape(parsed.hub_id) ||
      parsed.display_name.empty() || parsed.display_name.size() > 96 ||
      parsed.fingerprint.empty() || parsed.fingerprint.size() > 200 ||
      !valid_hex_sha256(parsed.tls_sha256) ||
      port_text.empty() || end == nullptr || *end != '\0' ||
      advertised_port <= 0 || advertised_port > 65535 ||
      static_cast<uint16_t>(advertised_port) != result->port ||
      api_path != "/" ||
      runtime_path != "/api/v1/companion/runtime" ||
      !extract_ipv4(result, &parsed.address)) {
    return false;
  }

  parsed.port = result->port;
  *candidate = std::move(parsed);
  return true;
}

bool binding_matches(const HubBinding &binding, const Candidate &candidate) {
  return binding.complete() &&
         binding.hub_id == candidate.hub_id &&
         binding.fingerprint == candidate.fingerprint &&
         binding.tls_sha256 == candidate.tls_sha256;
}

std::string hex_digest(const unsigned char digest[32]) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(64, '0');
  for (size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[(digest[i] >> 4) & 0x0f];
    out[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  return out;
}

esp_err_t capture_pinned_certificate(const Candidate &candidate,
                                     std::vector<unsigned char> *der) {
  if (der == nullptr) return ESP_ERR_INVALID_ARG;

  esp_tls_t *tls = esp_tls_init();
  if (tls == nullptr) return ESP_ERR_NO_MEM;

  esp_tls_cfg_t cfg = {};
  cfg.tls_version = ESP_TLS_VER_TLS_1_3;
  const int connected = esp_tls_conn_new_sync(
      candidate.address.c_str(), static_cast<int>(candidate.address.size()),
      candidate.port, &cfg, tls);
  if (connected != 1) {
    esp_tls_conn_destroy(tls);
    return ESP_FAIL;
  }

  auto *ssl = static_cast<mbedtls_ssl_context *>(esp_tls_get_ssl_context(tls));
  const mbedtls_x509_crt *peer =
      ssl != nullptr ? mbedtls_ssl_get_peer_cert(ssl) : nullptr;
  if (peer == nullptr || peer->raw.p == nullptr || peer->raw.len == 0) {
    esp_tls_conn_destroy(tls);
    return ESP_FAIL;
  }

  unsigned char digest[32] = {};
  if (mbedtls_sha256(peer->raw.p, peer->raw.len, digest, 0) != 0) {
    esp_tls_conn_destroy(tls);
    return ESP_FAIL;
  }
  if (hex_digest(digest) != candidate.tls_sha256) {
    esp_tls_conn_destroy(tls);
    ESP_LOGE(kTag, "discovered Hub TLS pin mismatch");
    return ESP_ERR_INVALID_CRC;
  }

  der->assign(peer->raw.p, peer->raw.p + peer->raw.len);
  esp_tls_conn_destroy(tls);
  return ESP_OK;
}

esp_err_t http_event(esp_http_client_event_t *event) {
  if (event == nullptr || event->user_data == nullptr) return ESP_OK;
  if (event->event_id == HTTP_EVENT_ON_DATA &&
      event->data != nullptr && event->data_len > 0) {
    auto *response = static_cast<HttpResponse *>(event->user_data);
    if (response->body.size() + static_cast<size_t>(event->data_len) > 16384) {
      return ESP_ERR_NO_MEM;
    }
    response->body.append(static_cast<const char *>(event->data),
                          static_cast<size_t>(event->data_len));
  }
  return ESP_OK;
}

std::string endpoint_url(const VerifiedHub &hub, const char *path) {
  char url[192] = {};
  std::snprintf(url, sizeof(url), "https://%s:%u%s",
                hub.address.c_str(), hub.port, path);
  return url;
}

esp_err_t request(const VerifiedHub &hub, esp_http_client_method_t method,
                  const char *path, const std::string *body,
                  HttpResponse *response) {
  if (response == nullptr || hub.certificate_der.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  response->status = 0;
  response->body.clear();
  const std::string url = endpoint_url(hub, path);

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = method;
  config.timeout_ms = kHttpTimeoutMs;
  config.cert_der = reinterpret_cast<const char *>(hub.certificate_der.data());
  config.cert_len = hub.certificate_der.size();
  config.skip_cert_common_name_check = true;
  config.tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_3;
  config.event_handler = http_event;
  config.user_data = response;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return ESP_ERR_NO_MEM;

  esp_err_t err = ESP_OK;
  if (body != nullptr) {
    err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) {
      err = esp_http_client_set_post_field(
          client, body->data(), static_cast<int>(body->size()));
    }
  }
  if (err == ESP_OK) err = esp_http_client_perform(client);
  if (err == ESP_OK) response->status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return err;
}

std::string json_string(const cJSON *root, const char *key) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) return {};
  return item->valuestring;
}

std::string response_error_code(const HttpResponse &response) {
  cJSON *root = cJSON_ParseWithLength(response.body.data(), response.body.size());
  if (root == nullptr) return {};
  const std::string code = json_string(root, "error_code");
  cJSON_Delete(root);
  return code;
}

std::string print_json(cJSON *root) {
  if (root == nullptr) return {};
  char *text = cJSON_PrintUnformatted(root);
  std::string out = text != nullptr ? text : "";
  if (text != nullptr) cJSON_free(text);
  return out;
}

esp_err_t verify_public_identity(const Candidate &candidate,
                                 const std::vector<unsigned char> &certificate,
                                 std::string *verified_name) {
  VerifiedHub temporary;
  temporary.address = candidate.address;
  temporary.port = candidate.port;
  temporary.certificate_der = certificate;

  HttpResponse response;
  const esp_err_t err = request(
      temporary, HTTP_METHOD_GET, "/api/v1/hub/identity", nullptr, &response);
  if (err != ESP_OK || response.status != 200) return ESP_FAIL;

  cJSON *root =
      cJSON_ParseWithLength(response.body.data(), response.body.size());
  if (root == nullptr) return ESP_ERR_INVALID_RESPONSE;

  const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  const std::string hub_id = lower_ascii(json_string(root, "hub_id"));
  const std::string display_name = json_string(root, "display_name");
  const std::string fingerprint = json_string(root, "fingerprint");
  const bool ok = cJSON_IsNumber(schema) && schema->valueint == 1 &&
                  hub_id == candidate.hub_id &&
                  !display_name.empty() &&
                  fingerprint == candidate.fingerprint;
  if (ok && verified_name != nullptr) *verified_name = display_name;
  cJSON_Delete(root);
  return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

std::string random_nonce_base64() {
  std::array<unsigned char, 32> nonce{};
  esp_fill_random(nonce.data(), nonce.size());

  size_t encoded_length = 0;
  int rc = mbedtls_base64_encode(nullptr, 0, &encoded_length,
                                 nonce.data(), nonce.size());
  if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) return {};

  std::vector<unsigned char> encoded(encoded_length + 1, 0);
  rc = mbedtls_base64_encode(encoded.data(), encoded.size(), &encoded_length,
                             nonce.data(), nonce.size());
  if (rc != 0) return {};
  return std::string(reinterpret_cast<const char *>(encoded.data()),
                     encoded_length);
}

std::string make_hostname(const std::string &device_id) {
  std::string compact;
  for (char ch : device_id) {
    if (ch != '-') compact.push_back(ch);
  }
  if (compact.size() > 8) compact = compact.substr(compact.size() - 8);
  return "stagecore-cam-" + compact;
}

cJSON *pairing_capabilities() {
  cJSON *caps = cJSON_CreateArray();
  if (caps != nullptr) {
    cJSON_AddItemToArray(
        caps, cJSON_CreateString("camera.foundation.inventory"));
  }
  return caps;
}

esp_err_t request_pairing(const VerifiedHub &hub, DeviceIdentity *identity,
                          const std::string &display_name,
                          std::string *request_id,
                          std::string *pairing_code) {
  const std::string nonce = random_nonce_base64();
  if (nonce.empty()) return ESP_FAIL;

  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return ESP_ERR_NO_MEM;
  cJSON_AddStringToObject(root, "companion_id", identity->device_id().c_str());
  cJSON_AddStringToObject(root, "display_name", display_name.c_str());
  const std::string hostname = make_hostname(identity->device_id());
  cJSON_AddStringToObject(root, "hostname", hostname.c_str());
  cJSON_AddStringToObject(root, "platform", "esp32");
  cJSON_AddStringToObject(root, "architecture", "xtensa");
  cJSON_AddStringToObject(root, "version", STAGECORE_FW_VERSION);
  cJSON_AddStringToObject(root, "public_key_algorithm", "P256_X963_SHA256");
  cJSON_AddStringToObject(root, "public_key_base64",
                          identity->public_key_base64().c_str());
  cJSON_AddStringToObject(root, "client_nonce_base64", nonce.c_str());
  cJSON *caps = pairing_capabilities();
  if (caps == nullptr) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  cJSON_AddItemToObject(root, "capabilities", caps);

  const std::string body = print_json(root);
  cJSON_Delete(root);
  if (body.empty()) return ESP_FAIL;

  HttpResponse response;
  esp_err_t err = request(
      hub, HTTP_METHOD_POST, "/api/v1/companion/pairing/requests",
      &body, &response);
  if (err != ESP_OK || response.status != 202) return ESP_FAIL;

  cJSON *reply =
      cJSON_ParseWithLength(response.body.data(), response.body.size());
  if (reply == nullptr) return ESP_ERR_INVALID_RESPONSE;
  *request_id = json_string(reply, "request_id");
  *pairing_code = json_string(reply, "pairing_code");
  cJSON_Delete(reply);
  return (!request_id->empty() && !pairing_code->empty())
             ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t wait_for_pairing(const VerifiedHub &hub,
                           const std::string &request_id,
                           const std::string &pairing_code) {
  for (int attempt = 0; attempt < kPairingPollLimit; ++attempt) {
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "request_id", request_id.c_str());
    cJSON_AddStringToObject(root, "pairing_code", pairing_code.c_str());
    const std::string body = print_json(root);
    cJSON_Delete(root);

    HttpResponse response;
    esp_err_t err = request(
        hub, HTTP_METHOD_POST, "/api/v1/companion/pairing/status",
        &body, &response);
    if (err != ESP_OK || response.status != 200) return ESP_FAIL;

    cJSON *reply =
        cJSON_ParseWithLength(response.body.data(), response.body.size());
    if (reply == nullptr) return ESP_ERR_INVALID_RESPONSE;
    const std::string status = json_string(reply, "status");
    cJSON_Delete(reply);

    if (status == "APPROVED") return ESP_OK;
    if (status == "REJECTED" || status == "EXPIRED") return ESP_FAIL;
    if (status != "PENDING") return ESP_ERR_INVALID_RESPONSE;
    vTaskDelay(pdMS_TO_TICKS(kPairingPollMs));
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t begin_authentication(const VerifiedHub &hub,
                               const std::string &device_id,
                               std::string *challenge_id,
                               std::string *nonce_base64,
                               std::string *server_error) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return ESP_ERR_NO_MEM;
  cJSON_AddStringToObject(root, "companion_id", device_id.c_str());
  const std::string body = print_json(root);
  cJSON_Delete(root);

  HttpResponse response;
  const esp_err_t err = request(
      hub, HTTP_METHOD_POST, "/api/v1/companion/auth/challenges",
      &body, &response);
  if (err != ESP_OK) return err;
  if (response.status != 200) {
    if (server_error != nullptr) *server_error = response_error_code(response);
    return ESP_ERR_INVALID_STATE;
  }

  cJSON *reply =
      cJSON_ParseWithLength(response.body.data(), response.body.size());
  if (reply == nullptr) return ESP_ERR_INVALID_RESPONSE;
  *challenge_id = json_string(reply, "challenge_id");
  *nonce_base64 = json_string(reply, "nonce_base64");
  cJSON_Delete(reply);
  return (!challenge_id->empty() && !nonce_base64->empty())
             ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t complete_authentication(const VerifiedHub &hub,
                                  DeviceIdentity *identity,
                                  const std::string &challenge_id,
                                  const std::string &nonce_base64,
                                  RuntimeCredential *credential) {
  std::string signature;
  esp_err_t err = identity->SignAuthenticationMessage(
      challenge_id, nonce_base64, &signature);
  if (err != ESP_OK) return err;

  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return ESP_ERR_NO_MEM;
  cJSON_AddStringToObject(root, "companion_id", identity->device_id().c_str());
  cJSON_AddStringToObject(root, "challenge_id", challenge_id.c_str());
  cJSON_AddStringToObject(root, "signature_base64", signature.c_str());
  const std::string body = print_json(root);
  cJSON_Delete(root);

  HttpResponse response;
  err = request(
      hub, HTTP_METHOD_POST, "/api/v1/companion/auth/sessions",
      &body, &response);
  if (err != ESP_OK || response.status != 201) return ESP_FAIL;

  cJSON *reply =
      cJSON_ParseWithLength(response.body.data(), response.body.size());
  if (reply == nullptr) return ESP_ERR_INVALID_RESPONSE;
  credential->session_id = json_string(reply, "session_id");
  credential->token = json_string(reply, "session_token");
  cJSON_Delete(reply);
  return (!credential->session_id.empty() && !credential->token.empty())
             ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t authenticate_once(const VerifiedHub &hub,
                            DeviceIdentity *identity,
                            RuntimeCredential *credential,
                            std::string *server_error) {
  std::string challenge_id;
  std::string nonce_base64;
  esp_err_t err = begin_authentication(
      hub, identity->device_id(), &challenge_id, &nonce_base64, server_error);
  if (err != ESP_OK) return err;
  return complete_authentication(
      hub, identity, challenge_id, nonce_base64, credential);
}

}  // namespace

esp_err_t DiscoverAndVerifyHub(VerifiedHub *hub) {
  if (hub == nullptr) return ESP_ERR_INVALID_ARG;

  esp_err_t err = mdns_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  mdns_result_t *results = nullptr;
  err = mdns_query_ptr(
      kService, kProto, kDiscoveryTimeoutMs, kMaxDiscoveryResults, &results);
  if (err != ESP_OK) return err;

  HubBinding remembered;
  err = load_binding(&remembered);
  if (err != ESP_OK) {
    mdns_query_results_free(results);
    return err;
  }

  std::vector<Candidate> candidates;
  for (mdns_result_t *item = results; item != nullptr; item = item->next) {
    Candidate candidate;
    if (!parse_candidate(item, &candidate)) continue;
    if (remembered.complete() && !binding_matches(remembered, candidate)) {
      continue;
    }
    candidates.push_back(std::move(candidate));
  }
  mdns_query_results_free(results);

  if (candidates.size() != 1) {
    ESP_LOGW(kTag, "expected one eligible StageCore Hub; found %u",
             static_cast<unsigned>(candidates.size()));
    return ESP_ERR_INVALID_STATE;
  }

  Candidate candidate = std::move(candidates.front());
  std::vector<unsigned char> certificate;
  err = capture_pinned_certificate(candidate, &certificate);
  if (err != ESP_OK) return err;

  std::string verified_name;
  err = verify_public_identity(candidate, certificate, &verified_name);
  if (err != ESP_OK) return err;

  if (!remembered.complete()) {
    HubBinding binding{
        candidate.hub_id, candidate.fingerprint, candidate.tls_sha256};
    err = save_binding(binding);
    if (err != ESP_OK) return err;
  }

  hub->hub_id = candidate.hub_id;
  hub->display_name = verified_name;
  hub->fingerprint = candidate.fingerprint;
  hub->tls_sha256 = candidate.tls_sha256;
  hub->address = candidate.address;
  hub->port = candidate.port;
  hub->certificate_der = std::move(certificate);
  return ESP_OK;
}

esp_err_t EnsurePairedAndAuthenticate(const VerifiedHub &hub,
                                      DeviceIdentity *identity,
                                      const std::string &display_name,
                                      RuntimeCredential *credential) {
  if (identity == nullptr || credential == nullptr ||
      display_name.empty() || hub.certificate_der.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  std::string server_error;
  esp_err_t err =
      authenticate_once(hub, identity, credential, &server_error);
  if (err == ESP_OK) return ESP_OK;

  if (server_error != "COMPANION_UNPAIRED") return err;

  std::string request_id;
  std::string pairing_code;
  err = request_pairing(
      hub, identity, display_name, &request_id, &pairing_code);
  if (err != ESP_OK) return err;

  ESP_LOGW(kTag, "PAIRING APPROVAL REQUIRED");
  ESP_LOGW(kTag, "StageCore pairing code: %s", pairing_code.c_str());

  err = wait_for_pairing(hub, request_id, pairing_code);
  if (err != ESP_OK) return err;

  credential->session_id.clear();
  credential->token.clear();
  server_error.clear();
  return authenticate_once(hub, identity, credential, &server_error);
}

}  // namespace stagecore_camera::foundation
