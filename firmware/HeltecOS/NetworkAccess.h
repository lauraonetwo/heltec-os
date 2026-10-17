#pragma once
#include <cstdint>

namespace NetworkAccess {
enum class Interface { Blocked, Station, Hotspot };
// Password setup is never a messaging network, even while its station connects.
inline Interface select(bool setupPortal, bool connecting, bool hotspot, bool stationConnected) {
  if (setupPortal || connecting) return Interface::Blocked;
  if (hotspot) return Interface::Hotspot;
  return stationConnected ? Interface::Station : Interface::Blocked;
}
inline bool permits(Interface interface, uint32_t localAddress, uint32_t stationAddress, uint32_t hotspotAddress) {
  if (!localAddress) return false;
  return (interface == Interface::Station && localAddress == stationAddress) ||
         (interface == Interface::Hotspot && localAddress == hotspotAddress);
}
}
