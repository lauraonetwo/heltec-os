#include "../firmware/HeltecOS/Morse.h"
#include <cassert>
#include <limits>

int main() {
  assert(Morse::classify(349) == Morse::Gesture::Dot);
  assert(Morse::classify(350) == Morse::Gesture::Dash);
  assert(Morse::classify(1799) == Morse::Gesture::Dash);
  assert(Morse::classify(1800) == Morse::Gesture::Actions);
  for (size_t i = 0; Morse::ALPHABET[i]; ++i) assert(Morse::decode(Morse::encode(Morse::ALPHABET[i])) == Morse::ALPHABET[i]);
  assert(Morse::decode("..--") == '\0');
  assert(Morse::decode("") == '\0');
  std::string text = "unchanged";
  assert(Morse::normalize("  hello   123  ", text) && text == "HELLO 123");
  for (auto bad : {"", "   ", "<script>", "HI!", "LINE\nBREAK", "caf\xc3\xa9"}) assert(!Morse::normalize(bad, text));
  assert(!Morse::normalize(std::string(81, 'A'), text));
  assert(Morse::normalize(std::string(80, 'Z'), text));
  assert((Morse::pulses("SOS") == std::vector<uint8_t>{1,1,1,1,1,3,3,1,3,1,3,3,1,1,1,1,1,3}));
  assert((Morse::pulses("E E") == std::vector<uint8_t>{1,7,1,3}));
  assert((Morse::pulses("T") == std::vector<uint8_t>{3,3}));
  Morse::Composer draft;
  draft.add('.', 100); draft.add('-', 300);
  draft.tick(1499, false); assert(draft.text.empty());
  draft.tick(1600, true); assert(draft.text.empty()); // Held PRG must not split a letter.
  draft.tick(1600, false); assert(draft.text == "A" && draft.symbols.empty());
  draft.space(); draft.space(); assert(draft.text == "A ");
  draft.add('.', 2000); draft.commit(); assert(draft.text == "A E");
  draft.erase(); assert(draft.text == "A ");
  draft.add('.', 3000); draft.erase(); assert(draft.symbols.empty() && draft.text == "A ");
  draft.symbols = "..--"; assert(!draft.commit() && draft.text == "A ");
  draft.clear(); assert(draft.text.empty() && draft.symbols.empty() && draft.notice.empty());
  for (int i = 0; i < 5; ++i) assert(draft.add('.', 10));
  assert(!draft.add('.', 10)); assert(draft.commit() && draft.text == "5");
  draft.text = std::string(80, 'A'); assert(!draft.add('.', 20));
  draft.clear();
  draft.add('.', std::numeric_limits<uint32_t>::max() - 500);
  draft.tick(700, false); assert(draft.text == "E"); // millis() rollover.
}
