#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace OtaProtocol {
inline constexpr char BOARD[] = "heltec-wifi-lora-32-v3.2";
inline constexpr char RELEASE_BASE[] = "https://github.com/lauraonetwo/heltec-os/releases/";
inline constexpr size_t MAX_MANIFEST = 512;
inline constexpr size_t SIGNATURE_BYTES = 384;  // RSA-3072, PKCS#1 v1.5 / SHA-256.
inline constexpr size_t SLOT_SIZE = 0x200000;
struct Version { std::array<uint16_t, 3> parts{}; };
struct Manifest {
  std::string version;
  Version parsedVersion;
  size_t size = 0;
  std::array<uint8_t, 32> digest{};
  std::string url;
};

inline bool startsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}
inline bool decimal(std::string_view text, uint32_t limit, uint32_t &value) {
  if (text.empty() || text.size() > 10 || (text.size() > 1 && text[0] == '0')) return false;
  value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    uint32_t digit = static_cast<unsigned>(c - '0');
    if (digit > limit || value > (limit - digit) / 10) return false;
    value = value * 10 + digit;
  }
  return true;
}
inline bool parseVersion(std::string_view text, Version &version) {
  for (size_t i = 0; i < 3; ++i) {
    size_t end = text.find('.');
    if ((i < 2 && end == std::string_view::npos) || (i == 2 && end != std::string_view::npos)) return false;
    uint32_t value;
    if (!decimal(text.substr(0, end), 65535, value)) return false;
    version.parts[i] = static_cast<uint16_t>(value);
    if (i < 2) text.remove_prefix(end + 1);
  }
  return true;
}
inline bool newer(const Version &candidate, const Version &installed) { return candidate.parts > installed.parts; }
inline std::string releaseUrl(const std::string &version, const char *file) {
  return std::string(RELEASE_BASE) + "download/v" + version + "/" + file;
}
inline bool allowedUrl(std::string_view url) {
  if (url.size() > 4096) return false;
  for (unsigned char c : url) if (c <= 32 || c >= 127 || c == '\\') return false;
  return startsWith(url, RELEASE_BASE) ||
    startsWith(url, "https://release-assets.githubusercontent.com/") ||
    startsWith(url, "https://objects.githubusercontent.com/") ||
    startsWith(url, "https://github-releases.githubusercontent.com/");
}
inline bool parseManifest(std::string_view text, Manifest &manifest) {
  if (text.empty() || text.size() > MAX_MANIFEST || text.back() != '\n') return false;
  std::array<std::string_view, 6> lines;
  for (auto &line : lines) {
    size_t end = text.find('\n');
    if (end == std::string_view::npos) return false;
    line = text.substr(0, end);
    text.remove_prefix(end + 1);
  }
  if (!text.empty() || lines[0] != "HELTEC-OS-OTA-V1" || lines[1] != "board=heltec-wifi-lora-32-v3.2" ||
      !startsWith(lines[2], "version=") || !startsWith(lines[3], "size=") ||
      !startsWith(lines[4], "sha256=") || !startsWith(lines[5], "url=")) return false;
  Manifest result;
  result.version = std::string(lines[2].substr(8));
  if (!parseVersion(result.version, result.parsedVersion)) return false;
  uint32_t size;
  if (!decimal(lines[3].substr(5), SLOT_SIZE, size) || size < 24) return false;
  result.size = size;
  auto hash = lines[4].substr(7);
  if (hash.size() != 64) return false;
  for (size_t i = 0; i < hash.size(); ++i) {
    char c = hash[i];
    int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0) return false;
    result.digest[i / 2] = static_cast<uint8_t>((result.digest[i / 2] << 4) | digit);
  }
  result.url = std::string(lines[5].substr(4));
  if (result.url != releaseUrl(result.version, "heltec-os.bin")) return false;
  manifest = result;
  return true;
}
inline bool imageHeaderValid(const uint8_t *data, size_t size) {
  return size >= 24 && data[0] == 0xe9 && data[12] == 9 && data[13] == 0 && (data[3] >> 4) == 3;
}
inline std::string identity(const std::string &version, const char *keyId) {
  return std::string("HELTEC_OS_IMAGE_V1|") + BOARD + "|" + version + "|" + keyId + "|";
}
}
