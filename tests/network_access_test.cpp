#include "../firmware/HeltecOS/NetworkAccess.h"
#include <cassert>
#include <initializer_list>

int main() {
  using namespace NetworkAccess;
  constexpr uint32_t station = 0x0a000001, ap = 0xc0a80401;
  // Hotspot messaging must work without any station connection or Internet.
  auto direct = select(false, false, true, false);
  assert(direct == Interface::Hotspot);
  assert(permits(direct, ap, 0, ap));
  assert(!permits(direct, station, station, ap));
  auto home = select(false, false, false, true);
  assert(home == Interface::Station);
  assert(permits(home, station, station, ap));
  assert(!permits(home, ap, station, ap));
  assert(select(false, false, false, false) == Interface::Blocked);
  for (bool connected : {false, true}) {
    for (bool hotspot : {false, true}) {
      assert(select(true, false, hotspot, connected) == Interface::Blocked);
      assert(select(false, true, hotspot, connected) == Interface::Blocked);
      assert(select(true, true, hotspot, connected) == Interface::Blocked);
    }
  }
  assert(!permits(Interface::Blocked, ap, station, ap));
  assert(!permits(Interface::Hotspot, 0, 0, 0));
  assert(!permits(Interface::Station, 0, 0, ap));
}
