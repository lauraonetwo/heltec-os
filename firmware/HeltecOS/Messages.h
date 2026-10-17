#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "Morse.h"
#include "NetworkAccess.h"

namespace Messages {
struct Message { uint32_t id = 0; bool fromPhone = false; String text; };
void begin(WebServer &server);
void tick(NetworkAccess::Interface interface, bool updating);
void home();
bool send(const String &text, bool fromPhone);
const char *error();
const String &accessCode();
bool flashing();
void stopLight();
size_t count();
const Message &newest(size_t offset);
}
