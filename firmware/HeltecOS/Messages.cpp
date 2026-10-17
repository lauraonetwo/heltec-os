#include "Messages.h"
#include "MessagesPage.h"
#include <WiFi.h>
#include <esp_system.h>

namespace Messages {
namespace {
WebServer *http = nullptr;
String code, session, nonce;
NetworkAccess::Interface networkInterface = NetworkAccess::Interface::Blocked;
bool updateBusy = false;
Message history[12];
size_t historyCount = 0, nextSlot = 0;
uint32_t nextId = 1;
std::vector<uint8_t> light;
size_t lightIndex = 0;
uint32_t lightAt = 0, lastLoginFailure = 0;
bool loginFailed = false;
const char *lastError = "";
String randomHex(size_t bytes) {
  String result;
  constexpr char digits[] = "0123456789ABCDEF";
  for (size_t i = 0; i < bytes; ++i) { uint8_t n = esp_random(); result += digits[n >> 4]; result += digits[n & 15]; }
  return result;
}
void headers(bool script = false) {
  http->sendHeader("Cache-Control", "no-store");
  http->sendHeader("X-Content-Type-Options", "nosniff");
  http->sendHeader("Referrer-Policy", "no-referrer");
  http->sendHeader("Content-Security-Policy", String("default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; connect-src 'self'") +
    (script ? "; script-src 'nonce-" + nonce + "'" : ""));
}
bool local() {
  if (NetworkAccess::permits(networkInterface, uint32_t(http->client().localIP()),
      WiFi.status() == WL_CONNECTED ? uint32_t(WiFi.localIP()) : 0, uint32_t(WiFi.softAPIP()))) return true;
  http->send(403, "text/plain", "Join the selected messaging network. WiFi password setup cannot access messages.");
  return false;
}
bool signedIn() {
  String cookies = http->header("Cookie");
  int start = 0;
  while (start < static_cast<int>(cookies.length())) {
    int end = cookies.indexOf(';', start);
    if (end < 0) end = cookies.length();
    String item = cookies.substring(start, end); item.trim();
    if (item == "heltec_session=" + session) return true;
    start = end + 1;
  }
  return false;
}
bool authorize(bool write = false) {
  headers();
  if (!local()) return false;
  if (!signedIn()) { http->send(401, "text/plain", "Open Messages and enter the code on the OLED."); return false; }
  if (write && http->header("X-Heltec-Token") != session) { http->send(403, "text/plain", "Invalid message session."); return false; }
  return true;
}
void login() {
  headers();
  if (!local()) return;
  if (loginFailed && uint32_t(millis() - lastLoginFailure) < 2000) { http->send(429, "text/plain", "Wait two seconds and try again."); return; }
  String entered = http->arg("code"); entered.trim(); entered.toUpperCase();
  if (entered != code) {
    loginFailed = true; lastLoginFailure = millis();
    http->send(403, "text/plain", "Incorrect code. Go back and use Messages > Phone access on the OLED."); return;
  }
  http->sendHeader("Set-Cookie", "heltec_session=" + session + "; Path=/; HttpOnly; SameSite=Strict");
  http->sendHeader("Location", "/messages");
  http->send(303, "text/plain", "Open Messages.");
}
void list() {
  if (!authorize()) return;
  String json = String("{\"busy\":") + ((flashing() || updateBusy) ? "true" : "false") + ",\"messages\":[";
  // Only normalized A-Z, digits and spaces enter history, so no JSON quoting is needed.
  for (size_t i = 0; i < historyCount; ++i) {
    const auto &message = newest(historyCount - i - 1);
    if (i) json += ',';
    json += "{\"id\":" + String(message.id) + ",\"phone\":" + (message.fromPhone ? "true" : "false") + ",\"text\":\"" + message.text + "\"}";
  }
  http->send(200, "application/json", json + "]}");
}
}
void begin(WebServer &server) {
  http = &server;
  code = randomHex(4); session = randomHex(16); nonce = randomHex(16);
  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW);
  const char *keys[] = {"Cookie", "X-Heltec-Token"};
  server.collectHeaders(keys, 2);
  server.on("/messages", HTTP_GET, home);
  server.on("/messages/login", HTTP_POST, login);
  server.on("/api/messages", HTTP_GET, list);
  server.on("/api/messages", HTTP_POST, []() {
    if (!authorize(true)) return;
    if (!send(http->arg("text"), true)) { http->send(flashing() || updateBusy ? 409 : 400, "text/plain", error()); return; }
    http->send(201, "text/plain", "Message delivered to board.");
  });
  server.on("/api/light/stop", HTTP_POST, []() {
    if (!authorize(true)) return;
    stopLight(); http->send(200, "text/plain", "Light stopped.");
  });
}
void home() {
  headers(true);
  if (!local()) return;
  if (!signedIn()) {
    http->send(200, "text/html; charset=utf-8", "<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Heltec OS Messages</title><style>body{font:18px system-ui;max-width:28rem;margin:3rem auto;padding:1rem;background:#101820;color:white}"
      "input,button{box-sizing:border-box;width:100%;padding:12px;margin:12px 0;font:inherit}button{background:#80d8c0;border:0}</style>"
      "<h1>Heltec OS Messages</h1><p>On the board, open <strong>Messages &gt; Phone access</strong>.</p>"
      "<form action='/messages/login' method='post'><label for='code'>Access code shown on the OLED</label>"
      "<input id='code' name='code' maxlength='8' minlength='8' autocomplete='off' autocapitalize='characters' required>"
      "<button>Connect</button></form><p>The code changes when the board restarts. Join the same WiFi network or the board's Device hotspot.</p></html>");
    return;
  }
  String html(FPSTR(MESSAGE_PAGE));
  html.replace("{{TOKEN}}", session); html.replace("{{NONCE}}", nonce);
  http->send(200, "text/html; charset=utf-8", html);
}
void tick(NetworkAccess::Interface interface, bool updating) {
  networkInterface = interface; updateBusy = updating;
  if (updating) { stopLight(); return; }
  if (flashing() && uint32_t(millis() - lightAt) >= light[lightIndex] * 120U) {
    ++lightIndex; lightAt = millis();
    if (lightIndex >= light.size()) stopLight();
    else digitalWrite(LED_BUILTIN, lightIndex % 2 == 0 ? HIGH : LOW);
  }
}
bool send(const String &text, bool fromPhone) {
  if (updateBusy) { lastError = "System update busy"; return false; }
  if (flashing()) { lastError = "Light busy; wait or stop"; return false; }
  std::string normalized;
  if (!Morse::normalize(std::string_view(text.c_str(), text.length()), normalized)) { lastError = "Use 1-80 A-Z, 0-9 or spaces"; return false; }
  history[nextSlot] = {nextId++, fromPhone, String(normalized.c_str())};
  nextSlot = (nextSlot + 1) % 12; if (historyCount < 12) ++historyCount;
  light = Morse::pulses(normalized); lightIndex = 0; lightAt = millis();
  digitalWrite(LED_BUILTIN, HIGH); lastError = "";
  return true;
}
const char *error() { return lastError; }
const String &accessCode() { return code; }
bool flashing() { return !light.empty(); }
void stopLight() { light.clear(); lightIndex = 0; digitalWrite(LED_BUILTIN, LOW); }
size_t count() { return historyCount; }
const Message &newest(size_t offset) { return history[(nextSlot + 12 - 1 - (offset % 12)) % 12]; }
}
