// StageCore camera Wi-Fi/MJPEG v0.1.0-dev.1.
// Build only with STAGECORE_STREAM_FIRMWARE. No OTA or StageCore Hub code here.
// A single client consumes JPEG frames on port 81; use a relay for four tablets.
#ifdef STAGECORE_STREAM_FIRMWARE
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <atomic>

namespace {
constexpr uint16_t kControlPort = 80;
constexpr uint16_t kStreamPort = 81;
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kReconnectTimeoutMs = 45000;
constexpr uint32_t kFrameIntervalMs = 80;  // Target ceiling ~12.5 FPS, not guaranteed.
constexpr char kBoundary[] = "stagecoreframe";
constexpr char kFirmwareVersion[] = "0.1.0-dev.1";

String cameraId;
String hostName;
Preferences preferences;
httpd_handle_t controlServer = nullptr;
httpd_handle_t streamServer = nullptr;
std::atomic<bool> streamActive{false};
bool cameraReady = false;
bool provisioning = false;
bool rebootRequested = false;
unsigned long rebootAtMs = 0;
unsigned long disconnectedSinceMs = 0;

// Hardware probe passed with the AI Thinker pin mapping on the owner's board.
camera_config_t makeCameraConfig() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = psramFound() ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  return config;
}

void sendText(httpd_req_t* req, const char* status, const char* mime,
              const char* body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, mime);
  httpd_resp_sendstr(req, body);
}

esp_err_t healthHandler(httpd_req_t* req) {
  if (provisioning || WiFi.status() != WL_CONNECTED) {
    sendText(req, "503 Service Unavailable", "text/plain", "not connected");
    return ESP_OK;
  }
  const char* state = cameraReady ? "ready" : "camera_error";
  char json[512];
  snprintf(json, sizeof(json),
           "{\"schema_version\":0,\"camera_id\":\"%s\","
           "\"firmware_version\":\"%s\",\"state\":\"%s\","
           "\"stream\":{\"path\":\"/api/v0/stream\",\"port\":81,"
           "\"format\":\"mjpeg\",\"width\":%u,\"height\":%u,\"target_fps\":12},"
           "\"wifi\":{\"rssi_dbm\":%ld},\"uptime_s\":%lu,"
           "\"stream_active\":%s}",
           cameraId.c_str(), kFirmwareVersion, state,
           cameraReady ? (psramFound() ? 640U : 320U) : 0U,
           cameraReady ? (psramFound() ? 480U : 240U) : 0U,
           static_cast<long>(WiFi.RSSI()),
           static_cast<unsigned long>(millis() / 1000UL),
           streamActive.load() ? "true" : "false");
  sendText(req, "200 OK", "application/json", json);
  return ESP_OK;
}

esp_err_t streamHandler(httpd_req_t* req) {
  if (provisioning || !cameraReady || WiFi.status() != WL_CONNECTED) {
    sendText(req, "503 Service Unavailable", "text/plain", "camera unavailable");
    return ESP_OK;
  }
  if (streamActive.exchange(true)) {
    sendText(req, "503 Service Unavailable", "text/plain",
             "one upstream stream only; connect through the StageCore relay");
    return ESP_OK;
  }

  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=stagecoreframe");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t err = ESP_OK;
  while (WiFi.status() == WL_CONNECTED && !provisioning && err == ESP_OK) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
      Serial.println("camera capture failed; closing stream");
      err = ESP_FAIL;
      break;
    }
    char header[96];
    const int headerLen = snprintf(header, sizeof(header),
                                   "\r\n--%s\r\nContent-Type: image/jpeg\r\n"
                                   "Content-Length: %u\r\n\r\n",
                                   kBoundary, static_cast<unsigned>(frame->len));
    if (headerLen <= 0 || headerLen >= static_cast<int>(sizeof(header))) {
      err = ESP_FAIL;
    } else {
      err = httpd_resp_send_chunk(req, header, headerLen);
      if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req,
                                   reinterpret_cast<const char*>(frame->buf),
                                   frame->len);
      }
    }
    esp_camera_fb_return(frame);
    if (err == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(kFrameIntervalMs));
    }
  }
  // Always release the single-upstream gate after disconnect or capture error.
  streamActive.store(false);
  return err;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool decodeComponent(const String& encoded, String& decoded) {
  decoded = "";
  decoded.reserve(encoded.length());
  for (size_t i = 0; i < encoded.length(); ++i) {
    const char c = encoded[i];
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%') {
      if (i + 2 >= encoded.length()) return false;
      const int hi = hexValue(encoded[i + 1]);
      const int lo = hexValue(encoded[i + 2]);
      if (hi < 0 || lo < 0) return false;
      const char value = static_cast<char>((hi << 4) | lo);
      if (value == '\0') return false;
      decoded += value;
      i += 2;
    } else {
      decoded += c;
    }
  }
  return true;
}

