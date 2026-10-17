#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Morse {
inline constexpr size_t MAX_TEXT = 80;
enum class Gesture { Dot, Dash, Actions };
inline Gesture classify(uint32_t duration) {
  return duration < 350 ? Gesture::Dot : duration < 1800 ? Gesture::Dash : Gesture::Actions;
}
inline constexpr const char *ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
inline constexpr const char *CODES[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
  "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};
inline const char *encode(char letter) {
  for (size_t i = 0; ALPHABET[i]; ++i) if (ALPHABET[i] == letter) return CODES[i];
  return "";
}
inline char decode(std::string_view symbols) {
  for (size_t i = 0; ALPHABET[i]; ++i) if (symbols == CODES[i]) return ALPHABET[i];
  return '\0';
}
// Reject unsupported input instead of silently dropping characters. Collapse spaces.
inline bool normalize(std::string_view input, std::string &output) {
  if (input.empty() || input.size() > MAX_TEXT) return false;
  std::string result;
  for (char letter : input) {
    if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
    if (letter == ' ') {
      if (!result.empty() && result.back() != ' ') result += ' ';
    } else {
      if (!*encode(letter)) return false;
      result += letter;
    }
  }
  if (!result.empty() && result.back() == ' ') result.pop_back();
  if (result.empty()) return false;
  output = result;
  return true;
}
// Alternating ON/OFF durations in Morse units: 1 dot, 3 dash, 3 letter gap, 7 word gap.
inline std::vector<uint8_t> pulses(std::string_view text) {
  std::vector<uint8_t> result;
  for (char letter : text) {
    if (letter == ' ') { if (!result.empty()) result.back() = 7; continue; }
    const char *code = encode(letter);
    for (size_t i = 0; code[i]; ++i) {
      result.push_back(code[i] == '.' ? 1 : 3);
      result.push_back(code[i + 1] ? 1 : 3);
    }
  }
  return result;
}

class Composer {
 public:
  std::string text;
  std::string symbols;
  std::string notice;
  uint32_t lastSymbol = 0;
  bool add(char symbol, uint32_t now) {
    if (text.size() >= MAX_TEXT) { notice = "Message full"; return false; }
    if (symbols.size() >= 5) { notice = "Max 5 marks; wait"; return false; }
    symbols += symbol;
    lastSymbol = now;
    notice.clear();
    return true;
  }
  bool commit() {
    if (symbols.empty()) return true;
    char letter = decode(symbols);
    symbols.clear();
    if (!letter) { notice = "Unknown Morse; retry"; return false; }
    if (text.size() >= MAX_TEXT) { notice = "Message full"; return false; }
    text += letter;
    notice.clear();
    return true;
  }
  void tick(uint32_t now, bool buttonDown) {
    if (!buttonDown && !symbols.empty() && uint32_t(now - lastSymbol) >= 1200) commit();
  }
  void space() {
    if (commit() && !text.empty() && text.back() != ' ' && text.size() < MAX_TEXT) text += ' ';
  }
  void erase() {
    if (!symbols.empty()) symbols.pop_back();
    else if (!text.empty()) text.pop_back();
    notice.clear();
  }
  void clear() { text.clear(); symbols.clear(); notice.clear(); }
};
}
