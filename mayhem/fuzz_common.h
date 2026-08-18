// Shared helpers for the mayhemheroes libwebp libFuzzer harnesses.
//
// These are the classic (pre-FuzzTest) helpers from upstream's old
// tests/fuzzer/fuzz_utils.h, inlined so the harnesses stay self-contained:
// upstream's current fuzz_utils.h depends on the FuzzTest framework, which
// FetchContents google/fuzztest from the network and is not air-gappable.
#ifndef MAYHEM_FUZZ_COMMON_H_
#define MAYHEM_FUZZ_COMMON_H_

#include <stddef.h>
#include <stdint.h>

// Arbitrary limits to prevent OOM, timeout, or slow execution.
static const size_t kFuzzPxLimit = 1024 * 1024;
static const int kFuzzFrameLimit = 3;

// Reads and sums (up to) 128 spread-out bytes.
static uint8_t FuzzHash(const uint8_t* const data, size_t size) {
  uint8_t value = 0;
  size_t incr = size / 128;
  if (!incr) incr = 1;
  for (size_t i = 0; i < size; i += incr) value += data[i];
  return value;
}

#endif  // MAYHEM_FUZZ_COMMON_H_
