#!/usr/bin/env bash
# Verify a downloaded Dino 8 release artifact.
#
#   scripts/verify-release.sh [options] <file-or-directory>...
#
# For every installer/package (.exe .zip .deb .tar.gz .dmg) given (or found in
# a given directory) it checks, in this order:
#
#   1. SHA-256   the file matches <file>.sha256 and/or SHA256SUMS next to it.
#                Proves: the bytes you have are the bytes that were published
#                (corruption / truncated download). On its own it does NOT
#                prove who published them - an attacker who can replace the
#                file can replace the checksum file too.
#   2. cosign    <file>.sigstore.json is a valid keyless Sigstore signature
#                made by this repository's dino8-app.yml workflow run, via
#                GitHub's OIDC identity (issuer
#                https://token.actions.githubusercontent.com). Proves: the file
#                was signed by a run of that workflow in that repository, and
#                has not been altered since. It does NOT prove the source code
#                is safe or that a real-world legal entity stands behind it.
#   3. Authenticode (.exe only, informational) if the file carries a Windows
#                code-signing signature and osslsigncode is installed, report
#                it. Windows users should run verify-release.ps1, which asks
#                Windows itself to validate the certificate chain.
#   4. attestation (only with --attestation) `gh attestation verify`, i.e. the
#                GitHub build-provenance attestation. Needs gh, logged in.
#
# It fails closed: a missing checksum, a missing bundle or a missing cosign
# binary is a FAILURE, unless you opt out with --skip-cosign.
#
# Options:
#   --repo OWNER/NAME        repository whose workflow must have signed the file
#                            (default: $DINO8_REPO or mlegere9789-collab/miniature-pancake)
#   --identity-regexp RE     override the whole certificate-identity regexp
#                            (e.g. to pin to refs/heads/main or a tag)
#   --skip-cosign            check checksums (and Authenticode) only
#   --attestation            also run `gh attestation verify --repo ...`
#   --require-authenticode   an unsigned .exe is a failure (default: informational)
#   -h, --help
#
# Exit status: 0 every requested check passed, 1 at least one failed, 2 usage.
set -u

repo=${DINO8_REPO:-mlegere9789-collab/miniature-pancake}
identity_re=""
issuer="https://token.actions.githubusercontent.com"
skip_cosign=0
want_attestation=0
require_authenticode=0

usage() { sed -n '2,/^set -u/p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; }

paths=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --repo) [ "$#" -ge 2 ] || { echo "--repo needs a value" >&2; exit 2; }; repo=$2; shift 2 ;;
    --identity-regexp) [ "$#" -ge 2 ] || { echo "--identity-regexp needs a value" >&2; exit 2; }; identity_re=$2; shift 2 ;;
    --skip-cosign) skip_cosign=1; shift ;;
    --attestation) want_attestation=1; shift ;;
    --require-authenticode) require_authenticode=1; shift ;;
    -h|--help) usage; exit 0 ;;
    --) shift; while [ "$#" -gt 0 ]; do paths+=("$1"); shift; done ;;
    -*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    *) paths+=("$1"); shift ;;
  esac
done
[ "${#paths[@]}" -gt 0 ] || paths=(.)

if [ -z "$identity_re" ]; then
  # Escape regexp metacharacters that legitimately occur in a repo slug.
  repo_re=$(printf '%s' "$repo" | sed -e 's/[.[\*^$()+?{|]/\\&/g')
  identity_re="^https://github\\.com/${repo_re}/\\.github/workflows/dino8-app\\.yml@refs/(heads|tags)/.+\$"
fi

if [ -t 1 ]; then
  c_ok=$'\033[32m'; c_bad=$'\033[31m'; c_dim=$'\033[33m'; c_off=$'\033[0m'
else
  c_ok=""; c_bad=""; c_dim=""; c_off=""
