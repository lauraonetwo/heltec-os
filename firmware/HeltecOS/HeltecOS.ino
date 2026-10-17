#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <HT_SSD1306Wire.h>
#include "Config.h"
#include "Types.h"
#include "OtaUpdater.h"
#include "Messages.h"

#if !defined(ARDUINO_HELTEC_WIFI_LORA_32_V3)
#error "Select Heltec WiFi LoRa 32(V3) before compiling."
#endif

SSD1306Wire oled(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
WebServer server(80);
DNSServer dns;
Preferences preferences;
Credentials saved;
Credentials candidate;
NetworkEntry networks[Config::MAX_NETWORKS];
Page page = Page::Boot;
const char *mainItems[] = {"WiFi", "Messages", "Update system", "About"};
const char *messageItems[] = {"Compose", "Inbox", "Phone access", "Stop light", "Back"};
const char *draftItems[] = {"Continue writing", "Add space", "Send + light", "Delete last", "Clear draft", "Back to Messages"};
Morse::Composer composer;
int messageSelection = 0, draftSelection = 0;
size_t inboxOffset = 0, inboxPart = 0;
uint32_t inboxNewestId = 0;
String messageNotice;
const char *wifiItems[] = {"Connection mode", "Scan networks", "Connection status", "Forget network", "Back"};
const char *modeItems[] = {"WiFi network", "Device hotspot", "Back"};
int modeSelection = 0;
bool hotspotActive = false;
bool phoneAccessDetails = false;
bool modeSaved = true;
bool hotspotKeySaved = false;
String hotspotName, hotspotPassword;
int mainSelection = 0;
int wifiSelection = 0;
int networkSelection = 0;
int networkCount = 0;
bool storageReady = false;
bool displayReady = false;
bool updateActionSelected = false;
Ota::Phase lastUpdatePhase = Ota::Phase::Idle;
bool scanActive = false;
bool connecting = false;
bool saveOnConnect = false;
bool portalActive = false;
bool connectQueued = false;
bool forgetConfirmed = false;
String selectedSsid;
wifi_auth_mode_t selectedAuth = WIFI_AUTH_OPEN;
String apName;
String apPassword;
String formToken;
String statusMessage = "Not connected";
String scanMessage;
unsigned long bootAt, lastDraw, scanAt, connectAt, portalAt;
bool previousReading = HIGH;
bool stableButton = HIGH;
bool buttonArmed = false;
unsigned long buttonChangedAt = 0;
unsigned long pressedAt = 0;

String randomHex(size_t bytes) {
  String result;
  const char digits[] = "0123456789ABCDEF";
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t value = static_cast<uint8_t>(esp_random());
    result += digits[value >> 4];
    result += digits[value & 15];
  }
  return result;
}

String escapeHtml(const String &value) {
  String result;
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': result += "&amp;"; break;
      case '<': result += "&lt;"; break;
      case '>': result += "&gt;"; break;
      case '"': result += "&quot;"; break;
      case '\'': result += "&#39;"; break;
      default: result += value[i];
    }
  }
  return result;
}

bool supportedAuth(wifi_auth_mode_t auth) {
  return auth == WIFI_AUTH_OPEN || auth == WIFI_AUTH_WPA_PSK ||
         auth == WIFI_AUTH_WPA2_PSK || auth == WIFI_AUTH_WPA_WPA2_PSK ||
         auth == WIFI_AUTH_WPA3_PSK || auth == WIFI_AUTH_WPA2_WPA3_PSK;
}

bool validPassword(const String &password) {
  if (selectedAuth == WIFI_AUTH_OPEN) return password.isEmpty();
  if (password.length() >= 8 && password.length() <= 63) return true;
  if (password.length() != 64 || selectedAuth == WIFI_AUTH_WPA3_PSK) return false;
  for (size_t i = 0; i < 64; ++i) {
    if (!isxdigit(static_cast<unsigned char>(password[i]))) return false;
  }
  return true;
}

