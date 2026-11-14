# NBS_: free-form NURBS faces through STEP, read by an independent parser and Cox-de Boor evaluator (tests/nbs_check.py): a bicubic patch
# with hand-typed control points (degrees, knot multiplicities, net) and a sphere of radius 4 (weights, evaluated radius), each re-imported and
# exported again (deviation < 1e-6), and the closing of every boundary loop.
NBSD="$TMPW/nbs"
mkdir -p "$NBSD"
{
  echo New
  for pt in 0,0,0 0,3,1 0,6,2 0,9,0 3,0,1 3,3,3 3,6,1 3,9,0 6,0,0 6,3,2 6,6,3 6,9,1 9,0,2 9,3,0 9,6,1 9,9,3; do echo "Point $pt"; done
  printf 'SelPt\nSrfControlPtGrid\n4\nSelNone\nSelPt\nDelete\nSelSrf\nExport %s/patch.stp\nNew\nImport %s/patch.stp\nSelAll\nExport %s/patch2.stp\n' "$NBSD" "$NBSD" "$NBSD"
  printf 'New\nSphere 0,0,0 4\nSelAll\nExport %s/sph.stp\nNew\nImport %s/sph.stp\nSelAll\nExport %s/sph2.stp\n' "$NBSD" "$NBSD" "$NBSD"
} > "$NBSD/gen.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  NBS_OUT="$(timeout 900 "$BIN" --smoke 120 --script "$NBSD/gen.txt" 2>&1)" || { echo "$NBS_OUT"; echo "FAIL: NBS_ script exited non-zero"; exit 1; }
else
  NBS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$NBSD/gen.txt" 2>&1)" || { echo "$NBS_OUT"; echo "FAIL: NBS_ script exited non-zero"; exit 1; }
fi
NBS_RES="$(python3 "$HERE/nbs_check.py" "$NBSD" 2>&1)"
echo "$NBS_RES"
if grep -q '^FAIL' <<<"$NBS_RES"; then fail=1; fi
