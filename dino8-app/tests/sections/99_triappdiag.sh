# TRIAPPDIAG: diagnostic of the sphere bounding box, the rim chamfer area and the off-axis cylinder union (prints the app's own lines)
TAD="$TMPW/triappdiag"
mkdir -p "$TAD"
cp "$HERE/step_kernel_rational_sphere_fixture.stp" "$TAD/sph.stp"
cat > "$TAD/a.txt" <<EOT
New
Open $TAD/sph.stp
SelAll
List
Area
BoundingBox
Cancel
New
Cylinder 0,0,0 2 5
ChamferEdge
Radius=0.6
Distance2=0.4
0,2,5
Enter
SelAll
Volume
Area
List
BoundingBox
Cancel
New
Cylinder 0,0,0 3 10
Cylinder 4,0,2 3 10
SelAll
BooleanUnion
SelAll
List
Volume
Area
EOT
tad_run() {
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 100 --script "$1" 2>&1 || true
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$1" 2>&1 || true
  fi
}
tad_run "$TAD/a.txt" | grep -E "^history:" | cut -c1-300 | sed 's/^/ok   DIAG /'
