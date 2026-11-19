#!/usr/bin/env bash
#
# mayhem/test.sh — run the known-answer selftest built by mayhem/build.sh (normal flags):
# loopback rendering levels/panning, buffer/IMA4 decode, error codes and EFX round-trips.
# Each check prints PASS/FAIL <name>; the expected check count is asserted so a no-op or
# truncated run fails.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

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

EXPECTED=25
BIN="$SRC/build-mayhem-test/oal_selftest"
if [ ! -x "$BIN" ]; then
  echo "missing $BIN — run mayhem/build.sh first" >&2
  emit_ctrf oal-selftest 0 "$EXPECTED"
  exit 1
fi
out=$("$BIN" 2>&1); rc=$?
echo "$out"
passed=$(grep -c '^PASS ' <<<"$out" || true)
failed=$(grep -c '^FAIL ' <<<"$out" || true)
missing=$(( EXPECTED - passed - failed ))
[ "$missing" -lt 0 ] && missing=0
# A check that never reported (crash, early exit) counts as failed, as does a non-zero exit.
failed=$(( failed + missing ))
[ "$rc" -ne 0 ] && [ "$failed" -eq 0 ] && failed=1
emit_ctrf oal-selftest "$passed" "$failed"
