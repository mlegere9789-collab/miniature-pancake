# MRM_: several rims of one solid of revolution blended exactly in ONE run (mrm_script.txt).
# FilletEdge stages every revolve rim picked on the solid and applies them together at Enter through
# kernel::FilletRevolveRims (cmd_fillet.cpp TryRevolveRimsFillet): one rebuild, each rim with its own radius, setbacks
# taken from the original profile corners (pick order does not matter), overlapping setbacks refused with the kernel's
# reason. ChamferEdge applies each exact kernel bevel as picked; both now work on a solid that already carries the torus
# blends / bevel cones of an earlier run. Fixture: the chamfered cylinder of RVR_ (profile (0,0) (5,0) (5,3) (4,4) (0,4)).
# Closed forms are the Green integral V = pi * closed-integral rho^2 dz of the blended profile with the circular arcs
# integrated in closed form (python, independent of the kernel's tangent-length formula; also reproduces the RVR_
# values 285.346 / 291.906 / 294.723). Volume is read from the display mesh (0.07-0.11% low, so windows are
# [closed - 1.0, closed + 0.3]); a missing blend moves the volume by more than the window:
#   two      r=1.5 @(5,0) + r=3 @(4,4)               280.570   (without the (4,4) rim 285.346, without (5,0) 294.723)
#   three    r=1.5 at (5,0), (5,3), (4,4)             282.647
#   overlap  r=1.8 @(5,3) alone (the (4,4) pick is refused)   297.356 (both would be 295.624)
#   chamfer_two  wall 1 x cap 0.5 @(5,0) + bevel 1.0 x cap 0.8 @(4,4)   284.853 (one alone: 291.906 / 292.445)
#   mixed    bottom chamfer 1 x 0.5, then r=0.8 at both ends of its bevel   290.670 (one alone: 290.755 / 291.821)
# Face / edge counts and "closed solid" are topological and exact: the kernel result gains one face and two edges per rim.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MRMO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/mrm_script.txt" 2>&1)" || { echo "$MRMO"; echo "FAIL: mrm script exited non-zero"; exit 1; }
else
  MRMO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/mrm_script.txt" 2>&1)" || { echo "$MRMO"; echo "FAIL: mrm script exited non-zero"; exit 1; }
