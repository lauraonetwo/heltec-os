#include "OtaUpdater.h"
#include "OtaProtocol.h"
#include "OtaTrust.h"
#include "Config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cstring>
#include <ctime>
#include <vector>

#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#error "OTA requires the ESP-IDF trusted certificate bundle."
#endif
#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Use the pinned Heltec core with bootloader rollback enabled."
#endif

// Override Arduino's weak C hook: validation belongs to our later health check.
extern "C" bool verifyRollbackLater() { return true; }

namespace Ota {
namespace {
constexpr char IDENTITY[] = "HELTEC_OS_IMAGE_V1|heltec-wifi-lora-32-v3.2|" HELTEC_OS_VERSION "|" HELTEC_OS_SIGNING_KEY_ID "|";
constexpr char LATEST[] = "https://github.com/lauraonetwo/heltec-os/releases/latest/download/manifest.txt";
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
Snapshot state;
std::atomic<bool> cancelRequested{false};
OtaProtocol::Manifest available;
unsigned long restartAt = 0;
bool healthChecked = false;

void publish(Phase phase, const char *message, unsigned progress = 0) {
  portENTER_CRITICAL(&stateLock);
  state.phase = phase;
  state.progress = progress;
  snprintf(state.message, sizeof(state.message), "%s", message);
  portEXIT_CRITICAL(&stateLock);
}
bool cancelled() { return cancelRequested.load(); }
void finish(Phase phase, const char *message) {
  portENTER_CRITICAL(&stateLock);
  state.phase = phase;
  state.busy = false;
  state.cancellable = false;
  snprintf(state.message, sizeof(state.message), "%s", message);
  portEXIT_CRITICAL(&stateLock);
}
void fail(const char *message) {
  finish(cancelled() ? Phase::Cancelled : Phase::Error, cancelled() ? "Update cancelled" : message);
}
bool networkReady() {
  if (WiFi.status() != WL_CONNECTED) { fail("Connect WiFi first"); return false; }
  auto running = esp_ota_get_running_partition();
  auto next = esp_ota_get_next_update_partition(nullptr);
  if (ESP.getFlashChipSize() != 0x800000 || !running || !next || running->address == next->address ||
      running->size != OtaProtocol::SLOT_SIZE || next->size != OtaProtocol::SLOT_SIZE) {
    fail("Wrong OTA layout"); return false;
  }
  esp_ota_img_states_t bootState;
  unsigned long healthWait = millis();
  while (esp_ota_get_state_partition(running, &bootState) == ESP_OK && bootState == ESP_OTA_IMG_PENDING_VERIFY) {
    publish(Phase::Checking, "Finishing startup");
    if (cancelled() || millis() - healthWait >= 15000) { fail("Startup not confirmed"); return false; }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (time(nullptr) < 1767225600) {
    publish(Phase::Checking, "Setting secure clock");
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    unsigned long start = millis();
    while (time(nullptr) < 1767225600 && millis() - start < 20000 && !cancelled()) vTaskDelay(pdMS_TO_TICKS(100));
    if (time(nullptr) < 1767225600 || cancelled()) { fail("Clock sync failed"); return false; }
  }
  return true;
}

class Request {
 public:
  esp_http_client_handle_t client = nullptr;
  std::string location;
  int status = 0;
  int64_t size = -1;
  ~Request() { close(); }
  void close() {
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); client = nullptr; }
  }
  static esp_err_t event(esp_http_client_event_t *event) {
    auto request = static_cast<Request *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        strcasecmp(event->header_key, "Location") == 0) {
      size_t length = strnlen(event->header_value, 4097);
      request->location = length <= 4096 ? std::string(event->header_value, length) : "";
    }
    return ESP_OK;
  }
  bool open(std::string url) {
    for (int redirect = 0; redirect <= 5 && !cancelled(); ++redirect) {
      if (!OtaProtocol::allowedUrl(url)) return false;
      close();
      location.clear();
      esp_http_client_config_t config{};
      config.url = url.c_str();
      config.crt_bundle_attach = esp_crt_bundle_attach;
      config.timeout_ms = 3000;
      config.disable_auto_redirect = true;
      config.event_handler = event;
      config.user_data = this;
      config.buffer_size = 4096;
      // GitHub asset redirects include long signed queries (URLs are capped at 4096 bytes).
      // The transmit buffer is separate from the receive buffer configured above.
      config.buffer_size_tx = 5120;
      config.user_agent = "HeltecOS/" HELTEC_OS_VERSION;
      client = esp_http_client_init(&config);
      if (!client || esp_http_client_open(client, 0) != ESP_OK) return false;
      size = esp_http_client_fetch_headers(client);
      status = esp_http_client_get_status_code(client);
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        url = location;
        continue;
      }
      return status == 200 && size > 0 && !esp_http_client_is_chunked_response(client);
    }
    return false;
  }
  bool readExact(uint8_t *data, size_t length, unsigned long deadlineStart, unsigned long limit) {
    size_t total = 0;
    unsigned long lastData = millis();
    while (total < length && !cancelled() && WiFi.status() == WL_CONNECTED && millis() - deadlineStart < limit) {
      int count = esp_http_client_read(client, reinterpret_cast<char *>(data + total), length - total);
      if (count > 0) { total += count; lastData = millis(); }
      else if (count == -ESP_ERR_HTTP_EAGAIN) { vTaskDelay(pdMS_TO_TICKS(10)); }
      else return false;
      if (millis() - lastData > 10000) return false;
    }
    return total == length && !cancelled();
  }
};

