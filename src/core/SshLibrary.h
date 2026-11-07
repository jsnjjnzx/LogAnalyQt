#pragma once

#include <libssh2.h>

inline bool initializeSshLibrary() {
  // libssh2_init must run once, before either a terminal or a transfer session starts.
  static const bool initialized = libssh2_init(0) == 0;
  return initialized;
}
