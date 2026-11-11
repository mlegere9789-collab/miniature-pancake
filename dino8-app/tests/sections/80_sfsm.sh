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
#     prints 313.9: its display tessellation, 4 significant digits).
# The loop-less case needs the pick fallback in cmd_srfedit.cpp: the app's loop-driven face mesher returns nothing for a face without trim loops, so
# before it SrfSeam answered "no surface near that point" for such a face.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then SFSM_RUN=(timeout 900 "$BIN" --smoke 60 --script); else SFSM_RUN=(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script); fi
SFSM_DIR="$(mktemp -d)"
sed "s|@SFSMFILE@|$HERE/sfsm_graph.dflow|g" "$HERE/sfsm_script.txt" > "$SFSM_DIR/sfsm_script.txt"
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
sfsm_has loops_area "Area = 313.9" "SFSM: the refused sphere's area is unchanged (4 pi r^2 = 314.2; the app tessellation prints 313.9)"
