#!/usr/bin/env bash
# Negative / tamper test of the release tooling, with NO release and NO network:
#
#   scripts/test-verify-release.sh
#
# Builds a small fixture of fake artifacts, stages it with the real
# scripts/stage-release.sh (SHA256SUMS + <file>.sha256), then runs the real
# scripts/verify-release.sh on it:
#
#   * positive control: the untouched fixture passes (--skip-cosign, since there
#     is no Sigstore signature to check offline);
#   * a file modified after staging           -> verify-release.sh must exit 1;
#   * a wrong SHA256SUMS entry (alone)        -> must exit 1;
#   * a wrong <file>.sha256 entry (alone)     -> must exit 1;
#   * a file missing from SHA256SUMS          -> must exit 1;
#   * no checksum file at all                 -> must exit 1;
#   * without --skip-cosign and without a signature bundle -> must exit 1
#     (fails closed), and with a garbage bundle -> must exit 1 as well when a
#     cosign binary is installed (otherwise that case is reported as skipped);
#   * stage-release.sh itself refuses duplicate names and an empty input.
#
# A verifier that cannot say "no" proves nothing; this is the "no".
# Exit status: 0 all expectations held, 1 otherwise.
set -u

here=$(cd "$(dirname "$0")" && pwd)
verify="$here/verify-release.sh"
stage="$here/stage-release.sh"
work=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/dino8-verify-test.XXXXXX")
trap 'rm -rf "$work"' EXIT

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  PASS  %s\n' "$*"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL  %s\n' "$*" >&2; }

sha_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# flip one byte (XOR 0xFF) at offset $2 of file $1
flip_byte() {
  local f=$1 off=$2 old new
  old=$(dd if="$f" bs=1 skip="$off" count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')
  new=$(printf '%02x' $(( 0x$old ^ 0xff )))
  # shellcheck disable=SC2059  # the \xHH escape has to be part of the format string
  printf "\\x$new" | dd of="$f" bs=1 seek="$off" conv=notrunc 2>/dev/null
}

# Fresh staged fixture in $1: two artifacts + SHA256SUMS + sidecars.
make_release() {
  local rel=$1 art="$work/art.$$.$RANDOM"
  mkdir -p "$art/Dino8-windows-installer" "$art/Dino8-linux"
  head -c 20000 /dev/urandom > "$art/Dino8-windows-installer/Dino8Setup-0.0.0-test.exe"
  head -c 30000 /dev/urandom > "$art/Dino8-linux/dino8_0.0.0-test_amd64.deb"
  rm -rf "$rel"
  "$stage" "$art" "$rel" >/dev/null 2>&1 || { echo "fixture staging failed" >&2; exit 1; }
  rm -rf "$art"
}

# expect_fail <label> <expected-text-or-empty> <verify args...>
expect_fail() {
  local label=$1 text=$2 out rc=0
  shift 2
  out=$("$verify" "$@" 2>&1) || rc=$?
  if [ "$rc" -ne 1 ]; then fail "$label: expected exit 1, got $rc"; printf '%s\n' "$out" | sed 's/^/           /' >&2; return; fi
  if [ -n "$text" ] && ! printf '%s' "$out" | grep -q -- "$text"; then
    fail "$label: exit 1 but the output does not mention '$text'"; printf '%s\n' "$out" | sed 's/^/           /' >&2; return
  fi
  if ! printf '%s' "$out" | grep -q 'NOT VERIFIED'; then fail "$label: exit 1 but no 'NOT VERIFIED' verdict printed"; return; fi
  pass "$label: verifier exits 1 (NOT VERIFIED)"
}

echo "== fixture"
rel="$work/rel"
make_release "$rel"
for f in "$rel"/*; do echo "   ${f##*/}"; done
exe="$rel/Dino8Setup-0.0.0-test.exe"
deb="$rel/dino8_0.0.0-test_amd64.deb"

