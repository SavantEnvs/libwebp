#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the behavioral oracle built by mayhem/build.sh.
#
# NOTE: upstream libwebp ships NO offline functional test suite — tests/ holds
# only FuzzTest-based fuzz targets that FetchContent google/fuzztest from the
# network at configure time (no `make check`, no ctest units). This oracle is
# AUTHORED (mayhem/webp_selftest.c): known-answer assertions over the public
# encode/decode API (bit-exact lossless round-trips, header parsing, corrupt
# input rejection). It counts PASS/FAIL lines from the runner's OUTPUT, so a
# neutered exit(0) binary (no output) fails the expected-count check.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

RUNNER=/mayhem/webp_selftest
EXPECTED=9   # discrete assertions webp_selftest.c must report

if [ ! -x "$RUNNER" ]; then
  echo "FATAL: $RUNNER missing — mayhem/build.sh must build it" >&2
  emit_ctrf "webp-selftest" 0 "$EXPECTED"
  exit 1
fi

out="$("$RUNNER" 2>&1)"; rc=$?
printf '%s\n' "$out"

passed=$(printf '%s\n' "$out" | grep -c '^PASS: ' || true)
failed=$(printf '%s\n' "$out" | grep -c '^FAIL: ' || true)

# Behavioral guard: every expected assertion must have actually REPORTED.
# A neutered runner (exit 0, no output) yields passed=0 -> missing counted failed.
if [ $(( passed + failed )) -ne "$EXPECTED" ] || [ "$rc" -ne 0 ]; then
  missing=$(( EXPECTED - passed - failed )); [ "$missing" -lt 0 ] && missing=0
  emit_ctrf "webp-selftest" "$passed" $(( failed + missing ))
  exit 1
fi

emit_ctrf "webp-selftest" "$passed" "$failed"
