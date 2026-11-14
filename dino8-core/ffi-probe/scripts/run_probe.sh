#!/usr/bin/env bash
# Builds the Rust staticlib and the C++ probe WITHOUT CMake (the "plain script" path) and runs it.
#
#   scripts/run_probe.sh [plain|asan|tsan|all] [probe_test options...]
#
#   plain  optimized build, no sanitizer
#   asan   AddressSanitizer + UndefinedBehaviorSanitizer (UB findings abort the run)
#   tsan   ThreadSanitizer (the Rust side is not instrumented; see the ADR)
#   all    plain, asan and tsan in turn
#
# Environment: CXX (default c++), CARGO_BUILD_JOBS (default 2), CARGO_TARGET_DIR (default
# ./target), D8_PROBE_OUT (default $CARGO_TARGET_DIR/probe-bin). Extra arguments go to the program,
# for example: scripts/run_probe.sh asan --ops 200000
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-2}"
target="${CARGO_TARGET_DIR:-$here/target}"
out="${D8_PROBE_OUT:-$target/probe-bin}"
cxx="${CXX:-c++}"
mode="${1:-plain}"
[[ $# -gt 0 ]] && shift
probe_args=("$@")

cargo build --release --manifest-path "$here/Cargo.toml" --target-dir "$target"
lib="$target/release/libdino8_ffi_probe.a"
# System libraries the Rust staticlib needs (Corrosion reads the same list from rustc).
native="$(cargo rustc --release --lib --crate-type staticlib --manifest-path "$here/Cargo.toml" \
  --target-dir "$target" -- --print native-static-libs 2>&1 | sed -n 's/.*native-static-libs: //p' | tail -1)"
native="${native:--lgcc_s -lutil -lrt -lpthread -lm -ldl -lc}"
mkdir -p "$out"

build_and_run() {
  local name="$1"; shift
  local flags=("$@")
  "$cxx" -std=c++17 -g -Wall -Wextra -I"$here/include" "${flags[@]}" \
    "$here/cpp/probe_test.cpp" "$lib" $native -o "$out/probe_$name"
  echo "== running probe_$name"
  ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}" \
  UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}" \
  TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:suppressions=$here/scripts/tsan.supp}" \
    "$out/probe_$name" --dir "$out" "${probe_args[@]}"
}

run_one() {
  case "$1" in
    plain) build_and_run plain -O2 ;;
    asan)  build_and_run asan -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer ;;
    tsan)  build_and_run tsan -O1 -fsanitize=thread -fno-omit-frame-pointer ;;
    *) echo "unknown mode $1" >&2; exit 2 ;;
  esac
}

if [[ "$mode" == all ]]; then
  for m in plain asan tsan; do run_one "$m"; done
else
  run_one "$mode"
fi
