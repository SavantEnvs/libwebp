#!/usr/bin/env bash
#
# mayhem/build.sh — build libwebp's fuzz harness (advanced_api_fuzzer) plus a
# behavioral self-test oracle. Runs inside the commit image as `mayhem` in /mayhem.
# The base image exports the build contract (CC, CXX, LIB_FUZZING_ENGINE,
# SANITIZER_FLAGS, DEBUG_FLAGS, STANDALONE_FUZZ_MAIN, SRC).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# limit allocation size to reduce spurious OOMs (matches upstream oss-fuzz build)
WEBP_DEFS="-DWEBP_MAX_IMAGE_SIZE=838860800"

# SanitizerCoverage instrumentation for the FUZZED code: -fsanitize=fuzzer at
# link only pulls in the libFuzzer runtime — edge coverage (edges_covered) needs
# every fuzzed object compiled with -fsanitize=fuzzer-no-link.
FUZZ_COV="-fsanitize=fuzzer-no-link"

# ---------------------------------------------------------------------------
# 1) SANITIZED static libwebp (the fuzzed code) — CMake, static, no tools.
# ---------------------------------------------------------------------------
cmake -S "$SRC" -B "$SRC/build" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $FUZZ_COV $DEBUG_FLAGS $WEBP_DEFS" \
  -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $FUZZ_COV $DEBUG_FLAGS" \
  -DBUILD_SHARED_LIBS=OFF \
  -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF \
  -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF \
  -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_EXTRAS=OFF \
  -DWEBP_BUILD_LIBWEBPMUX=ON >/dev/null
cmake --build "$SRC/build" -j"$MAYHEM_JOBS" --target webp webpdemux libwebpmux sharpyuv

SAN_LIBS=(
  "$SRC/build/libwebpmux.a"
  "$SRC/build/libwebpdemux.a"
  "$SRC/build/libwebp.a"
  "$SRC/build/libsharpyuv.a"
)

# ---------------------------------------------------------------------------
# 2) harnesses — fuzzer binary + standalone reproducer for each target.
# ---------------------------------------------------------------------------
HARNESSES=(advanced_api_fuzzer simple_api_fuzzer animation_api_fuzzer mux_demux_api_fuzzer)

$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o

for h in "${HARNESSES[@]}"; do
  $CC $SANITIZER_FLAGS $FUZZ_COV $DEBUG_FLAGS $WEBP_DEFS -I"$SRC" \
    -c "$SRC/mayhem/$h.c" -o "/tmp/$h.o"

  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE \
    "/tmp/$h.o" "${SAN_LIBS[@]}" \
    -o "/mayhem/$h"

  $CC $SANITIZER_FLAGS $DEBUG_FLAGS \
    /tmp/standalone_main.o "/tmp/$h.o" "${SAN_LIBS[@]}" \
    -o "/mayhem/$h-standalone"
done

# ---------------------------------------------------------------------------
# 3) TEST oracle build — a CLEAN, non-sanitized static libwebp so the
#    behavioral self-test is honest and fast. Left where mayhem/test.sh looks.
# ---------------------------------------------------------------------------
cmake -S "$SRC" -B "$SRC/build-tests" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
  -DBUILD_SHARED_LIBS=OFF \
  -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF \
  -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF \
  -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_EXTRAS=OFF \
  -DWEBP_BUILD_LIBWEBPMUX=OFF >/dev/null
cmake --build "$SRC/build-tests" -j"$MAYHEM_JOBS" --target webp sharpyuv

$CC -O2 $COVERAGE_FLAGS -I"$SRC/src" \
  "$SRC/mayhem/webp_selftest.c" \
  "$SRC/build-tests/libwebp.a" "$SRC/build-tests/libsharpyuv.a" \
  -lm -o /mayhem/webp_selftest

echo "build.sh: done"