bool verifySignature(const std::vector<uint8_t> &manifest, const std::vector<uint8_t> &signature) {
  uint8_t digest[32];
  if (signature.size() != OtaProtocol::SIGNATURE_BYTES ||
      mbedtls_sha256(manifest.data(), manifest.size(), digest, 0) != 0) return false;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  int result = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char *>(OtaTrust::PUBLIC_KEY), sizeof(OtaTrust::PUBLIC_KEY));
  if (result == 0 && mbedtls_pk_get_bitlen(&key) == 3072 && mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA)) {
    result = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature.data(), signature.size());
  } else result = -1;
  mbedtls_pk_free(&key);
  return result == 0;
}

void checkRelease() {
  if (!networkReady()) return;
  publish(Phase::Checking, "Checking GitHub...");
  Request request;
  if (!request.open(LATEST)) {
    if (request.status == 404 && !cancelled()) finish(Phase::NoRelease, "No OTA release yet");
    else fail("HTTPS check failed");
    return;
  }
  if (request.size > OtaProtocol::MAX_MANIFEST) { fail("Invalid manifest size"); return; }
  std::vector<uint8_t> manifest(request.size);
  if (!request.readExact(manifest.data(), manifest.size(), millis(), 15000)) { fail("Manifest interrupted"); return; }
  OtaProtocol::Manifest parsed;
  if (!OtaProtocol::parseManifest(std::string_view(reinterpret_cast<const char *>(manifest.data()), manifest.size()), parsed)) {
    fail("Invalid manifest"); return;
  }
  if (!request.open(OtaProtocol::releaseUrl(parsed.version, "manifest.sig")) || request.size != OtaProtocol::SIGNATURE_BYTES) {
    fail("Signature unavailable"); return;
  }
  std::vector<uint8_t> signature(request.size);
  if (!request.readExact(signature.data(), signature.size(), millis(), 15000) || !verifySignature(manifest, signature)) {
    fail("Signature rejected"); return;
  }
  OtaProtocol::Version installed;
  if (!OtaProtocol::parseVersion(Config::VERSION, installed)) { fail("Invalid local version"); return; }
  if (cancelled()) { fail("Cancelled"); return; }
  if (!OtaProtocol::newer(parsed.parsedVersion, installed)) { finish(Phase::Current, "System is up to date"); return; }
  available = parsed;
  portENTER_CRITICAL(&stateLock);
  snprintf(state.version, sizeof(state.version), "%s", parsed.version.c_str());
  portEXIT_CRITICAL(&stateLock);
  finish(Phase::Available, "Signed update ready");
}

