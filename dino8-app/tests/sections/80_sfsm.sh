# SFSM: SrfSeam on a face of a polysurface (kernel::ChangeFaceSeam; sfsm_script.txt, sfsm_graph.dflow).
# Fixture: a Dino Flow Sphere (centre 0,0,0, radius 5) baked into the document. kernel::Brep::Sphere builds a one-face Brep with NO trim loops (the face is
# its whole surface), which is what ChangeFaceSeam supports; the app's own Sphere command builds an ON_BrepSphere whose face has real trim loops.
# The sphere's u is the azimuth in radians and its seam starts at +x (5,0,0), so the closed forms are:
#   * clicking the sphere at (5,0,0) and then at (0,5,0) moves the seam to azimuth 90 degrees: u = pi/2 = 1.571;
#   * a second SrfSeam click at (0,5,0) is on the seam that is now there, so it is reported as already satisfied - this is the observable that the stored
#     Brep really changed (a command that only printed "moved" would move again and print u=1.571 a second time);
#   * the next move to (-5,0,0) is azimuth 180 degrees: u = pi = 3.142 (inside the moved domain [pi/2, 5 pi/2]);
#   * Undo restores the state after the first move, so (0,5,0) is on the seam again (already satisfied) and not on the u = pi one;
#   * the app's Sphere (ON_BrepSphere, trim loops) is refused with the kernel's reason, nothing changes and its area stays 4 pi r^2 = 314.2 (the app
#     prints 313.9: its display tessellation, 4 significant digits);
#   * Area and Volume of the baked sphere are the closed forms 4 pi r^2 = 314.16 and 4/3 pi r^3 = 523.60, before the first move, after it and after the
#     second one: the seam move changes the parameterization, never the shape. The app measures the display mesh (chord tolerance 0.005, an inscribed
#     polyhedron: it can only read LOW). A triangle's centre sags 4/3 of its edge sag below the sphere, so the shortfall is at most 2 (4/3) tol / r =
#     0.53% in area and 3 (4/3) tol / r = 0.4% in volume, and the windows are [closed form * (1 - 0.0055), closed form * 1.0005] and
#     [closed form * (1 - 0.004), closed form * 1.0005]. Measured: Area 313.6 (-0.17%), Volume 521.9 (-0.32%).
# The loop-less case needs the face mesher (geom/BrepMesher.cpp) to mesh a face with no trim loops from its surface domain: it used to emit nothing for it,
# so Area and Volume read 0 and SrfSeam answered "no surface near that point" for the face.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then SFSM_RUN=(timeout 900 "$BIN" --smoke 60 --script); else SFSM_RUN=(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script); fi
SFSM_DIR="$(mktemp -d)"
sed -e "s|@SFSMFILE@|$HERE/sfsm_graph.dflow|g" -e "s|@SFSMBOXFILE@|$HERE/sfsm_box_graph.dflow|g" "$HERE/sfsm_script.txt" > "$SFSM_DIR/sfsm_script.txt"
SFSMO="$("${SFSM_RUN[@]}" "$SFSM_DIR/sfsm_script.txt" 2>&1)" || { echo "$SFSMO"; echo "FAIL: sfsm script exited non-zero"; rm -rf "$SFSM_DIR"; exit 1; }
rm -rf "$SFSM_DIR"
sfsm_blk() { awk -v name="$1" '$0 ~ ("Unknown command: SFSMBEGIN_" name "$") { on = 1; next } on && /Unknown command: SFSMBEGIN_/ { exit } on { print }' <<<"$SFSMO"; }
sfsm_has() { # BLOCK PATTERN DESC (fixed string)
  if grep -qF -- "$2" <<<"$(sfsm_blk "$1")"; then echo "ok   $3"; else echo "FAIL $3"; fail=1; echo "     expected: $2"; { sfsm_blk "$1" | grep -E "SrfSeam|Warning|rror" || true; } | head -4 | cut -c1-260 | sed 's/^/     actual:   /'; fi
}
sfsm_lacks() { if grep -qF -- "$2" <<<"$(sfsm_blk "$1")"; then echo "FAIL $3"; fail=1; else echo "ok   $3"; fi; }
sfsm_has move "SrfSeam: seam of face 0 moved to u=1.571" "SFSM: the baked loop-less sphere's seam moved to azimuth 90 degrees (u = pi/2 = 1.571)"
sfsm_has again "SrfSeam: the seam is already at u=1.571; nothing changed" "SFSM: the seam is now at (0,5,0): a second click there is already satisfied (the stored Brep changed)"
sfsm_lacks again "moved to" "SFSM: the second click did not move anything"
sfsm_has move2 "SrfSeam: seam of face 0 moved to u=3.142" "SFSM: a further move to (-5,0,0) lands at azimuth 180 degrees (u = pi = 3.142)"
sfsm_has undo_again "SrfSeam: the seam is already at u=1.571; nothing changed" "SFSM: Undo restored the state after the first move (the seam is at u = pi/2 again)"
sfsm_has loops "SrfSeam: the face has trim loops" "SFSM: the app Sphere (trim loops) is refused with the kernel's reason"
sfsm_has loops "nothing was changed" "SFSM: the refusal says nothing was changed"
sfsm_num() { # BLOCK PREFIX : the number after PREFIX in the block ("Area = 313.9 square ..." -> 313.9)
  { grep -oE -- "$2 = [-0-9.eE+]+" <<<"$(sfsm_blk "$1")" || true; } | head -1 | sed -E 's/.* = //'
}
sfsm_within() { # BLOCK PREFIX CLOSEDFORM LOWFRAC DESC : closed form * (1 - LOWFRAC) <= value <= closed form * 1.0005
  local v; v="$(sfsm_num "$1" "$2")"
  if [ -n "$v" ] && awk -v v="$v" -v c="$3" -v lf="$4" 'BEGIN { exit !(v >= c * (1 - lf) && v <= c * 1.0005) }'; then echo "ok   $5 ($2 = $v)"; else echo "FAIL $5 ($2 = '${v:-none}', expected $3 down to -$4)"; fail=1; fi
}
sfsm_within area0 Area 314.1593 0.0055 "SFSM: the baked loop-less sphere's Area is 4 pi r^2 = 314.16 (it read 0: the face mesher emitted nothing for a loop-less face)"
sfsm_within vol0 Volume 523.5988 0.004 "SFSM: the baked sphere's Volume is 4/3 pi r^3 = 523.60 (it read 0: not a closed mesh)"
sfsm_within area1 Area 314.1593 0.0055 "SFSM: Area is unchanged after the seam moved to azimuth 90 degrees"
sfsm_within vol1 Volume 523.5988 0.004 "SFSM: Volume is unchanged after the seam move"
sfsm_within area2 Area 314.1593 0.0055 "SFSM: Area is unchanged after the second move (azimuth 180 degrees)"
sfsm_within vol2 Volume 523.5988 0.004 "SFSM: Volume is unchanged after the second move"
sfsm_within box_area Area 52 0.0005 "SFSM: a baked loop-less 4 x 3 x 2 Box (six planar faces) has Area 2 (12 + 8 + 6) = 52: planar faces mesh exactly"
sfsm_within box_vol Volume 24 0.0005 "SFSM: ...and Volume 4 * 3 * 2 = 24 (the six faces weld into a closed mesh)"
sfsm_within loops_area Area 314.1593 0.0055 "SFSM: the refused sphere's Area is the same closed form (the app prints its display tessellation, 313.9)"
