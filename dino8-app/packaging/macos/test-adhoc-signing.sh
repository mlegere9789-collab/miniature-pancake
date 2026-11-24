#!/usr/bin/env bash
# CI self-test of the macOS signing code path WITHOUT any certificate or secret.
#
#   test-adhoc-signing.sh <dmg>      macOS only; the "macos" job runs this on the
#                                    .dmg that CPack just built
#   test-adhoc-signing.sh --check-helpers
#                                    the platform-independent parts only (argument
#                                    handling, the scratch-copy guard of
#                                    sign-dmg.sh, the byte-flip helper); runs on
#                                    Linux too
#
# What it does with <dmg> (a COPY is signed, the original is never touched):
#   1. copies the .dmg into a scratch directory marked .dino8-test-signing-dir
#      and runs the repo's real packaging/macos/sign-dmg.sh in `adhoc` mode on
#      the copy (codesign --sign -, hardened runtime, the real entitlements file);
#   2. mounts the signed copy, extracts the app, and requires
#        codesign --verify --deep --strict          to succeed,
#        codesign -dv                               to report Signature=adhoc
#      (and no Developer ID authority), and the signed copy's hash to differ
#      from the original's;
#   3. TAMPER negatives on copies of the signed app: flipping one byte of the
#      main executable, and appending a byte to a sealed resource file, must each
#      make `codesign --verify --deep --strict` FAIL;
#   4. requires the original .dmg to be byte-identical afterwards.
#
# NOT done here, because it needs Apple credentials: Developer ID signing and
# notarization. An ad-hoc signature is not trusted by Gatekeeper (spctl is run
# informationally and is expected to reject).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
failures=0
pass() { printf '  PASS  %s\n' "$*"; }
fail() { printf '  FAIL  %s\n' "$*" >&2; failures=$((failures + 1)); }

