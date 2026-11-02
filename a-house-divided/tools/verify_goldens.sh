#!/usr/bin/env bash
# Rebuild hd-sim in release and verify every committed golden replay. Used on every OS in CI.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ ! -d shells/sim-cli ]; then
  echo "verify_goldens: sim-cli not present yet, nothing to verify"
  exit 0
fi
cargo build --release --locked -p hd-sim
BIN=target/release/hd-sim
[ -x "$BIN" ] || BIN=target/release/hd-sim.exe
shopt -s nullglob
files=(qc/golden-replays/*.hdrp)
if [ ${#files[@]} -eq 0 ]; then echo "verify_goldens: no goldens committed"; exit 1; fi
for f in "${files[@]}"; do
  echo "verifying $f"
  "$BIN" verify "$f"
done
echo "verify_goldens: ${#files[@]} replays verified"
