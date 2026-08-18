// libwebp advanced decode-API fuzz harness (mayhemheroes port).
//
// Derived from the upstream OSS-Fuzz harness tests/fuzzer/advanced_api_fuzzer
// (the classic libFuzzer form). Upstream has since moved its in-tree fuzzers to
// the FuzzTest framework (which FetchContents google/fuzztest from the network
// at configure time and is not air-gappable), so this port keeps the original
// self-contained libFuzzer entry point and drives the same WebPDecode/WebPIDecode
// code path. FuzzHash + kFuzzPxLimit are inlined so the harness has no dependency
// on the FuzzTest-era fuzz_utils.h.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "src/webp/decode.h"

#include "./fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t* const data, size_t size) {
  int i;
  WebPDecoderConfig config;
  if (!WebPInitDecoderConfig(&config)) return 0;
  if (WebPGetFeatures(data, size, &config.input) != VP8_STATUS_OK) return 0;
  if ((size_t)config.input.width * config.input.height > kFuzzPxLimit) return 0;

  // Using two independent criteria ensures that all combinations of options
  // can reach each path at the decoding stage, with meaningful differences.

  const uint8_t value = FuzzHash(data, size);
  const float factor = value / 255.f;  // 0-1

  config.options.flip = value & 1;
  config.options.bypass_filtering = value & 2;
  config.options.no_fancy_upsampling = value & 4;
  config.options.use_threads = value & 8;
  if (size & 1) {
    config.options.use_cropping = 1;
    config.options.crop_width = (int)(config.input.width * (1 - factor));
    config.options.crop_height = (int)(config.input.height * (1 - factor));
    config.options.crop_left = config.input.width - config.options.crop_width;
    config.options.crop_top = config.input.height - config.options.crop_height;
  }
  if (size & 2) {
    int strength = (int)(factor * 100);
    config.options.dithering_strength = strength;
    config.options.alpha_dithering_strength = 100 - strength;
  }
  if (size & 4) {
    config.options.use_scaling = 1;
    config.options.scaled_width = (int)(config.input.width * factor * 2);
    config.options.scaled_height = (int)(config.input.height * factor * 2);
  }

#if defined(WEBP_REDUCE_CSP)
  config.output.colorspace = (value & 1)
                                 ? ((value & 2) ? MODE_RGBA : MODE_BGRA)
                                 : ((value & 2) ? MODE_rgbA : MODE_bgrA);
#else
  config.output.colorspace = (WEBP_CSP_MODE)(value % MODE_LAST);
#endif  // WEBP_REDUCE_CSP

  for (i = 0; i < 2; ++i) {
    if (i == 1) {
      // Use the bitstream data to generate extreme ranges for the options. An
      // alternative approach would be to use a custom corpus containing webp
      // files prepended with sizeof(config.options) zeroes to allow the fuzzer
      // to modify these independently.
      const int data_offset = 50;
      if (size > data_offset + sizeof(config.options)) {
        memcpy(&config.options, data + data_offset, sizeof(config.options));
      } else {
        break;
      }
    }
    if (size % 3) {
      // Decodes incrementally in chunks of increasing size.
      WebPIDecoder* idec = WebPIDecode(NULL, 0, &config);
      if (!idec) return 0;
      VP8StatusCode status;
      if (size & 8) {
        size_t available_size = value + 1;
        while (1) {
          if (available_size > size) available_size = size;
          status = WebPIUpdate(idec, data, available_size);
          if (status != VP8_STATUS_SUSPENDED || available_size == size) break;
          available_size *= 2;
        }
      } else {
        // WebPIAppend expects new data and its size with each call.
        // Implemented here by simply advancing the pointer into data.
        const uint8_t* new_data = data;
        size_t new_size = value + 1;
        while (1) {
          if (new_data + new_size > data + size) {
            new_size = data + size - new_data;
          }
          status = WebPIAppend(idec, new_data, new_size);
          if (status != VP8_STATUS_SUSPENDED || new_size == 0) break;
          new_data += new_size;
          new_size *= 2;
        }
      }
      WebPIDelete(idec);
    } else {
      WebPDecode(data, size, &config);
    }

    WebPFreeDecBuffer(&config.output);
  }
  return 0;
}
