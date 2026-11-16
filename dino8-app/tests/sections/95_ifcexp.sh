# ---------------------------------------------------------------------------
# IFCEXP_: ExportIfc (cmd_arch.cpp over dino8/kernel/ifc_writer.h). One real app process builds a wall with a door and a window, a
# slab, a column and a beam, and writes an IFC4 STEP file; the stand-alone oracle tests/ifc_oracle.py (Python standard library,
# its own STEP tokenizer) then parses that file and checks the frame, ids, GlobalIds, referential integrity of every #id, the typed
# relationships, the extruded geometry against the written quantities, and the hand-computed values of tests/ifc_expect_smoke.json.
#
# Hand-computed values (no value is read from the app):
#  * wall 5 long, 2.4 high, 0.2 thick: footprint 1.0, side face 12.0, volume 2.4; door 0.9 x 2.1 = 1.89 and window 1.2 x 1.2 = 1.44 cut
#    it: net face 12 - 3.33 = 8.67, net volume 8.67 * 0.2 = 1.734.
#  * slab 6 x 4 x 0.25: area 24, perimeter 20, volume 6.0. Column 0.4 x 0.3 x 3.0: section 0.12, volume 0.36. Beam 5 long 0.2 x 0.3:
#    section 0.06, volume 0.3.
#  * entity count 121: 11 context/units/project + 6 site/building (2 placements, 2 entities, 2 aggregations) + 4 storey + 1 storey
#    aggregation + 1 containment = 23; 4 elements x 14 (5 placement, 4 profile, 4 solid and shape, 1 element) = 56; quantities with
#    their set and relation (6 + 2 extra + 2) + (4 + 2) + (3 + 2) + (3 + 2) = 26; property sets 4 x (2 values + set + relation) = 16.
#    23 + 56 + 26 + 16 = 121.
ifcx_ok() { echo "ok   $1"; }
ifcx_bad() { echo "FAIL $1"; fail=1; }
ifcx_exec() {  # ifcx_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 100 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$1" 2>&1
  fi
}

IFCX_FILE="$TMPW/ifcexp_model.ifc"
rm -f "$IFCX_FILE"
cat > "$TMPW/ifcexp_script.txt" <<EOS
Wall 0,0,0 5,0,0 2.4 0.2
SelLast
Door 2.5,0,0 0.9 2.1
SelLast
Window 4,0,0 1.2 1.2 0.9
ArchSlab 0,10,0 6,14,0 0.25
Column 10,0,0 3.0 0.4 0.3
Beam 0,5,3 5,5,3 0.2 0.3
ExportIfc File=$IFCX_FILE Name=Dino8Smoke Storey=Level0
EOS
IFCX_OUT="$(ifcx_exec "$TMPW/ifcexp_script.txt" 2>&1 || true)"
if grep -qF "ExportIfc: wrote" <<<"$IFCX_OUT" && grep -qF "4 element(s): 1 wall(s), 1 slab(s), 1 column(s), 1 beam(s); 0 component(s) not exported" <<<"$IFCX_OUT"; then
  ifcx_ok "ExportIfc reports 4 elements (wall, slab, column, beam) and nothing skipped (the door and window are openings of the wall)"
else
  ifcx_bad "ExportIfc reports 4 elements (wall, slab, column, beam) and nothing skipped"
fi
if [ -s "$IFCX_FILE" ]; then ifcx_ok "ExportIfc wrote a non-empty file"; else ifcx_bad "ExportIfc wrote a non-empty file"; fi

IFCX_SELF="$(python3 -I "$HERE/ifc_oracle.py" --selftest 2>&1 || true)"
if grep -q "^ok   selftest" <<<"$IFCX_SELF" && ! grep -q "^FAIL" <<<"$IFCX_SELF"; then
  ifcx_ok "IFC oracle self-test: a good file passes and ten corruptions are each caught"
else
  ifcx_bad "IFC oracle self-test ($IFCX_SELF)"
fi

IFCX_RES="$(python3 -I "$HERE/ifc_oracle.py" "$IFCX_FILE" --expect "$HERE/ifc_expect_smoke.json" 2>&1 || true)"
while IFS= read -r line; do
  case "$line" in
    "ok   "*) echo "ok   IFC oracle: ${line#ok   }" ;;
    "FAIL "*) echo "FAIL IFC oracle: ${line#FAIL }"; fail=1 ;;
  esac
done <<<"$IFCX_RES"
if ! grep -q "^ok   expected: 121 entities" <<<"$IFCX_RES"; then ifcx_bad "IFC oracle reached the expected-count checks"; fi
rm -f "$TMPW/ifcexp_script.txt" "$IFCX_FILE"