echo "== stage-release.sh edge cases"
mkdir -p "$work/dup/a" "$work/dup/b" "$work/empty"
echo x > "$work/dup/a/same.zip"; echo y > "$work/dup/b/same.zip"
if "$stage" "$work/dup" "$work/dup-out" >/dev/null 2>&1; then fail "stage-release.sh accepted two artifacts with the same name"; else pass "stage-release.sh rejects duplicate artifact names"; fi
if "$stage" "$work/empty" "$work/empty-out" >/dev/null 2>&1; then fail "stage-release.sh accepted an empty input"; else pass "stage-release.sh rejects an input with no installers"; fi
if [ -s "$rel/SHA256SUMS" ] && [ "$(wc -l < "$rel/SHA256SUMS" | tr -d ' ')" = 2 ]; then pass "SHA256SUMS lists both fixture artifacts"; else fail "SHA256SUMS is not the expected 2 lines"; fi

echo "== positive control (must PASS, otherwise the negatives below prove nothing)"
rc=0; out=$("$verify" --skip-cosign "$rel" 2>&1) || rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'all requested checks passed'; then pass "untouched fixture verifies (checksums only)"; else fail "untouched fixture did not verify (exit $rc)"; printf '%s\n' "$out" | sed 's/^/           /' >&2; fi

echo "== tamper: modified file"
make_release "$rel"; flip_byte "$deb" 15000
expect_fail "one flipped byte in the .deb" "MISMATCH" --skip-cosign "$rel"
make_release "$rel"; printf 'x' >> "$exe"
expect_fail "one byte appended to the .exe" "MISMATCH" --skip-cosign "$rel"
make_release "$rel"; : > "$deb"
expect_fail "file truncated to zero bytes" "MISMATCH" --skip-cosign "$deb"

echo "== tamper: wrong checksum entries"
make_release "$rel"; rm -f "$rel"/*.sha256
bad=$(sha_of "$exe"); bad="0${bad#?}"; [ "$bad" != "$(sha_of "$exe")" ] || bad="1${bad#?}"
sed "s/^[0-9a-f]\{64\}\(  Dino8Setup-0.0.0-test.exe\)\$/$bad\1/" "$rel/SHA256SUMS" > "$rel/SHA256SUMS.new" && mv "$rel/SHA256SUMS.new" "$rel/SHA256SUMS"
grep -q "^$bad" "$rel/SHA256SUMS" || fail "fixture edit failed: wrong SHA256SUMS entry not written"
expect_fail "wrong entry in SHA256SUMS only" "MISMATCH vs SHA256SUMS" --skip-cosign "$exe"

make_release "$rel"; rm -f "$rel/SHA256SUMS"
printf '%s  %s\n' "$bad" "Dino8Setup-0.0.0-test.exe" > "$exe.sha256"
expect_fail "wrong entry in <file>.sha256 only" "MISMATCH vs Dino8Setup-0.0.0-test.exe.sha256" --skip-cosign "$exe"

make_release "$rel"; rm -f "$rel"/*.sha256
grep -v 'Dino8Setup' "$rel/SHA256SUMS" > "$rel/SHA256SUMS.new" && mv "$rel/SHA256SUMS.new" "$rel/SHA256SUMS"
expect_fail "file missing from SHA256SUMS" "does not list" --skip-cosign "$exe"

make_release "$rel"; rm -f "$rel"/*.sha256 "$rel/SHA256SUMS"
expect_fail "no checksum file at all" "no checksum file" --skip-cosign "$exe"

make_release "$rel"; printf '%s  %s\n' "$(sha_of "$exe")" "some-other-name.exe" > "$exe.sha256"; rm -f "$rel/SHA256SUMS"
expect_fail "<file>.sha256 that names a different file" "does not list" --skip-cosign "$exe"

echo "== fails closed on the signature (checksums are intact here)"
make_release "$rel"
expect_fail "no .sigstore.json bundle (no --skip-cosign)" "" "$rel"
if command -v cosign >/dev/null 2>&1; then
  make_release "$rel"
  for f in "$exe" "$deb"; do echo '{"not":"a sigstore bundle"}' > "$f.sigstore.json"; done
  expect_fail "garbage .sigstore.json (cosign installed)" "cosign verification FAILED" "$rel"
else
  echo "  SKIP  garbage-bundle case: cosign is not installed here (the CI job installs it)"
fi

echo
echo "$checks checks, $failures failed"
if [ "$failures" -gt 0 ]; then echo "verify-release negative test: FAILED" >&2; exit 1; fi
echo "verify-release negative test: passed (the verifier rejects modified files, wrong checksums and missing signatures)"
