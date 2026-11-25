# ---------------------------------------------------------------------------
# DXLT_: DXF linetypes in both directions (dxflt_export_script.txt, dxflt_import_script.txt), checked by an independent stdlib DXF parser
# (dxf_check.py). ExportDxf wrote only "Continuous" (no LTYPE table, no entity linetype) and ImportDxf ignored LTYPE and group 6.
#   export  : LTYPE table (ByBlock / ByLayer / Continuous + Dashed 5,-2; Center 10,-2,2,-2; Hidden 2,-2; Cut 6,-1.5,0.5,-1.5 with total length and
#             element count), layer Walls = Hidden, layer Default = Continuous, the Cut line carries group 6 = Cut, ByLayer lines none
#   import  : the hand-built fixture defines HIDDEN 6.35,3.175 / CENTER 31.75,6.35,6.35,6.35 / DOT 0.635,6.35 (a dot becomes a dash of 0.1 x the
#             smallest element) and not the complex GAS_LINE; Print writes each line's real stroke-dasharray
#   reexport: the imported drawing exported again carries those patterns under unique names and the same layer / entity assignments
# ---------------------------------------------------------------------------
mkdir -p "$TMPW/dxlt"
if python3 "$HERE/dxf_check.py" fixture "$TMPW/dxlt/lt_fixture.dxf" >/dev/null; then echo "ok   DXLT_fixture: dxf_check.py wrote the hand-built LTYPE fixture"; else echo "FAIL DXLT_fixture: could not write lt_fixture.dxf"; fail=1; fi
sed "s|@TMP@|$TMPW/dxlt|g" "$HERE/dxflt_export_script.txt" > "$TMPW/dxflt_export_script.txt"
sed "s|@TMP@|$TMPW/dxlt|g" "$HERE/dxflt_import_script.txt" > "$TMPW/dxflt_import_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DXE="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/dxflt_export_script.txt" 2>&1)" || { echo "$DXE" | tail -20; echo "FAIL DXLT_run: export script exited non-zero"; fail=1; DXE=""; }
  DXI="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/dxflt_import_script.txt" 2>&1)" || { echo "$DXI" | tail -20; echo "FAIL DXLT_run: import script exited non-zero"; fail=1; DXI=""; }
else
  DXE="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxflt_export_script.txt" 2>&1)" || { echo "$DXE" | tail -20; echo "FAIL DXLT_run: export script exited non-zero"; fail=1; DXE=""; }
  DXI="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxflt_import_script.txt" 2>&1)" || { echo "$DXI" | tail -20; echo "FAIL DXLT_run: import script exited non-zero"; fail=1; DXI=""; }
fi
dxlt_run() {  # mode file
  if [ -s "$2" ]; then
    DXLT_R="$(python3 "$HERE/dxf_check.py" "$1" "$2" 2>&1)" && DXLT_RC=0 || DXLT_RC=$?
    echo "$DXLT_R"
    if [ "$DXLT_RC" != "0" ] || grep -q "^FAIL" <<<"$DXLT_R"; then fail=1; fi
  else
    echo "FAIL DXLT_$1: $2 was not written"; fail=1
    { grep -E "Could not|Nothing to export|Unsupported|Warning" <<<"${DXE}${DXI}" || true; } | head -3 | sed 's/^/     app: /'
  fi
}
dxlt_run export "$TMPW/dxlt/lt_export.dxf"
dxlt_has() { if grep -qF -- "$1" <<<"$DXI"; then echo "ok   $2"; else echo "FAIL $2"; echo "     expected: $1"; { grep -E "Linetype " <<<"$DXI" || true; } | head -12 | sed 's/^/     actual:   /'; fail=1; fi; }
dxlt_has "Linetype HIDDEN: 6.35,3.175" "DXLT_import: the Linetypes list has HIDDEN 6.35,3.175 from the file's LTYPE table"
dxlt_has "Linetype CENTER: 31.75,6.35,6.35,6.35" "DXLT_import: ... CENTER 31.75,6.35,6.35,6.35"
dxlt_has "Linetype DOT: 0.635,6.35" "DXLT_import: ... DOT 0.635,6.35 (the dot became a 0.635 dash)"
if grep -qF "Linetype GAS_LINE" <<<"$DXI"; then echo "FAIL DXLT_import: the complex linetype GAS_LINE was defined"; fail=1; else echo "ok   DXLT_import: the complex linetype GAS_LINE was not defined"; fi
dxlt_run svg "$TMPW/dxlt/lt_import.svg"
dxlt_run reexport "$TMPW/dxlt/lt_reexport.dxf"