bool formField(const String& body, const char* key, String& result) {
  int pos = 0;
  while (pos < static_cast<int>(body.length())) {
    const int amp = body.indexOf('&', pos);
    const int end = amp < 0 ? static_cast<int>(body.length()) : amp;
    const int eq = body.indexOf('=', pos);
    if (eq >= pos && eq < end && body.substring(pos, eq) == key) {
      return decodeComponent(body.substring(eq + 1, end), result);
    }
    pos = end + 1;
  }
  return false;
}

esp_err_t setupPageHandler(httpd_req_t* req) {
  if (!provisioning) {
    sendText(req, "404 Not Found", "text/plain", "not found");
    return ESP_OK;
  }
  static const char page[] =
      "<!doctype html><html><head><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>StageCore Camera</title>"
      "<style>body{font:18px system-ui;max-width:420px;margin:32px auto;"
      "padding:0 16px}label,input,button{display:block;width:100%;box-sizing:border-box}"
      "input,button{padding:12px;margin:10px 0 22px;font-size:18px}</style></head>"
      "<body><h1>StageCore Camera</h1><p>Local Wi-Fi setup</p>"
      "<form method=\"post\" action=\"/setup\">"
      "<label for=\"ssid\">Wi-Fi SSID</label>"
      "<input id=\"ssid\" name=\"ssid\" maxlength=\"32\" required>"
      "<label for=\"password\">Wi-Fi password (leave empty for open Wi-Fi)</label>"
      "<input id=\"password\" name=\"password\" type=\"password\" maxlength=\"63\">"
      "<button type=\"submit\">Save and restart</button></form>"
      "<p>Use 2.4 GHz Wi-Fi. This temporary access point is only for setup.</p>"
      "</body></html>";
  sendText(req, "200 OK", "text/html; charset=utf-8", page);
  return ESP_OK;
}

esp_err_t setupSaveHandler(httpd_req_t* req) {
  if (!provisioning) {
    sendText(req, "404 Not Found", "text/plain", "not found");
    return ESP_OK;
  }
  if (req->content_len <= 0 || req->content_len >= 512) {
    sendText(req, "400 Bad Request", "text/plain", "invalid form length");
    return ESP_OK;
  }
  char body[512];
  size_t received = 0;
  int retries = 0;
  while (received < static_cast<size_t>(req->content_len)) {
    const int count = httpd_req_recv(req, body + received,
                                     req->content_len - received);
    if (count == HTTPD_SOCK_ERR_TIMEOUT && ++retries < 3) continue;
    if (count <= 0) {
      sendText(req, "400 Bad Request", "text/plain", "incomplete form");
      return ESP_OK;
    }
    received += static_cast<size_t>(count);
  }
  body[received] = '\0';
  const String input(body);
  String ssid;
  String password;
  if (!formField(input, "ssid", ssid) ||
      !formField(input, "password", password) ||
      ssid.length() == 0 || ssid.length() > 32 ||
      (password.length() != 0 &&
       (password.length() < 8 || password.length() > 63))) {
    sendText(req, "400 Bad Request", "text/plain",
             "SSID must be 1-32 bytes; password must be empty or 8-63 bytes");
    return ESP_OK;
  }
  preferences.begin("stagecore-cam", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  sendText(req, "200 OK", "text/html",
           "<p>Saved. Camera restarting; connect your device to the show Wi-Fi.</p>");
  rebootAtMs = millis();
  rebootRequested = true;
  return ESP_OK;
}

bool startControlServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = kControlPort;
  config.max_uri_handlers = 4;
  config.stack_size = 8192;
  if (httpd_start(&controlServer, &config) != ESP_OK) return false;
  httpd_uri_t health = {};
  health.uri = "/api/v0/health";
  health.method = HTTP_GET;
  health.handler = healthHandler;
  httpd_register_uri_handler(controlServer, &health);
  httpd_uri_t setup = {};
  setup.uri = "/setup";
  setup.method = HTTP_GET;
  setup.handler = setupPageHandler;
  httpd_register_uri_handler(controlServer, &setup);
  httpd_uri_t save = {};
  save.uri = "/setup";
  save.method = HTTP_POST;
  save.handler = setupSaveHandler;
  httpd_register_uri_handler(controlServer, &save);
  return true;
}