sha_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# flip_byte <file> <offset>: XOR the byte at <offset> with 0xFF, in place.
flip_byte() {
  local f=$1 off=$2 old new
  old=$(dd if="$f" bs=1 skip="$off" count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')
  [ -n "$old" ] || { echo "flip_byte: offset $off is past the end of $f" >&2; return 1; }
  new=$(printf '%02x' $(( 0x$old ^ 0xff )))
  # shellcheck disable=SC2059  # the \xHH escape has to be part of the format string
  printf "\\x$new" | dd of="$f" bs=1 seek="$off" conv=notrunc 2>/dev/null
}

check_helpers() {
  local d out rc
  d=$(mktemp -d "${TMPDIR:-/tmp}/dino8-adhoc-selftest.XXXXXX")
  echo "helper self-test (platform independent)"

  # byte flip changes exactly one byte, and flipping again restores the file
  head -c 4096 /dev/zero | tr '\0' 'A' > "$d/blob"
  cp "$d/blob" "$d/blob.orig"
  flip_byte "$d/blob" 2000
  if [ "$(cmp -l "$d/blob.orig" "$d/blob" | wc -l | tr -d ' ')" = "1" ] && [ "$(sha_of "$d/blob")" != "$(sha_of "$d/blob.orig")" ]; then
    pass "flip_byte changes exactly one byte"; else fail "flip_byte did not change exactly one byte"; fi
  flip_byte "$d/blob" 2000
  if cmp -s "$d/blob.orig" "$d/blob"; then pass "flip_byte is its own inverse"; else fail "flip_byte twice did not restore the file"; fi
  if flip_byte "$d/blob" 999999 2>/dev/null; then fail "flip_byte past EOF should fail"; else pass "flip_byte past EOF fails"; fi

  # sign-dmg.sh argument handling and the scratch-copy guard
  : > "$d/fake.dmg"
  rc=0; out=$("$here/sign-dmg.sh" 2>&1) || rc=$?
  if [ "$rc" = 2 ]; then pass "sign-dmg.sh without arguments exits 2 (usage)"; else fail "sign-dmg.sh without arguments exited $rc"; fi
  rc=0; out=$("$here/sign-dmg.sh" bogus "$d/fake.dmg" 2>&1) || rc=$?
  if [ "$rc" = 2 ] && printf '%s' "$out" | grep -q 'unknown mode'; then pass "sign-dmg.sh rejects an unknown mode"; else fail "sign-dmg.sh unknown mode: exit $rc: $out"; fi
  rc=0; out=$(env -u APPLE_SIGNING_IDENTITY "$here/sign-dmg.sh" adhoc "$d/fake.dmg" 2>&1) || rc=$?
  if [ "$rc" = 2 ] && printf '%s' "$out" | grep -q 'refusing to sign'; then pass "adhoc mode refuses a .dmg outside a marked scratch directory"; else fail "adhoc guard: exit $rc: $out"; fi
  rc=0; out=$(env -u APPLE_SIGNING_IDENTITY "$here/sign-dmg.sh" sign "$d/fake.dmg" 2>&1) || rc=$?
  if [ "$rc" != 0 ] && printf '%s' "$out" | grep -q 'APPLE_SIGNING_IDENTITY is empty'; then pass "production sign mode still requires APPLE_SIGNING_IDENTITY"; else fail "production sign without identity: exit $rc: $out"; fi
  if [ "$(uname -s)" != Darwin ]; then
    mkdir "$d/scratch"; : > "$d/scratch/fake.dmg"; : > "$d/scratch/.dino8-test-signing-dir"
    rc=0; out=$(env -u APPLE_SIGNING_IDENTITY "$here/sign-dmg.sh" adhoc "$d/scratch/fake.dmg" 2>&1) || rc=$?
    if [ "$rc" != 0 ] && [ "$rc" != 2 ] && ! printf '%s' "$out" | grep -q 'refusing to sign' && printf '%s' "$out" | grep -q 'AD-HOC TEST SIGNING'; then
      pass "adhoc mode passes the guard in a marked scratch directory (then stops at hdiutil, which does not exist here)"
    else fail "adhoc in marked directory: exit $rc: $out"; fi
  fi
  rm -rf "$d"
  [ "$failures" -eq 0 ] || { echo "helper self-test FAILED ($failures)" >&2; return 1; }
  echo "helper self-test passed"
}

if [ "${1:-}" = "--check-helpers" ]; then check_helpers; exit $?; fi

dmg=${1:-}
[ -n "$dmg" ] && [ -f "$dmg" ] || { echo "usage: $0 <dmg> | --check-helpers" >&2; exit 2; }
[ "$(uname -s)" = Darwin ] || { echo "$0 needs macOS (codesign, hdiutil)" >&2; exit 2; }
dmg=$(cd "$(dirname "$dmg")" && pwd)/$(basename "$dmg")

check_helpers

work=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/dino8-adhoc-test.XXXXXX")
mnt=$work/mnt
cleanup() { hdiutil detach "$mnt" -quiet 2>/dev/null || true; rm -rf "$work"; }
trap cleanup EXIT

echo
echo "################################################################"
echo "# AD-HOC TEST SIGNING of a COPY of $(basename "$dmg")"
echo "# codesign --sign - : NOT Developer-ID signed, NOT notarized (no Apple credentials),"
echo "# not trusted by Gatekeeper. The .dmg that gets published stays exactly as CPack built it."
echo "################################################################"

orig_hash=$(sha_of "$dmg")
mkdir -p "$work/copy" "$mnt"
: > "$work/copy/.dino8-test-signing-dir"
cp "$dmg" "$work/copy/$(basename "$dmg")"
copy="$work/copy/$(basename "$dmg")"

"$here/sign-dmg.sh" adhoc "$copy"

echo "--- checks on the ad-hoc signed copy"
if [ "$(sha_of "$copy")" != "$orig_hash" ]; then pass "signed copy differs from the original image"; else fail "signed copy is identical to the original image - nothing was signed"; fi
if codesign --verify --verbose=2 "$copy"; then pass "codesign --verify on the signed .dmg copy"; else fail "codesign --verify on the signed .dmg copy"; fi

# the image carries a license agreement: answer it non-interactively (no terminal on a CI runner)
printf "Y\n" | PAGER=true hdiutil attach "$copy" -readonly -nobrowse -noverify -mountpoint "$mnt" >/dev/null
ditto "$mnt/Dino8.app" "$work/app"
hdiutil detach "$mnt" -quiet

if codesign --verify --deep --strict --verbose=2 "$work/app"; then pass "codesign --verify --deep --strict on the app"; else fail "codesign --verify --deep --strict on the app"; fi
details=$(codesign -dv --verbose=4 "$work/app" 2>&1 || true)
printf '%s\n' "$details" | sed 's/^/           /'
if printf '%s\n' "$details" | grep -q 'Signature=adhoc'; then pass "codesign -dv reports Signature=adhoc"; else fail "codesign -dv does not report Signature=adhoc"; fi
if printf '%s\n' "$details" | grep -q 'Authority=Developer ID'; then fail "unexpected Developer ID authority on an ad-hoc test signature"; else pass "no Developer ID authority (not a trusted signature)"; fi

echo "--- Gatekeeper (informational; an ad-hoc signature is expected to be REJECTED)"
spctl --assess --type execute --verbose=2 "$work/app" 2>&1 | sed 's/^/           /' || true

main="$work/app/Contents/MacOS/Dino8"
echo "--- tamper negative 1: flip one byte of the main executable"
ditto "$work/app" "$work/app_t1"
size=$(wc -c < "$work/app_t1/Contents/MacOS/Dino8" | tr -d ' ')
if [ "$size" -gt 65536 ] && flip_byte "$work/app_t1/Contents/MacOS/Dino8" $((size / 4)); then
  if codesign --verify --deep --strict --verbose=2 "$work/app_t1" 2>"$work/t1.err"; then
    fail "tampered executable still verifies"
  else
    pass "tampered executable fails codesign --verify: $(tail -1 "$work/t1.err")"
  fi
else
  fail "could not tamper with $main (size $size)"
fi

echo "--- tamper negative 2: modify a sealed resource file"
ditto "$work/app" "$work/app_t2"
res=$(find "$work/app_t2/Contents/Resources" -type f 2>/dev/null | sort | head -1)
if [ -n "$res" ]; then
  printf 'x' >> "$res"
  if codesign --verify --deep --strict --verbose=2 "$work/app_t2" 2>"$work/t2.err"; then
    fail "tampered resource ${res#"$work"/} still verifies"
  else
    pass "tampered resource fails codesign --verify: $(tail -1 "$work/t2.err")"
  fi
else
  echo "  SKIP  no file under Contents/Resources to tamper with"
fi

if [ "$(sha_of "$dmg")" = "$orig_hash" ]; then pass "original .dmg is byte-identical (published image stays as CPack built it)"; else fail "ORIGINAL .dmg WAS MODIFIED"; fi

echo
if [ "$failures" -gt 0 ]; then echo "macOS ad-hoc signing self-test FAILED ($failures)" >&2; exit 1; fi
echo "macOS ad-hoc signing self-test passed (ad-hoc, NOT Developer ID, NOT notarized; notarization is skipped: it needs Apple credentials)."
