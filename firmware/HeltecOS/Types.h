#pragma once
#include <Arduino.h>
#include <WiFi.h>

// One NVS blob keeps the SSID and password together across power loss.
struct Credentials {
  char ssid[33] = {};
  char password[65] = {};
};

struct NetworkEntry {
  String ssid;
  int32_t rssi;
  wifi_auth_mode_t auth;
};

enum class Page { Boot, Menu, WiFiMenu, Scanning, Networks, Portal,
                  Connecting, Status, Notice, Forget, Update, About,
                  MessagesMenu, Compose, DraftActions, Inbox, PhoneAccess, ConnectionMode };
