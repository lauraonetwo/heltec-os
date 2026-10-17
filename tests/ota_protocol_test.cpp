#include "../firmware/HeltecOS/OtaProtocol.h"
#include <cassert>
#include <iostream>
using namespace OtaProtocol;
int main() {
  uint32_t value;
  assert(decimal("4294967295", UINT32_MAX, value) && value == UINT32_MAX);
  assert(!decimal("4294967296", UINT32_MAX, value));
  Version installed, candidate;
  assert(parseVersion("1.1.0", installed));
  assert(parseVersion("1.10.0", candidate) && newer(candidate, installed));
  assert(parseVersion("1.0.99", candidate) && !newer(candidate, installed));
  for (auto value : {"1.1", "1.1.0.0", "01.1.0", "1.-1.0", "1.65536.0", "1.1.0-beta", "999999999999.0.0"}) assert(!parseVersion(value, candidate));
  std::string base = "HELTEC-OS-OTA-V1\nboard=heltec-wifi-lora-32-v3.2\nversion=1.2.0\nsize=1000\nsha256=" + std::string(64, 'a') + "\nurl=https://github.com/lauraonetwo/heltec-os/releases/download/v1.2.0/heltec-os.bin\n";
  Manifest manifest;
  assert(parseManifest(base, manifest) && manifest.size == 1000 && manifest.digest[0] == 0xaa);
  assert(!parseManifest(base + "extra\n", manifest));
  assert(!parseManifest(base.substr(0, base.size()-1), manifest));
  for (auto replacement : {"size=2097153", "size=-1", "size=01000", "size=0", "size=4294967296"}) {
    auto bad = base; bad.replace(bad.find("size=1000"), 9, replacement); assert(!parseManifest(bad, manifest));
  }
  auto bad = base; bad.replace(bad.find("v3.2"), 4, "v4.0"); assert(!parseManifest(bad, manifest));
  bad = base; bad.replace(bad.find("download/v1.2.0"), 15, "download/v2.0.0"); assert(!parseManifest(bad, manifest));
  assert(allowedUrl("https://release-assets.githubusercontent.com/example?token=123"));
  for (auto url : {"http://github.com/lauraonetwo/heltec-os/releases/", "https://github.com.evil/releases/", "https://release-assets.githubusercontent.com@evil/", "https://github.com/other/repo/releases/", "https://release-assets.githubusercontent.com/\n"}) assert(!allowedUrl(url));
  uint8_t header[24]{}; header[0]=0xe9; header[3]=0x30; header[12]=9;
  assert(imageHeaderValid(header, 24)); assert(!imageHeaderValid(header, 23));
  header[12]=0; assert(!imageHeaderValid(header,24));
  std::cout << "OTA protocol tests passed\n";
}