void sendHtml(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'");
  server.send(code, "text/html; charset=utf-8",
    "<!doctype html><html lang='en'><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Heltec OS WiFi setup</title><style>body{font:18px system-ui;max-width:30rem;"
    "margin:3rem auto;padding:1rem;background:#101820;color:#fff}input,button{box-sizing:border-box;"
    "width:100%;padding:12px;margin:12px 0;font:inherit}button{background:#80d8c0;border:0}"
    "a{color:#80d8c0}</style><h1>Heltec OS</h1>" + body + "</html>");
}

bool portalClient() {
  // Reject requests arriving through the home network interface.
  return portalActive && server.client().localIP() == WiFi.softAPIP();
}

void showPortalForm() {
  if (!portalClient()) { server.send(403, "text/plain", "Setup is available on the device access point only."); return; }
  if (connecting || connectQueued) {
    sendHtml(200, "<h2>Connecting...</h2><p>Check the OLED for the result. If connection fails, reload this page to retry.</p>");
    return;
  }
  sendHtml(200, "<h2>Connect to WiFi</h2><p>Network: <strong>" + escapeHtml(selectedSsid) +
    "</strong></p><p>" + escapeHtml(statusMessage) + "</p>"
    "<form method='post' action='/connect'><input type='hidden' name='token' value='" + formToken +
    "'><label for='password'>WiFi password</label><input id='password' name='password' type='password' "
    "minlength='8' maxlength='64' autocomplete='new-password' required>"
    "<button type='submit'>Connect</button></form><p>The password is stored on this device only after a successful connection. "
    "Hold PRG to cancel setup.</p>");
}

void acceptCredentials() {
  if (!portalClient() || server.arg("token") != formToken) {
    server.send(403, "text/plain", "Invalid setup session."); return;
  }
  if (connecting || connectQueued) { server.send(409, "text/plain", "Connection already in progress."); return; }
  String password = server.arg("password");
  if (!validPassword(password)) {
    sendHtml(400, "<p>Use 8-63 password bytes, or a 64-digit hexadecimal WPA/WPA2 key.</p><a href='/'>Try again</a>");
    return;
  }
  candidate = Credentials{};
  selectedSsid.toCharArray(candidate.ssid, sizeof(candidate.ssid));
  password.toCharArray(candidate.password, sizeof(candidate.password));
  password = "";
  // Let the HTTP response finish before the station changes radio channel.
  sendHtml(200, "<h2>Connecting...</h2><p>Check the OLED for the result. Your phone may leave this temporary network. "
    "If connection fails, reconnect to the setup network and reload this page.</p>");
  connectQueued = true;
}

void stopPortal() {
  if (!portalActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive = false;
  connectQueued = false;
  apPassword = "";
  formToken = "";
}

void restoreSavedConnection() {
  if (hotspotActive) return;
  WiFi.setAutoReconnect(true);
  if (saved.ssid[0] && WiFi.status() != WL_CONNECTED) {
    WiFi.setMinSecurity(saved.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN);
    WiFi.begin(saved.ssid, saved.password);
  }
}

void cancelConnection() {
  if (connecting) WiFi.disconnect(false, false);
  connecting = false;
  connectQueued = false;
  candidate = Credentials{};
  stopPortal();
  restoreSavedConnection();
}

void startConnection(bool persist) {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  saveOnConnect = persist;
  connecting = true;
  connectAt = millis();
  statusMessage = "Connecting...";
  WiFi.setMinSecurity(candidate.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN);
  WiFi.begin(candidate.ssid, candidate.password);
  page = Page::Connecting;
}

void startPortal() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP_STA);
  apName = "Heltec-" + WiFi.macAddress().substring(12);
  apName.replace(":", "");
  apPassword = randomHex(5);
  formToken = randomHex(16);
  if (!WiFi.softAP(apName.c_str(), apPassword.c_str(), 1, false, 1)) {
    WiFi.mode(WIFI_STA);
    statusMessage = "Setup failed";
    restoreSavedConnection();
    page = Page::Status;
    return;
  }
  portalActive = true;
  portalAt = millis();
  statusMessage = "Enter the password for the selected network.";
  dns.start(53, "*", WiFi.softAPIP());
  page = Page::Portal;
}

