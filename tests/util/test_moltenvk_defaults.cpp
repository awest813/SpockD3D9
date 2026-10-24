/**
 * Policy for MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS.
 *
 * Tier 2 is the only MoltenVK setting that can bind DXVK's 2048-entry
 * sampler heap. An explicit user value, including 0, must win.
 */

#include <cassert>
#include <cstring>

#include "../../src/util/util_env.h"

int main() {
  const char* unset = dxvk::env::moltenVkArgumentBufferDefault(nullptr);
  const char* empty = dxvk::env::moltenVkArgumentBufferDefault("");

  assert(unset != nullptr && std::strcmp(unset, "2") == 0);
  assert(empty != nullptr && std::strcmp(empty, "2") == 0);
  assert(dxvk::env::moltenVkArgumentBufferDefault("0") == nullptr);
  assert(dxvk::env::moltenVkArgumentBufferDefault("1") == nullptr);
  assert(dxvk::env::moltenVkArgumentBufferDefault("2") == nullptr);

  return 0;
}