fi
n_ok=0; n_fail=0; n_info=0
ok()   { printf '  %s[ OK ]%s %s\n' "$c_ok" "$c_off" "$*"; n_ok=$((n_ok + 1)); }
fail() { printf '  %s[FAIL]%s %s\n' "$c_bad" "$c_off" "$*"; n_fail=$((n_fail + 1)); }
info() { printf '  %s[INFO]%s %s\n' "$c_dim" "$c_off" "$*"; n_info=$((n_info + 1)); }

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  elif command -v openssl >/dev/null 2>&1; then openssl dgst -sha256 -r "$1" | cut -d' ' -f1
  else return 1
  fi
}

# expected hash for $1 (basename) from checksum file $2, in "<hash>  <name>" or
# "<hash> *<name>" format. Prints nothing if not listed.
expected_from() {
  awk -v n="$1" '{ h=$1; $1=""; sub(/^ +\*?/, ""); if ($0 == n) { print tolower(h); exit } }' "$2"
}

check_checksums() {
  local f=$1 dir name actual exp found=0
  dir=$(dirname "$f"); name=$(basename "$f")
  actual=$(sha256_of "$f") || { fail "no sha256 tool found (need sha256sum, shasum or openssl)"; return; }
  if [ -f "$f.sha256" ]; then
    found=1
    exp=$(expected_from "$name" "$f.sha256")
    if [ -z "$exp" ]; then fail "$name.sha256 does not list $name"
    elif [ "$exp" = "$actual" ]; then ok "SHA-256 matches $name.sha256 ($actual)"
    else fail "SHA-256 MISMATCH vs $name.sha256: expected $exp, file is $actual"; fi
  fi
  if [ -f "$dir/SHA256SUMS" ]; then
    exp=$(expected_from "$name" "$dir/SHA256SUMS")
    if [ -z "$exp" ]; then
      [ "$found" -eq 1 ] || fail "SHA256SUMS does not list $name"
    else
      found=1
      if [ "$exp" = "$actual" ]; then ok "SHA-256 matches SHA256SUMS ($actual)"
      else fail "SHA-256 MISMATCH vs SHA256SUMS: expected $exp, file is $actual"; fi
    fi
  fi
  [ "$found" -eq 1 ] || fail "no checksum file for $name (need $name.sha256 or SHA256SUMS in the same folder)"
}

cosign_verify() {
  local f=$1 label=$2 name out
  name=$(basename "$f")
  if [ ! -f "$f.sigstore.json" ]; then
    fail "$label: no signature bundle $name.sigstore.json next to the file"
    return
  fi
  # The release workflow writes the new-style Sigstore bundle. cosign 2.4.x
  # needs --new-bundle-format to read it; cosign 3.x reads it by default (and
  # may no longer list the flag), so only pass the flag when cosign knows it.
  # shellcheck disable=SC2086
  if out=$(cosign verify-blob $new_bundle_flag --bundle "$f.sigstore.json" \
        --certificate-identity-regexp "$identity_re" \
        --certificate-oidc-issuer "$issuer" "$f" 2>&1); then
    ok "$label: cosign signature valid; signed by a $repo dino8-app.yml workflow run (issuer $issuer)"
  else
    fail "$label: cosign verification FAILED"
    printf '%s\n' "$out" | sed 's/^/           /'
  fi
}

check_authenticode() {
  local f=$1 name out
  name=$(basename "$f")
  if ! command -v osslsigncode >/dev/null 2>&1; then
    if [ "$require_authenticode" -eq 1 ]; then fail "Authenticode: osslsigncode not installed, cannot check $name (use verify-release.ps1 on Windows)"
    else info "Authenticode: skipped (osslsigncode not installed; on Windows run verify-release.ps1)"; fi
    return
  fi
  if out=$(osslsigncode verify -in "$f" 2>&1) && printf '%s' "$out" | grep -q 'Signature verification: ok'; then
    ok "Authenticode: signature present and verifies against the system trust store"
    printf '%s\n' "$out" | grep -E 'Subject:|Issuer :' | head -2 | sed 's/^/           /'
  elif printf '%s' "$out" | grep -qi 'no signature found'; then
    if [ "$require_authenticode" -eq 1 ]; then fail "Authenticode: $name is not Authenticode-signed"
    else info "Authenticode: $name is not Authenticode-signed (expected until a code-signing certificate is configured; see docs/CODE_SIGNING.md)"; fi
  else
    # Signature exists but this machine could not validate the chain, or it is invalid.
    fail "Authenticode: signature present but NOT validated here (bad signature, or the chain is not trusted by this machine's CA bundle). Re-check on Windows with verify-release.ps1."
    printf '%s\n' "$out" | tail -5 | sed 's/^/           /'
  fi
}