void pollConnection() {
  if (!connecting) return;
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == String(candidate.ssid)) {
    statusMessage = "Connected";
    if (saveOnConnect) {
      if (storageReady && preferences.putBytes("network", &candidate, sizeof(candidate)) == sizeof(candidate)) {
        saved = candidate;
      } else {
        statusMessage = "Connected; not saved";
      }
    }
    candidate = Credentials{};
    connecting = false;
    stopPortal();
    WiFi.setAutoReconnect(true);
    if (page == Page::Connecting) page = Page::Status;
  } else if (millis() - connectAt >= Config::CONNECT_TIMEOUT_MS) {
    WiFi.disconnect(false, false);
    connecting = false;
    candidate = Credentials{};
    statusMessage = "Connection failed";
    if (portalActive) {
      page = Page::Portal;
    } else {
      restoreSavedConnection();
      if (page == Page::Connecting) page = Page::Status;
    }
  }
}

void stopScan() {
  if (scanActive) esp_wifi_scan_stop();
  scanActive = false;
  WiFi.scanDelete();
  restoreSavedConnection();
}

void startScan() {
  if (hotspotActive) { statusMessage = "Select WiFi network mode"; page = Page::Notice; return; }
  cancelConnection();
  WiFi.setAutoReconnect(false);
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
  WiFi.scanDelete();
  networkCount = 0;
  networkSelection = 0;
  scanMessage = "No networks found";
  scanAt = millis();
  scanActive = true;
  page = Page::Scanning;
  if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) {
    stopScan();
    scanMessage = "Scan failed";
    page = Page::Networks;
  }
}

void pollScan() {
  if (!scanActive) return;
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING && millis() - scanAt < Config::SCAN_TIMEOUT_MS) return;
  if (count < 0) {
    stopScan();
    scanMessage = "Scan failed";
    page = Page::Networks;
    return;
  }
  // Arduino returns strongest-first results. Keep one entry per SSID/auth pair.
  for (int i = 0; i < count && networkCount < Config::MAX_NETWORKS; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    bool duplicate = false;
    for (int j = 0; j < networkCount; ++j) {
      if (networks[j].ssid == ssid && networks[j].auth == auth) duplicate = true;
    }
    if (!duplicate) networks[networkCount++] = {ssid, WiFi.RSSI(i), auth};
  }
  stopScan();
  page = Page::Networks;
}

void chooseNetwork() {
  if (networkSelection == networkCount) { startScan(); return; }
  if (networkSelection == networkCount + 1) { page = Page::WiFiMenu; return; }
  selectedSsid = networks[networkSelection].ssid;
  selectedAuth = networks[networkSelection].auth;
  if (!supportedAuth(selectedAuth)) {
    statusMessage = "Unsupported security";
    page = Page::Notice;
    return;
  }
  if (selectedAuth == WIFI_AUTH_OPEN) {
    candidate = Credentials{};
    selectedSsid.toCharArray(candidate.ssid, sizeof(candidate.ssid));
    startConnection(true);
  } else {
    // Always allow a new password, including when the saved password changed.
    startPortal();
  }
}

