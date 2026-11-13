# ---------------------------------------------------------------------------
# DXFQ_: DXF export entity fidelity, read back by an independent parser (tests/dxfq_check.py: its own group-code reader, plus the
# third-party ezdxf package when it can be imported or installed). Scenario tests/dxfq_script.txt exports layers (linetype, off,
# locked), an object linetype, TEXT (height, rotation on a rotated plane), two ellipses, an interpolated and a control-point
# spline, five DIMENSIONs, a solid hatch and a rotated, scaled ANSI31 hatch (plus the same hatch as the app's own baked lines).
# Every expectation is closed form (see the checker's header). Before the fix the file had: layers always Continuous and no
# LTYPE/STYLE tables, ellipses as SPLINEs, DIMENSIONs without their geometry block (ezdxf's audit throws them away), and hatch
# pattern lines unrotated/unscaled (45 degrees / 0.125 instead of 75 / 1 for the scaled, rotated ANSI31).
# Run on its own with: tests/run_sections.sh -b <Dino8> dxfq
dxfq_dir="$TMPW/dxfq"
rm -rf "$dxfq_dir"; mkdir -p "$dxfq_dir"
sed "s|@TMP@|$dxfq_dir|g" "$HERE/dxfq_script.txt" > "$dxfq_dir/script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  timeout 900 "$BIN" --smoke 60 --script "$dxfq_dir/script.txt" > "$dxfq_dir/app.txt" 2>&1 || true
else
  timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$dxfq_dir/script.txt" > "$dxfq_dir/app.txt" 2>&1 || true
fi
dxfq_ez=""
if ! python3 -I -c 'import ezdxf' >/dev/null 2>&1; then
  if timeout 180 python3 -m pip install --quiet --disable-pip-version-check --target "$dxfq_dir/ezpkg" ezdxf >/dev/null 2>&1; then dxfq_ez="$dxfq_dir/ezpkg"; fi
fi
if [ -f "$dxfq_dir/q1.dxf" ] && [ -f "$dxfq_dir/q2_live.dxf" ] && [ -f "$dxfq_dir/q2_baked.dxf" ]; then
  python3 -I "$HERE/dxfq_check.py" "$dxfq_dir" "$dxfq_ez" > "$dxfq_dir/verdict.txt" 2>&1 || true
  cat "$dxfq_dir/verdict.txt"
  if grep -q '^FAIL' "$dxfq_dir/verdict.txt" || ! grep -q '^ok' "$dxfq_dir/verdict.txt"; then fail=1; fi
else
  echo "FAIL DXFQ: the app did not write q1.dxf / q2_live.dxf / q2_baked.dxf"
  tail -n 20 "$dxfq_dir/app.txt" | cut -c1-200
  fail=1
fi
