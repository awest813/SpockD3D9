/**
 * Hermetic test for macOS Homebrew prefix ordering.
 *
 * Built twice in CI with DXVK_TEST_HOMEBREW_*_SLICE so both Intel Mac and
 * Apple Silicon orderings are verified on a single Linux runner.
 */

#if !defined(__APPLE__)
#error "This test targets macOS Homebrew prefix selection"
#endif

#if !defined(DXVK_TEST_HOMEBREW_INTEL_SLICE) && !defined(DXVK_TEST_HOMEBREW_ARM_SLICE)
#error "Define DXVK_TEST_HOMEBREW_INTEL_SLICE or DXVK_TEST_HOMEBREW_ARM_SLICE"
#endif

#include <cassert>
#include <string>
#include <vector>

#include "../../src/util/util_env.h"

int main() {
  std::vector<std::string> defaults;
  dxvk::env::appendDefaultHomebrewPrefixes(defaults);

  assert(defaults.size() == 2);

#if defined(DXVK_TEST_HOMEBREW_INTEL_SLICE)
  assert(defaults[0] == "/usr/local");
  assert(defaults[1] == "/opt/homebrew");
#elif defined(DXVK_TEST_HOMEBREW_ARM_SLICE)
  assert(defaults[0] == "/opt/homebrew");
  assert(defaults[1] == "/usr/local");
#endif

  return 0;
}