void setConnectionMode(bool hotspot, bool persist) {
  if (Ota::snapshot().busy) { statusMessage = "System update busy"; page = Page::Notice; return; }
  // Keep message history and saved router credentials while changing radio interfaces.
  server.stop();
  WiFi.setAutoReconnect(false);
  if (scanActive) esp_wifi_scan_stop();
  scanActive = false;
  WiFi.scanDelete();
  connecting = connectQueued = false;
  candidate = Credentials{};
  stopPortal();
  hotspotActive = false;
  WiFi.disconnect(false, false);
  WiFi.softAPdisconnect(true);
  phoneAccessDetails = false;
  bool started = true;
  if (hotspot) {
    IPAddress address(192, 168, 4, 1);
    started = WiFi.mode(WIFI_AP) && WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0)) &&
      WiFi.softAP(hotspotName.c_str(), hotspotPassword.c_str(), 1, false, 2);
    if (started) {
      hotspotActive = true;
      statusMessage = "Hotspot active";
      page = Page::PhoneAccess;
    }
  }
  if (!hotspot || !started) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    if (saved.ssid[0]) { candidate = saved; selectedSsid = saved.ssid; startConnection(false); }
    else { statusMessage = "Select Scan networks"; page = Page::Status; }
    if (!started) { statusMessage = "Hotspot start failed"; page = Page::Notice; }
  }
  if (persist && started) modeSaved = storageReady && (!hotspot || hotspotKeySaved) && preferences.putBool("hotspot", hotspot) == 1;
  server.begin();
}

void shortPress() {
  switch (page) {
    case Page::Menu: mainSelection = (mainSelection + 1) % 4; break;
    case Page::MessagesMenu: messageSelection = (messageSelection + 1) % 5; messageNotice = ""; break;
    case Page::DraftActions: draftSelection = (draftSelection + 1) % 6; messageNotice = ""; break;
    case Page::Inbox:
      if (Messages::count()) {
        if ((inboxPart + 1) * 36 < Messages::newest(inboxOffset).text.length()) ++inboxPart;
        else { inboxPart = 0; inboxOffset = (inboxOffset + 1) % Messages::count(); }
      }
      break;
    case Page::WiFiMenu: wifiSelection = (wifiSelection + 1) % 5; break;
    case Page::ConnectionMode: modeSelection = (modeSelection + 1) % 3; break;
    case Page::PhoneAccess: if (hotspotActive) phoneAccessDetails = !phoneAccessDetails; break;
    case Page::Networks: networkSelection = (networkSelection + 1) % (networkCount + 2); break;
    case Page::Forget: forgetConfirmed = !forgetConfirmed; break;
    case Page::Update: if (!hotspotActive && !Ota::snapshot().busy) updateActionSelected = !updateActionSelected; break;
    default: break;
  }
}

