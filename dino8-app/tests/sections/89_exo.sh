# EXO_: mesh export options - Export <path> Format=Ascii|Binary|BinaryBigEndian, Scale=, Units=, Tolerance=, PerObject=Yes for
# .stl / .obj / .ply. The written files are decoded by tests/exo_check.py with readers written from the public format
# descriptions (struct for binary STL and binary PLY), never by the application's own importers.
EXOD="$TMPW/exo"
mkdir -p "$EXOD"
cat > "$EXOD/gen.txt" <<EOS
New
Box 0,0,0 10,20,0 30
SelAll
Export $EXOD/bin.stl
Export $EXOD/asc.stl Format=Ascii
Export $EXOD/sc.stl Scale=0.001
Export $EXOD/in.stl Units=in
Export $EXOD/a.ply
Export $EXOD/b_le.ply Format=Binary
Export $EXOD/b_be.ply Format=BinaryBigEndian
Export $EXOD/s.obj Scale=2
Export $EXOD/bad.stl Format=Bogus
Export $EXOD/badscale.stl Scale=-2
Export $EXOD/badpo.dxf PerObject=Yes
New
Box 0,0,0 1234.56789,2000,0 3000
SelAll
Export $EXOD/big.obj
New
Cylinder 0,0,0 5 20
SelAll
Export $EXOD/coarse.stl Tolerance=1
Export $EXOD/fine.stl Tolerance=0.005
New
Box 0,0,0 10,10,0 10
Box 20,0,0 30,10,0 10
SelAll
Export $EXOD/po.stl PerObject=Yes
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  EXO_OUT="$(timeout 900 "$BIN" --smoke 120 --script "$EXOD/gen.txt" 2>&1)" || { echo "$EXO_OUT"; echo "FAIL: EXO_ script exited non-zero"; exit 1; }
else
  EXO_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$EXOD/gen.txt" 2>&1)" || { echo "$EXO_OUT"; echo "FAIL: EXO_ script exited non-zero"; exit 1; }
fi
printf '%s\n' "$EXO_OUT" > "$EXOD/app.out"
EXO_RES="$(python3 "$HERE/exo_check.py" "$EXOD" "$EXOD/app.out" 2>&1)"
echo "$EXO_RES"
if grep -q '^FAIL' <<<"$EXO_RES"; then fail=1; fi
