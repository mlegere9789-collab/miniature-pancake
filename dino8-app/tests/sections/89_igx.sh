# IGX_: the structure of an exported IGES file, read by an independent parser (tests/igx_check.py): units declaration, the circle as
# an exact entity 100, well-formed entity 126 curves, entity 128 nets of a box and a cylinder at their closed-form sizes / radii, and
# one entity 144 per face pointing at a 128 surface and a 142 boundary.
IGXD="$TMPW/igx"
mkdir -p "$IGXD"
cat > "$IGXD/gen.txt" <<EOS
New
Circle 0,0,0 5
Box 20,0,0 30,10,0 10
Cylinder 50,0,0 3 10
SelAll
Export $IGXD/a.igs
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  IGX_OUT="$(timeout 900 "$BIN" --smoke 120 --script "$IGXD/gen.txt" 2>&1)" || { echo "$IGX_OUT"; echo "FAIL: IGX_ script exited non-zero"; exit 1; }
else
  IGX_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$IGXD/gen.txt" 2>&1)" || { echo "$IGX_OUT"; echo "FAIL: IGX_ script exited non-zero"; exit 1; }
fi
IGX_RES="$(python3 "$HERE/igx_check.py" "$IGXD" 2>&1)"
echo "$IGX_RES"
if grep -q '^FAIL' <<<"$IGX_RES"; then fail=1; fi