void installRelease() {
  if (!networkReady()) return;
  auto partition = esp_ota_get_next_update_partition(nullptr);
  if (available.size > partition->size) { fail("Firmware too large"); return; }
  Request request;
  publish(Phase::Downloading, "Downloading...");
  if (!request.open(available.url) || request.size != static_cast<int64_t>(available.size)) {
    fail("Download unavailable"); return;
  }
  uint8_t buffer[4096];
  unsigned long started = millis();
  if (!request.readExact(buffer, 24, started, 180000) || !OtaProtocol::imageHeaderValid(buffer, 24)) {
    fail("Wrong firmware image"); return;
  }
  esp_ota_handle_t handle;
  if (esp_ota_begin(partition, available.size, &handle) != ESP_OK) { fail("Cannot open OTA slot"); return; }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool good = mbedtls_sha256_starts(&hash, 0) == 0;
  good = good && esp_ota_write(handle, buffer, 24) == ESP_OK && mbedtls_sha256_update(&hash, buffer, 24) == 0;
  size_t received = 24;
  const std::string marker = OtaProtocol::identity(available.version, HELTEC_OS_SIGNING_KEY_ID);
  std::string tail(reinterpret_cast<char *>(buffer), 24);
  bool identityFound = false;
  while (good && received < available.size && !cancelled()) {
    size_t count = std::min(sizeof(buffer), available.size - received);
    good = request.readExact(buffer, count, started, 180000);
    if (!good) break;
    good = esp_ota_write(handle, buffer, count) == ESP_OK && mbedtls_sha256_update(&hash, buffer, count) == 0;
    tail.append(reinterpret_cast<char *>(buffer), count);
    identityFound = identityFound || tail.find(marker) != std::string::npos;
    if (tail.size() >= marker.size()) tail.erase(0, tail.size() - marker.size() + 1);
    received += count;
    publish(Phase::Downloading, "Downloading...", received * 100 / available.size);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  publish(Phase::Verifying, "Verifying firmware", 100);
  uint8_t digest[32];
  good = good && received == available.size && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  good = good && identityFound && memcmp(digest, available.digest.data(), sizeof(digest)) == 0;
  if (!good || cancelled()) {
    esp_ota_abort(handle);
    fail("Firmware rejected"); return;
  }
  // Seal the operation atomically with respect to the UI's cancellation request.
  portENTER_CRITICAL(&stateLock);
  bool abort = cancelRequested.load();
  if (!abort) { state.cancellable = false; state.phase = Phase::Finalizing; }
  portEXIT_CRITICAL(&stateLock);
  if (abort) { esp_ota_abort(handle); fail("Cancelled"); return; }
  publish(Phase::Finalizing, "Preparing restart", 100);
  if (esp_ota_end(handle) != ESP_OK) { fail("Image validation failed"); return; }
  if (esp_ota_set_boot_partition(partition) != ESP_OK) { fail("Boot selection failed"); return; }
  // Keep busy set until reboot, so another operation cannot replace the boot choice.
  publish(Phase::Restarting, "Restarting...", 100);
}

void worker(void *argument) {
  bool install = argument != nullptr;
  if (install) installRelease(); else checkRelease();
  vTaskDelete(nullptr);
}
void launch(bool install) {
  portENTER_CRITICAL(&stateLock);
  if (state.busy || (install && state.phase != Phase::Available)) { portEXIT_CRITICAL(&stateLock); return; }
  cancelRequested.store(false);
  state.busy = state.cancellable = true;
  state.phase = install ? Phase::Downloading : Phase::Checking;
  state.progress = 0;
  portEXIT_CRITICAL(&stateLock);
  if (xTaskCreate(worker, "heltec-ota", 16384, install ? reinterpret_cast<void *>(1) : nullptr, 1, nullptr) != pdPASS) {
    fail("Not enough memory");
  }
}
}  // namespace

Snapshot snapshot() {
  portENTER_CRITICAL(&stateLock);
  Snapshot copy = state;
  portEXIT_CRITICAL(&stateLock);
  return copy;
}
void check() { launch(false); }
void install() { launch(true); }
void cancel() {
  portENTER_CRITICAL(&stateLock);
  if (state.busy && state.cancellable) cancelRequested.store(true);
  portEXIT_CRITICAL(&stateLock);
}
const char *imageIdentity() { return IDENTITY; }
void tick(bool displayReady, bool storageReady) {
  if (!healthChecked && millis() >= 10000) {
    healthChecked = true;
    esp_ota_img_states_t status;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &status) == ESP_OK && status == ESP_OTA_IMG_PENDING_VERIFY) {
      // WiFi availability is deliberately not a health requirement: offline boot must work.
      if (displayReady && storageReady && ESP.getFlashChipSize() == 0x800000) esp_ota_mark_app_valid_cancel_rollback();
      else esp_ota_mark_app_invalid_rollback_and_reboot();
    }
  }
  if (snapshot().phase == Phase::Restarting) {
    if (!restartAt) restartAt = millis();
    if (millis() - restartAt >= 1500) ESP.restart();
  }
}
}  // namespace Ota
