# ---------------------------------------------------------------------------
# DXRT_: DXF round trip. tests/dxrt_script.txt builds lines on two layers, a circle, an arc, an open polyline, a rectangle with a solid
# hatch, an ellipse, an interpolated spline and four DIMENSIONs (linear, aligned, radius, diameter), exports rt_a.dxf, opens a new
# document, imports rt_a.dxf and exports rt_b.dxf. tests/dxrt_check.py (standard library only) checks rt_a.dxf against the AutoCAD DXF
# reference group codes by closed form, then that rt_b.dxf has the same entity counts per type, the same layers and the same key group
# values entity by entity. Text is not in the scene: an imported TEXT becomes glyph curves (Annotation=Text), not a live Text, so it
# leaves the second export as polylines (see the commit message).
# Run on its own with: tests/run_sections.sh -b <Dino8> dxrt
dxrt_dir="$TMPW/dxrt"
rm -rf "$dxrt_dir"; mkdir -p "$dxrt_dir"
sed "s|@TMP@|$dxrt_dir|g" "$HERE/dxrt_script.txt" > "$dxrt_dir/script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  timeout 900 "$BIN" --smoke 60 --script "$dxrt_dir/script.txt" > "$dxrt_dir/app.txt" 2>&1 || true
else
  timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$dxrt_dir/script.txt" > "$dxrt_dir/app.txt" 2>&1 || true
fi
if [ -f "$dxrt_dir/rt_a.dxf" ] && [ -f "$dxrt_dir/rt_b.dxf" ]; then
  python3 -I "$HERE/dxrt_check.py" "$dxrt_dir" > "$dxrt_dir/verdict.txt" 2>&1 || true
  cat "$dxrt_dir/verdict.txt"
  if grep -q '^FAIL' "$dxrt_dir/verdict.txt" || ! grep -q '^ok' "$dxrt_dir/verdict.txt"; then fail=1; fi
else
  echo "FAIL DXRT: the app did not write rt_a.dxf / rt_b.dxf"
  tail -n 20 "$dxrt_dir/app.txt" | cut -c1-200
  fail=1
fi