fi
mrmblock() { awk -v name="$1" '$0 ~ ("Unknown command: MRMBEGIN_" name "$") { on = 1; next } on && /Unknown command: MRMBEGIN_/ { exit } on { print }' <<<"$MRMO"; }
mrmcheck() { local blk; blk="$(mrmblock "$1")"; if grep -qF -- "$2" <<<"$blk"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; grep -E "^history: (FilletEdge|ChamferEdge|!|Object|  [0-9]|Volume)" <<<"$blk" | head -10 | sed 's/^/     actual:   /' || true; fail=1; fi; }
mrmnot() { local blk; blk="$(mrmblock "$1")"; if grep -qF -- "$2" <<<"$blk"; then echo "FAIL $3"; echo "     unexpected: $2"; fail=1; else echo "ok   $3"; fi; }
mrmvol() { local blk; blk="$(mrmblock "$1")"; grep -E "^history: Volume = " <<<"$blk" | tail -1 | sed -E 's/^history: Volume = ([0-9.eE+-]+).*/\1/' || true; }
# mrmnum NAME CLOSED DESC: the case's last "Volume = V" lies in [CLOSED - 1.0, CLOSED + 0.3]
mrmnum() {
  local v lo hi; v="$(mrmvol "$1")"; lo="$(awk -v c="$2" 'BEGIN { printf "%.3f", c - 1.0 }')"; hi="$(awk -v c="$2" 'BEGIN { printf "%.3f", c + 0.3 }')"
  if [ -n "$v" ] && awk -v v="$v" -v lo="$lo" -v hi="$hi" 'BEGIN { exit !(v >= lo && v <= hi) }'; then echo "ok   $3 (Volume $v in [$lo, $hi], closed form $2)"; else echo "FAIL $3 (Volume '${v:-none}' not in [$lo, $hi], closed form $2)"; fail=1; fi
}
# mrmobjs NAME N DESC: the case's List shows exactly N objects
mrmobjs() {
  local blk n; blk="$(mrmblock "$1")"
  n="$(grep -cE "^history: Object [0-9]+ \(" <<<"$blk" || true)"
  if [ "$n" = "$2" ]; then echo "ok   $3 ($n object(s))"; else echo "FAIL $3 ('$n' object(s), expected $2)"; fail=1; fi
}
# mrmsame A B DESC: the last Volume of two cases agrees to 1e-4 relative (same geometry, different pick order / run split)
mrmsame() {
  local a b; a="$(mrmvol "$1")"; b="$(mrmvol "$2")"
  if [ -n "$a" ] && [ -n "$b" ] && awk -v a="$a" -v b="$b" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 1e-4 * a) }'; then echo "ok   $3 ($a vs $b)"; else echo "FAIL $3 ('${a:-none}' vs '${b:-none}')"; fail=1; fi
}
MRM_VALID="Object 1: valid"
# --- two rims, two radii, ONE run
mrmcheck two "FilletEdge: 2 rims of solid-of-revolution object 1 replaced with an exact fillet (radii 1.5, 3; exact kernel torus patches tangent to both faces at each rim, 6 faces, closed solid)" "MRM_two: both rims are blended exactly in ONE run (before: the second pick was refused and only the first applied)"
mrmcheck two "6 faces, 11 edges, closed solid" "MRM_two: 6 faces (input + 2 rims), closed solid"
mrmcheck two "$MRM_VALID" "MRM_two: Check valid"
mrmobjs two 1 "MRM_two: replaced in place, no mesh object"
mrmnum two 280.570 "MRM_two: Green volume r=1.5 @(5,0) + r=3 @(4,4)"
mrmnot two "cannot be blended together" "MRM_two: ...no refusal"
mrmcheck two_swapped "radii 3, 1.5" "MRM_two_swapped: picked in the other order (radii listed in pick order)"
mrmnum two_swapped 280.570 "MRM_two_swapped: same Green volume"
mrmsame two two_swapped "MRM_two_swapped: pick order does not change the volume"
mrmcheck two_swapped "$MRM_VALID" "MRM_two_swapped: Check valid"
mrmnum two_runs 280.570 "MRM_two_runs: second rim in a SECOND run on a solid that already carries a torus: same Green volume"
mrmsame two two_runs "MRM_two_runs: one run vs two runs"
mrmcheck two_runs "$MRM_VALID" "MRM_two_runs: Check valid"
mrmcheck two_runs "6 faces, 11 edges, closed solid" "MRM_two_runs: 6 faces, closed solid"
mrmnot two_runs "not used" "MRM_two_runs: the second run does not fall back to anything"
# --- three rims
mrmcheck three "FilletEdge: 3 rims of solid-of-revolution object 1 replaced with an exact fillet (radii 1.5, 1.5, 1.5;" "MRM_three: three rims in one run"
mrmcheck three "7 faces" "MRM_three: 7 faces (input + 3 rims)"
mrmcheck three "$MRM_VALID" "MRM_three: Check valid"
mrmnum three 282.647 "MRM_three: Green volume r=1.5 at (5,0), (5,3), (4,4)"
# --- overlapping setbacks
mrmcheck overlap "overlapping setbacks: rims 0 and 1 share a face" "MRM_overlap: the second pick is refused with the kernel's reason"
mrmcheck overlap "the staged rim(s) stay staged" "MRM_overlap: ...and the first rim stays staged"
mrmcheck overlap "FilletEdge: rim of solid-of-revolution object 1 replaced with an exact fillet (radius 1.8;" "MRM_overlap: the first rim applies alone"
mrmnum overlap 297.356 "MRM_overlap: Green volume with r=1.8 @(5,3) alone (both rims would give 295.624)"
mrmcheck overlap "$MRM_VALID" "MRM_overlap: Check valid"
# --- the same rim twice
mrmcheck same "the same rim is listed twice" "MRM_same: the same rim picked twice is refused with the kernel's reason"
mrmnum same 285.346 "MRM_same: only the first r=1.5 applies"
# --- chamfers
mrmcheck chamfer_two "$MRM_VALID" "MRM_chamfer_two: Check valid"
mrmcheck chamfer_two "6 faces" "MRM_chamfer_two: 6 faces, closed solid"
mrmnum chamfer_two 284.853 "MRM_chamfer_two: Green volume wall 1 x cap 0.5 @(5,0) + bevel 1.0 x cap 0.8 @(4,4)"
# --- rims on a bevel cone an earlier chamfer made
mrmcheck mixed "FilletEdge: 2 rims of solid-of-revolution object 1 replaced with an exact fillet (radii 0.8, 0.8;" "MRM_mixed: both ends of a bevel cone from an earlier run are blended in one run"
mrmcheck mixed "$MRM_VALID" "MRM_mixed: Check valid"
mrmnum mixed 290.670 "MRM_mixed: Green volume (one rim alone: 290.755 / 291.821)"
# --- negative controls: the single-rim RVR_ pins are unchanged
mrmcheck ctl_single "FilletEdge: rim of solid-of-revolution object 1 replaced with an exact fillet (radius 1.5; exact kernel torus patch tangent to both faces at the rim, 5 faces, closed solid)" "MRM_ctl_single: one rim still reads exactly as RVR_ pins it"
mrmnum ctl_single 285.346 "MRM_ctl_single: Green volume r=1.5 at (5,0), as RVR_"
mrmnot ctl_single "rims of solid-of-revolution" "MRM_ctl_single: ...no multi-rim wording for one rim"
mrmnum ctl_single_chamfer 291.906 "MRM_ctl_single_chamfer: single exact bevel, as RVR_"
mrmnot ctl_cyl_two "solid-of-revolution" "MRM_ctl_cyl_two: a plain cylinder's two rims never take the revolve path"
mrmnot ctl_tcone_two "solid-of-revolution" "MRM_ctl_tcone_two: a cone frustum's two rims never take the revolve path"
mrmcheck ctl_box "replaced with an exact fillet (radius 1)" "MRM_ctl_box: a box edge is still the kernel's exact planar fillet"
mrmnot ctl_box "solid-of-revolution" "MRM_ctl_box: ...no revolve-rim message"
if grep -q "gl_error=0" <<<"$MRMO"; then echo "ok   MRM script ran without OpenGL errors"; else echo "FAIL MRM script ran without OpenGL errors"; fail=1; fi