void longPress() {
  switch (page) {
    case Page::Boot: page = Page::Menu; break;
    case Page::Menu:
      page = mainSelection == 0 ? Page::WiFiMenu : mainSelection == 1 ? Page::MessagesMenu : mainSelection == 2 ? Page::Update : Page::About;
      if (page == Page::Update) { updateActionSelected = false; if (!hotspotActive) Ota::check(); }
      break;
    case Page::MessagesMenu:
      messageNotice = "";
      if (messageSelection == 0) page = Page::Compose;
      else if (messageSelection == 1) { inboxOffset = inboxPart = 0; page = Page::Inbox; }
      else if (messageSelection == 2) { phoneAccessDetails = false; page = Page::PhoneAccess; }
      else if (messageSelection == 3) { Messages::stopLight(); messageNotice = "Light stopped"; }
      else page = Page::Menu;
      break;
    case Page::DraftActions:
      messageNotice = "";
      if (draftSelection == 0) page = Page::Compose;
      else if (draftSelection == 1) { composer.space(); page = Page::Compose; }
      else if (draftSelection == 2) {
        if (Messages::send(String(composer.text.c_str()), false)) {
          composer.clear(); messageNotice = "Sent to history + LED"; page = Page::MessagesMenu;
        } else messageNotice = Messages::error();
      }
      else if (draftSelection == 3) { composer.erase(); page = Page::Compose; }
      else if (draftSelection == 4) { composer.clear(); page = Page::Compose; }
      else page = Page::MessagesMenu;
      break;
    case Page::Inbox:
    case Page::PhoneAccess: page = Page::MessagesMenu; break;
    case Page::WiFiMenu:
      if (wifiSelection == 0) { modeSelection = hotspotActive ? 1 : 0; page = Page::ConnectionMode; }
      else if (wifiSelection == 1) startScan();
      else if (wifiSelection == 2) page = Page::Status;
      else if (wifiSelection == 3) { forgetConfirmed = false; page = Page::Forget; }
      else page = Page::Menu;
      break;
    case Page::ConnectionMode:
      if (modeSelection == 2) page = Page::WiFiMenu;
      else setConnectionMode(modeSelection == 1, true);
      break;
    case Page::Scanning: stopScan(); page = Page::WiFiMenu; break;
    case Page::Networks: chooseNetwork(); break;
    case Page::Portal:
    case Page::Connecting: cancelConnection(); page = Page::WiFiMenu; break;
    case Page::Forget:
      if (forgetConfirmed) {
        bool removed = storageReady && (!preferences.isKey("network") || preferences.remove("network"));
        if (removed) {
          connecting = false;
          candidate = Credentials{};
          saved = Credentials{};
          WiFi.setAutoReconnect(false);
          if (!hotspotActive) WiFi.disconnect(false, true);
          statusMessage = "Network forgotten";
        } else statusMessage = "Could not forget";
        page = removed ? Page::Status : Page::Notice;
      } else page = Page::WiFiMenu;
      break;
    case Page::Update: {
      if (hotspotActive) { page = Page::Menu; break; }
      auto update = Ota::snapshot();
      if (update.busy) Ota::cancel();
      else if (!updateActionSelected) page = Page::Menu;
      else {
        updateActionSelected = false;
        if (update.phase == Ota::Phase::Available) Ota::install();
        else Ota::check();
      }
      break;
    }
    case Page::Status:
    case Page::Notice: page = Page::WiFiMenu; break;
    default: page = Page::Menu; break;
  }
}

void pollButton() {
  bool reading = digitalRead(Config::PRG_PIN);
  unsigned long now = millis();
  if (reading != previousReading) buttonChangedAt = now;
  if (now - buttonChangedAt >= Config::DEBOUNCE_MS && reading != stableButton) {
    stableButton = reading;
    if (stableButton == LOW) { pressedAt = now; buttonArmed = true; }
    else if (buttonArmed) {
      buttonArmed = false;
      if (page == Page::Compose) {
        auto gesture = Morse::classify(now - pressedAt);
        if (gesture == Morse::Gesture::Actions) {
          composer.commit(); draftSelection = 0; messageNotice = ""; page = Page::DraftActions;
        } else composer.add(gesture == Morse::Gesture::Dot ? '.' : '-', now);
      }
      else if (now - pressedAt >= Config::LONG_PRESS_MS) longPress();
      else shortPress();
    }
  }
  previousReading = reading;
}

String fitText(String text, int width = 122) {
  text.replace("\n", " ");
  text.replace("\r", " ");
  if (oled.getStringWidth(text) <= width) return text;
  while (text.length() && oled.getStringWidth(text + "...") > width) text.remove(text.length() - 1);
  return text + "...";
}

void title(const String &text) {
  oled.clear();
  oled.setColor(WHITE);
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.drawString(64, 0, fitText(text));
  oled.drawLine(0, 13, 127, 13);
}

void line(int y, const String &text) {
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.drawString(64, y, fitText(text));
}

void row(int y, const String &text, bool selected) {
  if (selected) { oled.fillRect(0, y, 128, 12); oled.setColor(BLACK); }
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawString(3, y, fitText(text));
  oled.setColor(WHITE);
}

