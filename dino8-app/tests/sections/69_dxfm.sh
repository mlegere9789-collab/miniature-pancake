# ---------------------------------------------------------------------------
# DXFM_: DXF export of multi-line text and of text/dimension styles, read back by an independent parser (tests/dxfm_check.py: its own
# group-code reader, plus ezdxf when importable or installable). An MTEXT is opened from tests/dxfm_fixture.dxf, an annotation style
# Arch is made, a Text and a DimLinear are made in it, everything is exported (tests/dxfm_script.txt). Before: the MTEXT left as 6+
# baked glyph polylines (no MTEXT writer), STYLE only "Standard" (every TEXT named it whatever its annotation style), no DIMSTYLE
# table at all (DIMENSIONs named nothing). Closed-form expectations are in the checker's header.
# Run on its own with: tests/run_sections.sh -b <Dino8> dxfm
dxfm_dir="$TMPW/dxfm"
rm -rf "$dxfm_dir"; mkdir -p "$dxfm_dir"
cp "$HERE/dxfm_fixture.dxf" "$dxfm_dir/dxfm_fixture.dxf"
sed "s|@TMP@|$dxfm_dir|g" "$HERE/dxfm_script.txt" > "$dxfm_dir/script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  timeout 900 "$BIN" --smoke 60 --script "$dxfm_dir/script.txt" > "$dxfm_dir/app.txt" 2>&1 || true
else
  timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$dxfm_dir/script.txt" > "$dxfm_dir/app.txt" 2>&1 || true
fi
dxfm_ez=""
if ! python3 -I -c 'import ezdxf' >/dev/null 2>&1; then
  if timeout 180 python3 -m pip install --quiet --disable-pip-version-check --target "$dxfm_dir/ezpkg" ezdxf >/dev/null 2>&1; then dxfm_ez="$dxfm_dir/ezpkg"; fi
fi
if [ -f "$dxfm_dir/m1.dxf" ]; then
  python3 -I "$HERE/dxfm_check.py" "$dxfm_dir" "$dxfm_ez" > "$dxfm_dir/verdict.txt" 2>&1 || true
  cat "$dxfm_dir/verdict.txt"
  if grep -q '^FAIL' "$dxfm_dir/verdict.txt" || ! grep -q '^ok' "$dxfm_dir/verdict.txt"; then fail=1; fi
else
  echo "FAIL DXFM: the app did not write m1.dxf"
  tail -n 20 "$dxfm_dir/app.txt" | cut -c1-200
  fail=1
fi
