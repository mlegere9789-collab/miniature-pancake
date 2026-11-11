# TYPED_: What prints the analytic kind of each object (typed_readout_script.txt). Every expected value is a closed form of the
# command's own arguments: the 3-4-5 line has length 5; Circle radius 5; the Arc from angle 0 to the point straight above the centre
# is 90 degrees; the ellipse with semi-axes 6 and 3; the sphere / cylinder / torus carry their radii; the cone of base radius 3 and
# height 6 has half angle atan(3 / 6) = 26.565051 degrees (the readout names the apex too; the section only pins its x and y, the app builds that cone with the apex at the picked point).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  TYPED_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/typed_readout_script.txt" 2>&1)" || { echo "$TYPED_OUT" | tail -40; echo "FAIL TYPED_run: script exited non-zero"; fail=1; TYPED_OUT=""; }
else
  TYPED_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/typed_readout_script.txt" 2>&1)" || { echo "$TYPED_OUT" | tail -40; echo "FAIL TYPED_run: script exited non-zero"; fail=1; TYPED_OUT=""; }
fi
typed_has() { if grep -qF -- "$1" <<<"$TYPED_OUT"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; near "$TYPED_OUT" "$1"; fi; }
typed_has "Type: Line, from (0, 0, 0) to (3, 4, 0), length 5" "TYPED_0: the 3-4-5 line is a Line of length 5"
typed_has "Type: Circle, center (10, 0, 0), radius 5" "TYPED_1: Circle 10,0,0 5 is a Circle with its centre and radius"
typed_has "Type: Arc, center (20, 0, 0), radius 5, angle 90 degrees" "TYPED_2: the quarter arc is an Arc of 90 degrees"
typed_has "Type: Polyline, 5 vertices" "TYPED_3: the closed rectangle is a Polyline of 5 vertices (not a Line, Arc or Ellipse)"
typed_has "Type: Ellipse, center (0, 40, 0), radii 6 x 3" "TYPED_4: the ellipse reports its centre and radii 6 x 3"
typed_has "Face types: Sphere x1" "TYPED_5: a sphere B-rep is one Sphere face"
typed_has "Sphere, center (0, 60, 0), radius 3" "TYPED_5: the Sphere face reports its centre and radius"
typed_has "Cylinder, radius 2" "TYPED_6: the cylinder wall is a Cylinder of radius 2"
typed_has "Face types: Plane x2, Cylinder x1" "TYPED_6: the capped cylinder is two Plane caps and one Cylinder wall"
typed_has "Cone, apex (60, 60, " "TYPED_7: the cone's apex is on the axis through 60,60"
typed_has "half angle 26.565051 degrees" "TYPED_7: the cone's half angle is atan(3 / 6)"
typed_has "Torus, center (0, 80, 0)" "TYPED_8: the torus is a Torus at its centre"
typed_has "major radius 6, minor radius 1.5" "TYPED_8: the torus reports its major and minor radius"