void drawScreen() {
  switch (page) {
    case Page::Boot:
      title("HELTEC OS");
      oled.setFont(ArialMT_Plain_16);
      line(20, "hi " + String(Config::USER_NAME) + "!");
      oled.drawRect(14, 46, 100, 8);
      oled.fillRect(16, 48, min(millis() - bootAt, Config::BOOT_MS) * 96 / Config::BOOT_MS, 4);
      break;
    case Page::Menu: {
      title("HELTEC OS v" + String(Config::VERSION));
      int first = mainSelection < 3 ? 0 : 1;
      for (int i = first; i < first + 3; ++i) row(15 + (i - first) * 12, mainItems[i], mainSelection == i);
      line(51, "Tap: next  Hold: select");
      break;
    }
    case Page::MessagesMenu: {
      title("Messages (" + String(Messages::count()) + ")" + (Messages::flashing() ? " *" : ""));
      int first = messageSelection < 3 ? 0 : messageSelection - 2;
      for (int i = first; i < min(first + 3, 5); ++i) row(15 + (i - first) * 12, messageItems[i], messageSelection == i);
      line(51, messageNotice.isEmpty() ? "Tap: next  Hold: select" : messageNotice);
      break;
    }
    case Page::Compose: {
      title("Compose " + String(composer.text.size()) + "/80");
      String draft(composer.text.c_str());
      if (draft.length() > 24) draft = draft.substring(draft.length() - 24);
      line(15, draft.substring(0, 12)); line(27, draft.substring(12));
      String marks(composer.symbols.c_str());
      if (stableButton == LOW) marks += millis() - pressedAt >= 1800 ? " [Actions]" : millis() - pressedAt >= 350 ? " [-]" : " [.]";
      line(39, composer.notice.empty() ? (marks.isEmpty() ? "Tap . / hold - / wait letter" : marks) : String(composer.notice.c_str()));
      line(51, "Hold 1.8s: actions");
      break;
    }
    case Page::DraftActions: {
      title("Draft actions");
      int first = draftSelection < 3 ? 0 : draftSelection - 2;
      for (int i = first; i < min(first + 3, 6); ++i) row(15 + (i - first) * 12, draftItems[i], draftSelection == i);
      line(51, messageNotice.isEmpty() ? "Tap: next  Hold: select" : messageNotice);
      break;
    }
    case Page::Inbox: {
      if (!Messages::count()) { title("Inbox"); line(24, "No messages yet"); }
      else {
        if (inboxOffset >= Messages::count() || inboxNewestId != Messages::newest(0).id) {
          inboxOffset = inboxPart = 0; inboxNewestId = Messages::newest(0).id;
        }
        const auto &message = Messages::newest(inboxOffset);
        title(String(message.fromPhone ? "Phone" : "Board") + " #" + String(message.id) + " " + String(inboxPart + 1) + "/" + String((message.text.length() + 35) / 36));
        for (size_t i = 0; i < 3; ++i) {
          size_t start = inboxPart * 36 + i * 12;
          line(15 + i * 12, start < message.text.length() ? message.text.substring(start, start + 12) : "");
        }
      }
      line(51, "Tap: next  Hold: back"); break;
    }
    case Page::PhoneAccess:
      title(!modeSaved ? "Settings not saved" : hotspotActive ? "Device hotspot" : "Phone: same WiFi");
      if (hotspotActive && !phoneAccessDetails) {
        line(15, hotspotName);
        line(27, "Key: " + hotspotPassword);
        line(39, "Join in iPhone WiFi");
      } else if (hotspotActive || WiFi.status() == WL_CONNECTED) {
        line(15, "http://");
        line(27, (hotspotActive ? WiFi.softAPIP() : WiFi.localIP()).toString());
        line(39, "Code: " + Messages::accessCode());
      } else { line(22, "Connect WiFi first"); line(36, "WiFi > Scan networks"); }
      line(51, hotspotActive ? "Tap: next  Hold: back" : "Hold PRG: back"); break;
    case Page::WiFiMenu: {
      title(hotspotActive ? "WiFi - Hotspot" : WiFi.status() == WL_CONNECTED ? "WiFi - Connected" : "WiFi - Offline");
      int first = wifiSelection < 3 ? 0 : wifiSelection - 2;
      for (int i = first; i < first + 3; ++i) row(15 + (i - first) * 12, wifiItems[i], wifiSelection == i);
      line(51, "Tap: next  Hold: select");
      break;
    }
    case Page::ConnectionMode:
      title(modeSaved ? "Connection mode" : "Mode not saved");
      for (int i = 0; i < 3; ++i) row(15 + i * 12,
        String(i == (hotspotActive ? 1 : 0) ? "* " : "  ") + modeItems[i], modeSelection == i);
      line(51, "Tap: next  Hold: select"); break;
    case Page::Scanning:
      title("WiFi"); line(22, "Scanning networks..."); line(51, "Hold PRG: cancel"); break;
    case Page::Networks: {
      title(networkCount ? "WiFi: " + String(networkCount) + " networks" : scanMessage);
      int first = networkSelection < 3 ? 0 : networkSelection - 2;
      for (int i = first; i < min(first + 3, networkCount + 2); ++i) {
        String label = i == networkCount ? "Rescan" : i == networkCount + 1 ? "Back" :
          String(networks[i].auth == WIFI_AUTH_OPEN ? "  " : "* ") + networks[i].ssid;
        row(15 + (i - first) * 12, label, networkSelection == i);
      }
      line(51, "Tap: next  Hold: select");
      break;
    }
    case Page::Portal:
      title(statusMessage == "Connection failed" ? "Failed - retry setup" : "WiFi password setup");
      line(15, "Join " + apName);
      line(27, "Key: " + apPassword);
      line(39, "Open 192.168.4.1");
      line(51, "Hold PRG: cancel");
      break;
    case Page::Connecting:
      title("WiFi"); line(18, "Connecting..."); line(32, selectedSsid.isEmpty() ? String(candidate.ssid) : selectedSsid);
      line(51, "Hold PRG: cancel"); break;
    case Page::Status:
      title(hotspotActive ? "Hotspot: " + String(WiFi.softAPgetStationNum()) + " clients" : "WiFi status");
      if (hotspotActive) {
        line(16, hotspotName); line(28, "Key: " + hotspotPassword); line(40, WiFi.softAPIP().toString());
      } else if (WiFi.status() == WL_CONNECTED) {
        line(16, WiFi.SSID()); line(28, WiFi.localIP().toString());
        line(40, statusMessage == "Connected; not saved" ? statusMessage : "Signal: " + String(WiFi.RSSI()) + " dBm");
      } else {
        line(22, connecting ? "Connecting..." : statusMessage.startsWith("Connected") ? "Disconnected" : statusMessage);
        line(36, "2.4 GHz WiFi only");
      }
      line(51, modeSaved ? "Hold PRG: back" : "Mode not saved; hold back"); break;
    case Page::Notice:
      title("WiFi"); line(22, statusMessage); line(51, "Hold PRG: back"); break;
    case Page::Forget:
      title("Forget saved network?"); line(18, String(saved.ssid));
      row(32, forgetConfirmed ? "Forget network" : "Cancel", true);
      line(51, "Tap: change  Hold: OK"); break;
    case Page::Update: {
      auto update = Ota::snapshot();
      if (update.phase != lastUpdatePhase) { updateActionSelected = false; lastUpdatePhase = update.phase; }
      title("Update system");
      if (hotspotActive) {
        line(15, "Hotspot: local only"); line(27, "Use WiFi for updates");
        row(39, "Back", true); line(51, "Hold PRG: back"); break;
      }
      if (update.phase == Ota::Phase::Available) {
        line(15, "Available: " + String(update.version));
        line(27, "Installed: " + String(Config::VERSION));
      } else {
        line(15, String(update.message));
        line(27, update.busy ? String(update.progress) + "%" : "Version " + String(Config::VERSION));
      }
      if (update.busy) {
        oled.drawRect(14, 43, 100, 5);
        oled.fillRect(15, 44, update.progress * 98 / 100, 3);
        line(51, update.cancellable ? "Hold PRG: cancel" : "Keep power connected");
      } else {
        row(39, updateActionSelected ? (update.phase == Ota::Phase::Available ? "Install update" : "Check again") : "Back", true);
        line(51, "Tap: change  Hold: OK");
      }
      break;
    }
    case Page::About:
      title("About Heltec OS"); line(16, "Version " + String(Config::VERSION));
      line(28, "Heltec V3.2 / ESP32-S3");
      line(40, "Flash: " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
      line(51, "Hold PRG: back"); break;
  }
  oled.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(Config::PRG_PIN, INPUT_PULLUP);
  previousReading = stableButton = digitalRead(Config::PRG_PIN);
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);
  displayReady = oled.init();
  bootAt = millis();
  WiFi.persistent(false);
  WiFi.setHostname("heltec-os");
  WiFi.mode(WIFI_STA);
  storageReady = preferences.begin("heltec-os", false);
  hotspotName = "HeltecChat-" + WiFi.macAddress().substring(12);
  hotspotName.replace(":", "");
  hotspotPassword = storageReady ? preferences.getString("hotspot-key", "") : "";
  hotspotKeySaved = hotspotPassword.length() == 12;
  if (hotspotPassword.length() != 12) {
    hotspotPassword = randomHex(6);
    hotspotKeySaved = storageReady && preferences.putString("hotspot-key", hotspotPassword) == hotspotPassword.length();
  }
  if (storageReady && preferences.getBool("hotspot", false) && !hotspotKeySaved) modeSaved = false;
  if (storageReady && preferences.getBytesLength("network") == sizeof(saved)) {
    preferences.getBytes("network", &saved, sizeof(saved));
    saved.ssid[32] = '\0';
    saved.password[64] = '\0';
  }
  Messages::begin(server);
  server.on("/", HTTP_GET, []() { if (portalClient()) showPortalForm(); else Messages::home(); });
  server.on("/connect", HTTP_POST, acceptCredentials);
  server.onNotFound([]() {
    if (!portalClient()) { server.send(404, "text/plain", "Not found. Open /messages."); return; }
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "Open WiFi setup.");
  });
  server.begin();
  if (storageReady && preferences.getBool("hotspot", false)) {
    setConnectionMode(true, false);
    if (hotspotActive) page = Page::Boot;
  } else if (saved.ssid[0]) {
    candidate = saved;
    startConnection(false);
    page = Page::Boot;
  }
  Serial.println("HELTEC OS v" + String(Config::VERSION));
  Serial.println(Ota::imageIdentity());
}

void loop() {
  Ota::tick(displayReady, storageReady);
  pollButton();
  if (page == Page::Compose) composer.tick(millis(), stableButton == LOW || previousReading == LOW);
  pollScan();
  Messages::tick(NetworkAccess::select(portalActive, connecting, hotspotActive, WiFi.status() == WL_CONNECTED), Ota::snapshot().busy);
  server.handleClient();
  if (portalActive) {
    dns.processNextRequest();
    if (connectQueued) { connectQueued = false; startConnection(true); }
    if (millis() - portalAt >= Config::PORTAL_TIMEOUT_MS) {
      cancelConnection();
      statusMessage = "Setup timed out";
      page = Page::Status;
    }
  }
  pollConnection();
  unsigned long now = millis();
  if (page == Page::Boot && now - bootAt >= Config::BOOT_MS) page = Page::Menu;
  if (now - lastDraw >= 50) { lastDraw = now; drawScreen(); }
  delay(1);
}
