#pragma once

#define HELTEC_OS_VERSION "1.1.0"

namespace Config {
constexpr char VERSION[] = HELTEC_OS_VERSION;
constexpr char USER_NAME[] = "user";
constexpr unsigned long BOOT_MS = 2500;
constexpr unsigned long DEBOUNCE_MS = 40;
constexpr unsigned long LONG_PRESS_MS = 700;
constexpr unsigned long CONNECT_TIMEOUT_MS = 20000;
constexpr unsigned long SCAN_TIMEOUT_MS = 15000;
constexpr unsigned long PORTAL_TIMEOUT_MS = 300000;
constexpr int PRG_PIN = 0;
constexpr int MAX_NETWORKS = 20;
}
