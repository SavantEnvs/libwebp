// mayhem/webp_selftest.c — AUTHORED behavioral known-answer oracle for libwebp.
//
// Upstream libwebp ships NO offline unit/functional test suite (tests/ holds
// only FuzzTest-based fuzzers that FetchContent google/fuzztest from the
// network), so this authored oracle asserts encoder/decoder BEHAVIOR through
// the public API: exact lossless round-trips, header parsing, lossy sanity,
// incremental decode, and rejection of corrupt input. Each case prints
// "PASS: <name>" or "FAIL: <name>"; mayhem/test.sh counts these lines.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webp/decode.h"
#include "webp/encode.h"

static int g_pass = 0, g_fail = 0;

static void report(const char* name, int ok) {
  printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
  if (ok) ++g_pass; else ++g_fail;
}

#define W 64
#define H 48

static void fill_image(uint8_t* rgba) {
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      uint8_t* p = rgba + 4 * (y * W + x);
      p[0] = (uint8_t)(x * 4);
      p[1] = (uint8_t)(y * 5);
      p[2] = (uint8_t)((x * y) & 0xff);
      p[3] = (uint8_t)(255 - ((x + y) & 0x3f));
    }
}

int main(void) {
  uint8_t rgba[W * H * 4];
  fill_image(rgba);

  // 1. lossless encode produces a non-empty RIFF/WEBP container
  uint8_t* enc = NULL;
  size_t enc_size = WebPEncodeLosslessRGBA(rgba, W, H, W * 4, &enc);
  report("lossless_encode_nonempty", enc_size > 20 && enc != NULL);
  report("lossless_container_magic",
         enc_size > 12 && memcmp(enc, "RIFF", 4) == 0 &&
         memcmp(enc + 8, "WEBP", 4) == 0);

  // 2. header parse: WebPGetInfo returns the exact dimensions
  int w = 0, h = 0;
  int info_ok = enc && WebPGetInfo(enc, enc_size, &w, &h);
  report("getinfo_dimensions", info_ok && w == W && h == H);

  // 3. lossless round-trip is BIT-EXACT
  int dw = 0, dh = 0;
  uint8_t* dec = enc ? WebPDecodeRGBA(enc, enc_size, &dw, &dh) : NULL;
  report("lossless_roundtrip_bitexact",
         dec && dw == W && dh == H && memcmp(dec, rgba, sizeof(rgba)) == 0);
  WebPFree(dec);

  // 4. features: lossless stream is flagged lossless (format 2), has alpha
  WebPBitstreamFeatures feat;
  int feat_ok = enc &&
      WebPGetFeatures(enc, enc_size, &feat) == VP8_STATUS_OK;
  report("features_lossless_alpha",
         feat_ok && feat.format == 2 && feat.has_alpha == 1 &&
         feat.width == W && feat.height == H);

  // 5. incremental decode reproduces the same pixels
  if (enc) {
    WebPDecoderConfig config;
    int inc_ok = 0;
    if (WebPInitDecoderConfig(&config)) {
      config.output.colorspace = MODE_RGBA;
      WebPIDecoder* idec = WebPIDecode(NULL, 0, &config);
      if (idec) {
        size_t half = enc_size / 2;
        VP8StatusCode st = WebPIAppend(idec, enc, half);
        if (st == VP8_STATUS_SUSPENDED || st == VP8_STATUS_OK)
          st = WebPIAppend(idec, enc + half, enc_size - half);
        if (st == VP8_STATUS_OK &&
            config.output.width == W && config.output.height == H &&
            config.output.u.RGBA.rgba &&
            memcmp(config.output.u.RGBA.rgba, rgba, sizeof(rgba)) == 0)
          inc_ok = 1;
        WebPIDelete(idec);
      }
      WebPFreeDecBuffer(&config.output);
    }
    report("incremental_decode_bitexact", inc_ok);
  } else {
    report("incremental_decode_bitexact", 0);
  }

  // 6. lossy encode decodes to the right size with bounded error
  uint8_t* lossy = NULL;
  size_t lossy_size = WebPEncodeRGBA(rgba, W, H, W * 4, 90.0f, &lossy);
  int lossy_ok = 0;
  if (lossy_size > 0 && lossy) {
    int lw = 0, lh = 0;
    uint8_t* ldec = WebPDecodeRGBA(lossy, lossy_size, &lw, &lh);
    if (ldec && lw == W && lh == H) {
      double err = 0;
      for (size_t i = 0; i < sizeof(rgba); ++i) {
        double d = (double)rgba[i] - ldec[i];
        err += d * d;
      }
      err = sqrt(err / (double)sizeof(rgba));  // RMSE over RGBA
      lossy_ok = err < 24.0;  // q=90 on a smooth gradient stays well under this
    }
    WebPFree(ldec);
  }
  report("lossy_roundtrip_rmse", lossy_ok);
  WebPFree(lossy);

  // 7. corrupt input is REJECTED (truncated + bit-flipped bitstreams)
  if (enc && enc_size > 24) {
    int rej_trunc = !WebPGetInfo(enc, 11, &w, &h);
    uint8_t* bad = (uint8_t*)malloc(enc_size);
    memcpy(bad, enc, enc_size);
    memcpy(bad + 8, "JUNK", 4);  // clobber the WEBP fourcc
    int bw = 0, bh = 0;
    uint8_t* bdec = WebPDecodeRGBA(bad, enc_size, &bw, &bh);
    report("corrupt_input_rejected", rej_trunc && bdec == NULL);
    WebPFree(bdec);
    free(bad);
  } else {
    report("corrupt_input_rejected", 0);
  }

  // 8. version sanity: decoder + encoder ABI versions are coherent
  int dv = WebPGetDecoderVersion(), ev = WebPGetEncoderVersion();
  report("abi_versions_sane", dv > 0 && ev > 0 && (dv >> 16) >= 1);

  WebPFree(enc);
  printf("SELFTEST SUMMARY passed=%d failed=%d\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
