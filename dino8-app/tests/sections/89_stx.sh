# STX_: the structure of an exported STEP solid, read by an independent parser (tests/stx_check.py): one MANIFOLD_SOLID_BREP over one
# CLOSED_SHELL, the closed-form face counts / radii / vertex box, and a CLOSED shell in the STEP sense - every EDGE_CURVE shared by
# exactly two ORIENTED_EDGEs (the exporter used to write every face's edges and vertices again, so no edge was shared), and the
# AP214 presentation of a named, red object on layer Walls: the solid's name, STYLED_ITEM -> COLOUR_RGB (1,0,0), PRESENTATION_LAYER_ASSIGNMENT.
STXD="$TMPW/stx"
mkdir -p "$STXD"
cat > "$STXD/gen.txt" <<EOS
New
Box 0,0,0 10,10,0 10
Cylinder 5,5,-5 2 20
SelNone
SelID 1
BooleanDifference
SelID 2
Enter
SelAll
Export $STXD/cut.stp
New
Cylinder 0,0,0 3 10
SelAll
Export $STXD/cyl.stp
New
NewLayer Walls
Box 0,0,0 10,10,0 10
SelAll
SetRenderColor 255,0,0
SelAll
SetObjectName Slab
SelAll
Export $STXD/styled.stp
New
Import $STXD/styled.stp
SelAll
List
Export $STXD/styled2.stp
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  STX_OUT="$(timeout 900 "$BIN" --smoke 120 --script "$STXD/gen.txt" 2>&1)" || { echo "$STX_OUT"; echo "FAIL: STX_ script exited non-zero"; exit 1; }
else
  STX_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$STXD/gen.txt" 2>&1)" || { echo "$STX_OUT"; echo "FAIL: STX_ script exited non-zero"; exit 1; }
fi
printf '%s\n' "$STX_OUT" > "$STXD/app.out"
STX_RES="$(python3 "$HERE/stx_check.py" "$STXD" "$STXD/app.out" 2>&1)"
echo "$STX_RES"
if grep -q '^FAIL' <<<"$STX_RES"; then fail=1; fi