check_attestation() {
  local f=$1 out
  if ! command -v gh >/dev/null 2>&1; then fail "attestation: gh CLI not installed"; return; fi
  if out=$(gh attestation verify "$f" --repo "$repo" 2>&1); then
    ok "attestation: GitHub build-provenance attestation verified for $repo"
  else
    fail "attestation: gh attestation verify FAILED (also fails if gh is not logged in)"
    printf '%s\n' "$out" | tail -5 | sed 's/^/           /'
  fi
}

# Collect artifacts.
files=()
for p in "${paths[@]}"; do
  if [ -d "$p" ]; then
    while IFS= read -r -d '' f; do files+=("$f"); done < <(find "$p" -maxdepth 1 -type f \( -name '*.exe' -o -name '*.zip' -o -name '*.deb' -o -name '*.tar.gz' -o -name '*.dmg' \) -print0 | sort -z)
  elif [ -f "$p" ]; then files+=("$p")
  else echo "no such file or directory: $p" >&2; exit 2
  fi
done
if [ "${#files[@]}" -eq 0 ]; then echo "no release artifacts (.exe .zip .deb .tar.gz .dmg) found in: ${paths[*]}" >&2; exit 2; fi

echo "Dino 8 release verification"
echo "  repository : $repo"
if [ "$skip_cosign" -eq 0 ]; then echo "  identity   : $identity_re"; fi

cosign_ready=0
new_bundle_flag=""
if [ "$skip_cosign" -eq 0 ]; then
  if command -v cosign >/dev/null 2>&1; then
    cosign_ready=1
    if cosign verify-blob --help 2>&1 | grep -q -e '--new-bundle-format'; then new_bundle_flag="--new-bundle-format"; fi
  fi
fi

seen_dirs=""
for f in "${files[@]}"; do
  echo
  echo "$f"
  check_checksums "$f"
  if [ "$skip_cosign" -eq 1 ]; then
    info "cosign: skipped (--skip-cosign); checksums alone do not prove who published the file"
  elif [ "$cosign_ready" -eq 1 ]; then
    cosign_verify "$f" "cosign"
    d=$(cd "$(dirname "$f")" && pwd)
    case "$seen_dirs" in *"|$d|"*) ;; *)
      seen_dirs="$seen_dirs|$d|"
      if [ -f "$d/SHA256SUMS" ] && [ -f "$d/SHA256SUMS.sigstore.json" ]; then cosign_verify "$d/SHA256SUMS" "cosign (SHA256SUMS)"; fi ;;
    esac
  else
    fail "cosign: not installed - cannot check the signature (install from https://docs.sigstore.dev/cosign/system_config/installation/ or pass --skip-cosign)"
  fi
  case "$f" in *.exe) check_authenticode "$f" ;; esac
  if [ "$want_attestation" -eq 1 ]; then check_attestation "$f"; fi
done

echo
echo "Summary: $n_ok passed, $n_fail failed, $n_info informational"
if [ "$n_fail" -gt 0 ]; then
  echo "RESULT: NOT VERIFIED - do not install this file."
  exit 1
fi
echo "RESULT: all requested checks passed."
if [ "$skip_cosign" -eq 1 ]; then echo "(Signature check was skipped: this confirms integrity only, not origin.)"; fi
exit 0
