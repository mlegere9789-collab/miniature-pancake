#!/usr/bin/env bash
# Generates include/dino8_probe.h from the Rust sources with cbindgen.
#   scripts/gen_header.sh          regenerate the committed header
#   scripts/gen_header.sh --check  fail if the committed header is out of date (use in CI)
# Needs cbindgen on PATH: cargo install cbindgen --locked
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$here/include/dino8_probe.h"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
# cbindgen prints one "Skip <item> (not pub)" notice per private item; hide those, keep real warnings.
cbindgen --config "$here/cbindgen.toml" --crate dino8-ffi-probe --output "$tmp" "$here" 2> >(grep -v '^WARN: Skip' >&2)
if [[ "${1:-}" == "--check" ]]; then
  if ! diff -u "$out" "$tmp"; then
    echo "include/dino8_probe.h is out of date: run scripts/gen_header.sh" >&2
    exit 1
  fi
  echo "header is up to date"
else
  mkdir -p "$(dirname "$out")"
  cp "$tmp" "$out"
  echo "wrote $out"
fi
