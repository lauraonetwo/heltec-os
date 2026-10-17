#pragma once
#include <cstdint>

namespace Ota {
enum class Phase { Idle, Checking, Available, Current, NoRelease, Downloading, Verifying, Finalizing, Restarting, Error, Cancelled };
struct Snapshot {
  Phase phase = Phase::Idle;
  bool busy = false;
  bool cancellable = false;
  unsigned progress = 0;
  char message[64] = "Check GitHub releases";
  char version[24] = {};
};
Snapshot snapshot();
void check();
void install();
void cancel();
void tick(bool displayReady, bool storageReady);
const char *imageIdentity();
}