bool startStreamServer() {
  // A streaming handler occupies its HTTP worker. Keep health on port 80.
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = kStreamPort;
  config.ctrl_port = 32769;  // Default control server uses 32768.
  config.max_uri_handlers = 1;
  config.stack_size = 8192;
  if (httpd_start(&streamServer, &config) != ESP_OK) return false;
  httpd_uri_t stream = {};
  stream.uri = "/api/v0/stream";
  stream.method = HTTP_GET;
  stream.handler = streamHandler;
  httpd_register_uri_handler(streamServer, &stream);
  return true;
}

void stopServers() {
  if (streamServer != nullptr) {
    httpd_stop(streamServer);
    streamServer = nullptr;
  }
  if (controlServer != nullptr) {
    httpd_stop(controlServer);
    controlServer = nullptr;
  }
}

void startProvisioning() {
  stopServers();
  MDNS.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  char randomPassword[17];
  snprintf(randomPassword, sizeof(randomPassword), "%08lx%08lx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  const String apSsid = "StageCore-CAM-" + cameraId.substring(cameraId.length() - 6);
  provisioning = true;
  const bool apOk = WiFi.softAP(apSsid.c_str(), randomPassword);
  Serial.printf("provisioning_ap=%s\n", apOk ? apSsid.c_str() : "FAILED");
  if (apOk) {
    Serial.printf("provisioning_password=%s\n", randomPassword);
    Serial.println("provisioning_url=http://192.168.4.1/setup");
  }
  Serial.printf("control_server=%s\n", startControlServer() ? "ready" : "FAILED");
}

void startConnected() {
  provisioning = false;
  disconnectedSinceMs = 0;
  MDNS.begin(hostName.c_str());
  MDNS.addService("stagecore-camera", "tcp", kControlPort);
  MDNS.addServiceTxt("stagecore-camera", "tcp", "stream-port", "81");
  Serial.printf("camera_id=%s\n", cameraId.c_str());
  Serial.printf("health=http://%s/api/v0/health\n",
                WiFi.localIP().toString().c_str());
  Serial.printf("stream=http://%s:81/api/v0/stream\n",
                WiFi.localIP().toString().c_str());
  Serial.printf("control_server=%s\n", startControlServer() ? "ready" : "FAILED");
  Serial.printf("stream_server=%s\n", startStreamServer() ? "ready" : "FAILED");
}

void connectOrProvision() {
  preferences.begin("stagecore-cam", true);
  const String ssid = preferences.getString("ssid", "");
  const String password = preferences.getString("password", "");
  preferences.end();
  if (ssid.isEmpty()) {
    startProvisioning();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostName.c_str());
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("wifi_connecting");
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < kConnectTimeoutMs) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    startConnected();
  } else {
    Serial.println("wifi_connect_timeout; entering local setup");
    startProvisioning();
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\nSTAGECORE_CAM_STREAM_v0");
  WiFi.mode(WIFI_STA);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  cameraId = "stagecam-" + mac.substring(mac.length() - 6);
  hostName = cameraId;
  camera_config_t config = makeCameraConfig();
  const esp_err_t err = esp_camera_init(&config);
  cameraReady = (err == ESP_OK);
  Serial.printf("camera_init=%s error=0x%x psram=%s\n",
                cameraReady ? "PASS" : "FAIL",
                static_cast<unsigned>(err),
                psramFound() ? "yes" : "no");
  // Even if capture fails, allow Wi-Fi setup and report camera_error health.
  connectOrProvision();
}

void loop() {
  if (rebootRequested && (millis() - rebootAtMs) > 1500) {
    ESP.restart();
  }
  if (!provisioning) {
    if (WiFi.status() == WL_CONNECTED) {
      disconnectedSinceMs = 0;
    } else {
      if (disconnectedSinceMs == 0) disconnectedSinceMs = millis();
      if (millis() - disconnectedSinceMs > kReconnectTimeoutMs) {
        Serial.println("wifi_lost; entering local setup");
        startProvisioning();
      }
    }
  }
  delay(250);
}
#endif
