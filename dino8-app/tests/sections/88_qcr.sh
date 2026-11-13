# QCR_: hostile-input probes of recently merged commands (qcr_script.txt). Each check FAILs on a build that has the bug it names:
#   * Boss / Rib: Enter or a typed number at an object prompt dereferenced the empty footprint curve / base surface (segmentation fault);
#     a height of 0, -3 or 1e9 replaced the exact polysurface by a mesh; a height of 1e-9 hung the kernel route and 1e-5 reported a volume
#     100 times too large; Rib Thickness=nan / 0 / -2 was accepted (NaN-volume mesh, or the exact base replaced by a mesh)
#   * Offset: a distance typed before the curves were chosen wrote options[0] of an empty list (segmentation fault)
#   * SubDMoveSymmetric / SubDRemoveCreaseSymmetric: a refused command left an empty undo step (Undo undid nothing, Redo was wiped)
#   * ArrayCrvOnSrf: the item count was never range-checked
# and a few positive controls with closed forms (ArrayCrv points, ArraySrf centroid, MoveFace + Undo + Redo, the rib volume 1000 + 6 x 4 x t).
# Every script case runs in its OWN app process under `timeout` (QCR_TMO seconds, default 90), at most QCR_JOBS (default 3) at a time, so a
# crash or hang fails only its own checks (the pattern of tests/sections/61_qc3a.sh). Expected volumes: box 10 x 10 x 10 = 1000;
# boss r = 2, h = 5: 1000 + pi 2^2 5 = 1062.83; rib 6 long x 4 high x thickness t: 1000 + 24 t; pocket r = 2, depth 3: 1000 - 37.70 = 962.30.
sed "s|@TMP@|$TMPW|g" "$HERE/qcr_script.txt" > "$TMPW/qcr_script.txt"
qcr_ok() { echo "ok   $1"; }
qcr_bad() { echo "FAIL $1"; fail=1; }
QCR_TMO="${QCR_TMO:-90}"
QCR_JOBS="${QCR_JOBS:-3}"
QCR_DIR="$TMPW/qcr_cases"
mkdir -p "$QCR_DIR"
awk -v d="$QCR_DIR" '
  /^QCRBEGIN_/ { if (out != "") close(out); name = substr($0, 10); out = ""
                 if (name != "end") { out = d "/" name ".txt"; print "SetActiveViewport Top\nNew" > out; print $0 > out }
                 next }
  out != "" { print > out }' "$TMPW/qcr_script.txt"
QCR_PLAIN=0
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then QCR_PLAIN=1; fi
qcr_run_one() {  # $1 = case name; writes $QCR_DIR/$1.out and $1.rc (own working directory and config dir)
  local n="$1" w="$QCR_DIR/run_$1" rc=1 try
  mkdir -p "$w/cfg"
  for try in 1 2; do
    if [ "$QCR_PLAIN" = 1 ]; then
      (cd "$w" && XDG_CONFIG_HOME="$w/cfg" timeout "$QCR_TMO" "$BIN" --smoke 30 --script "$QCR_DIR/$n.txt" >"$QCR_DIR/$n.out" 2>&1); rc=$?
    else
      (cd "$w" && XDG_CONFIG_HOME="$w/cfg" timeout "$QCR_TMO" xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$QCR_DIR/$n.txt" >"$QCR_DIR/$n.out" 2>&1); rc=$?
    fi
    # xvfb-run -a can lose a race for a display number when several start together: retry once if the app never started
    if [ "$rc" != 0 ] && [ "$rc" != 124 ] && ! grep -q '^smoke:' "$QCR_DIR/$n.out" 2>/dev/null && ! grep -q 'Segmentation fault' "$QCR_DIR/$n.out" 2>/dev/null; then continue; fi
    break
  done
  echo "$rc" >"$QCR_DIR/$n.rc"
}
QCR_NAMES="$(cd "$QCR_DIR" && ls ./*.txt | sed 's|^\./||; s|\.txt$||')"
for n in $QCR_NAMES; do
  while [ "$(jobs -rp | wc -l)" -ge "$QCR_JOBS" ]; do sleep 0.2; done
  qcr_run_one "$n" &
done
wait
for n in $QCR_NAMES; do
  rc="$(cat "$QCR_DIR/$n.rc" 2>/dev/null || echo 1)"
  if [ "$rc" = 124 ]; then
    echo "FAIL QCR_${n}_run: the script case did not finish in ${QCR_TMO} s (a hang)"; fail=1
  elif [ "$rc" != 0 ]; then
    tail -4 "$QCR_DIR/$n.out" 2>/dev/null | cut -c1-200; echo "FAIL QCR_${n}_run: the app exited with status $rc (a crash, or a hang the app's own 30 s smoke watchdog stopped)"; fail=1
  else
    qcr_ok "QCR_${n}_run: the app ran the case to the end (no crash, no hang)"
  fi
  # a case that died or hung has no usable output: every check below then fails on an empty block instead of passing on nothing
  if [ "$rc" != 0 ]; then : >"$QCR_DIR/$n.out"; fi
done
qcr_case() { [ -f "$QCR_DIR/$1.out" ] && awk -v n="QCRBEGIN_$1" 'index($0, "Unknown command: " n) { on = 1; next } on' "$QCR_DIR/$1.out"; }
qcr_has() { if [ -n "$1" ] && grep -qF -- "$2" <<<"$1"; then qcr_ok "$3"; else qcr_bad "$3"; echo "     expected: $2"; fi; }
qcr_hasnot() { if [ -z "$1" ] || grep -qF -- "$2" <<<"$1"; then qcr_bad "$3"; else qcr_ok "$3"; fi; }  # an empty case (it died earlier) must not pass
qcr_count() { if [ -n "$1" ] && [ "$(grep -cF -- "$2" <<<"$1")" -ge "$3" ]; then qcr_ok "$4"; else qcr_bad "$4 (found $( { grep -cF -- "$2" <<<"$1" || true; } ) of $3)"; fi; }
# the Nth "Volume = <number>" of a block (N = 0: the last one)
qcr_vol() { local all; all="$({ grep -oE "Volume = [0-9.eE+-]+" <<<"$1" || true; } | sed -E 's/Volume = //')"; if [ "$2" = 0 ]; then tail -1 <<<"$all"; else sed -n "${2}p" <<<"$all"; fi; }
qcr_num() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then qcr_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else qcr_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }
# the first "CV[i] x,y,z" line of an object listing for control point i
qcr_cv() { { grep -oE "CV\[$2\] [-0-9.eE+]+,[-0-9.eE+]+,[-0-9.eE+]+" <<<"$1" || true; } | tail -1 | sed -E "s/CV\[$2\] //"; }

A="$(qcr_case boss_enter)"
qcr_has "$A" "Boss: nothing was selected; nothing was changed" "QCR_boss_enter: Enter with nothing selected ends Boss and says so"
qcr_num "$(qcr_vol "$A" 0)" 1000 0.001 "QCR_boss_enter: the box is untouched (1000)"
A="$(qcr_case rib_number_first)"
qcr_num "$(qcr_vol "$A" 0)" 1000 0.001 "QCR_rib_number_first: a number typed at the spine prompt is ignored, the box is untouched (1000)"
A="$(qcr_case boss_number_at_base)"
qcr_has "$A" "Boss: exact kernel route (MakeBoss)" "QCR_boss_number_at_base: a number at the base prompt is ignored and the boss still goes through the exact route"
qcr_num "$(qcr_vol "$A" 0)" 1062.83 0.002 "QCR_boss_number_at_base: 1000 + pi 2^2 5"

A="$(qcr_case boss_height_bad)"
qcr_count "$A" "the height must be a number from the model tolerance" 3 "QCR_boss_height_bad: 0, -3 and 1e9 are each refused with the allowed range"
qcr_has "$A" "Boss: exact kernel route (MakeBoss)" "QCR_boss_height_bad: the 5 typed afterwards builds the boss exactly (the prompt was kept)"
qcr_hasnot "$A" "built following the base's local surface normal" "QCR_boss_height_bad: no mesh construction ran"
qcr_num "$(qcr_vol "$A" 0)" 1062.83 0.002 "QCR_boss_height_bad: volume 1000 + pi 2^2 5"
qcr_has "$A" "(polysurface)" "QCR_boss_height_bad: the base is still an exact polysurface, not a mesh"
qcr_hasnot "$A" "(mesh)" "QCR_boss_height_bad: no mesh object was left behind"
A="$(qcr_case boss_tiny)"
qcr_has "$A" "the height must be a number from the model tolerance" "QCR_boss_tiny: a height of 1e-9 is below the model tolerance and is refused"
qcr_has "$A" "Boss: exact kernel route (MakeBoss)" "QCR_boss_tiny: the 5 typed afterwards builds the boss"
qcr_num "$(qcr_vol "$A" 0)" 1062.83 0.002 "QCR_boss_tiny: volume 1000 + pi 2^2 5"

A="$(qcr_case rib_thickness_bad)"
qcr_count "$A" "Rib: Thickness must be a positive number" 3 "QCR_rib_thickness_bad: nan, 0 and -2 are each refused"
qcr_has "$A" "Rib: exact kernel route (MakeRib)" "QCR_rib_thickness_bad: the rib is the exact one"
qcr_num "$(qcr_vol "$A" 0)" 1024 0.001 "QCR_rib_thickness_bad: thickness stayed 1, volume 1000 + 6 x 4 x 1"
A="$(qcr_case rib_thickness_presel)"
qcr_has "$A" "Rib: exact kernel route (MakeRib)" "QCR_rib_thickness_presel: the exact rib"
qcr_num "$(qcr_vol "$A" 0)" 1048 0.001 "QCR_rib_thickness_presel: Rib Thickness=2 with the spine preselected, volume 1000 + 6 x 4 x 2"
A="$(qcr_case rib_thickness_lastwins)"
qcr_num "$(qcr_vol "$A" 0)" 1072 0.001 "QCR_rib_thickness_lastwins: Thickness=2 Thickness=3 on the command line, the last one wins, volume 1000 + 6 x 4 x 3"
A="$(qcr_case rib_thickness_after_nonoption)"
qcr_num "$(qcr_vol "$A" 0)" 1072 0.001 "QCR_rib_thickness_after_nonoption: 'Foo=1 Thickness=3': the token that is no option stops the scan, Thickness=3 still applies later, 1000 + 6 x 4 x 3"
A="$(qcr_case rib_thickness_nosel)"
qcr_num "$(qcr_vol "$A" 0)" 1048 0.001 "QCR_rib_thickness_nosel: Rib Thickness=2 with nothing selected, volume 1000 + 6 x 4 x 2"
A="$(qcr_case rib_undo_redo)"
qcr_num "$(qcr_vol "$A" 1)" 1024 0.001 "QCR_rib_undo_redo: the rib adds 24"
qcr_has "$A" "(polysurface)" "QCR_rib_undo_redo: the objects after Undo include the polysurface"
qcr_num "$(qcr_vol "$A" 0)" 1024 0.001 "QCR_rib_undo_redo: Redo brings the rib back (1024)"

A="$(qcr_case leader_text_first)"
qcr_has "$A" "Leader Hello" "QCR_leader_text_first: a number typed at the first-point prompt is ignored and the leader still builds with its text"
A="$(qcr_case tween_number_first)"
qcr_has "$A" "TweenCurves: 3 curve(s) created" "QCR_tween_number_first: a number typed before the curves are chosen is ignored; 3 tween curves are built afterwards"
qcr_count "$A" "(curve)" 5 "QCR_tween_number_first: the 2 lines and the 3 tween curves"
A="$(qcr_case offsetmultiple_number_first)"
qcr_has "$A" "OffsetMultiple: 3 curve(s) created (3 offsets x 2)" "QCR_offsetmultiple_number_first: the distance 2 typed before the curve was chosen is kept (3 offsets x 2)"
A="$(qcr_case offsetmultiple_options)"
qcr_has "$A" "OffsetMultiple: Count must be a number from 1 to" "QCR_offsetmultiple_options: Count=1000000000 is refused with the range"
qcr_has "$A" "OffsetMultiple: the distance must be a number above 0 and at most" "QCR_offsetmultiple_options: Distance=1e300 is refused with the range"
qcr_has "$A" "OffsetMultiple: 3 curve(s) created (3 offsets x 1)" "QCR_offsetmultiple_options: Distance=1 afterwards builds 3 offsets x 1"

A="$(qcr_case offsetsrf_collapse)"
qcr_has "$A" "passes through the solid" "QCR_offsetsrf_collapse: an inward offset of -6 on a 10 cube is refused and says why"
qcr_hasnot "$A" "(mesh)" "QCR_offsetsrf_collapse: no inside-out mesh was left next to the box"
qcr_num "$(qcr_vol "$A" 0)" 1000 0.001 "QCR_offsetsrf_collapse: the scene still holds exactly the box (1000)"
A="$(qcr_case offsetsrf_refused_undo)"
qcr_num "$(qcr_vol "$A" 1)" 1500 0.001 "QCR_offsetsrf_refused_undo: MoveFace gave 1500"
qcr_num "$(qcr_vol "$A" 2)" 1000 0.001 "QCR_offsetsrf_refused_undo: after the refused OffsetSrf ONE Undo undid the MoveFace (1000)"

A="$(qcr_case copy_group)"
qcr_has "$A" "Copied 2 object(s)" "QCR_copy_group: Copy of the 2 grouped boxes made 2 copies"
qcr_hasnot "$A" "4 object(s) selected" "QCR_copy_group: SelGroup on an original selects its own 2 objects, not the copies as well (4)"
A="$(qcr_case array_group)"
qcr_has "$A" "ArrayLinear created 4 object(s)" "QCR_array_group: ArrayLinear 3 of the 2 grouped boxes made 4 copies"
qcr_hasnot "$A" "6 object(s) selected" "QCR_array_group: SelGroup on an original selects its own 2 objects, not the 4 copies as well (6)"

A="$(qcr_case offset_number_first)"
P0="$(qcr_cv "$A" 0)"; P1="$(qcr_cv "$A" 1)"
if [ "$P0" = "0,3,0" ] && [ "$P1" = "10,3,0" ]; then qcr_ok "QCR_offset_number_first: the offset segment is (0,3,0)-(10,3,0), on the side of the clicked point"
else qcr_bad "QCR_offset_number_first: the offset segment is (${P0:-none})-(${P1:-none}), expected (0,3,0)-(10,3,0)"; fi

A="$(qcr_case subd_refused_undo)"
qcr_has "$A" "has no mirror partner within the tolerance" "QCR_subd_refused_undo: the move of the partnerless vertex is refused"
qcr_has "$A" "plane through (2, 0, 0): NOT symmetric, 8 vertices (0 on the plane, 6 paired, 2 unpaired)" "QCR_subd_refused_undo: the sculpt broke the symmetry (2 unpaired)"
qcr_has "$A" "plane through (2, 0, 0): symmetric, 8 vertices (0 on the plane, 8 paired, 0 unpaired)" "QCR_subd_refused_undo: after the refusal ONE Undo undid the sculpt (the refusal left no undo step)"
A="$(qcr_case subd_crease_refused_redo)"
qcr_count "$A" "Crease edges: 2" 2 "QCR_subd_crease_refused_redo: 2 crease edges after the crease and again after Redo (the refused smoothing did not clear the Redo stack)"
qcr_has "$A" "Crease edges: 0" "QCR_subd_crease_refused_redo: Undo removed the crease"

A="$(qcr_case arraycrv_points)"
for x in 1 3.5 6 8.5 11; do qcr_has "$A" "  $x,1,0" "QCR_arraycrv_points: a copy at x = $x (the point 1 + the path point 0, 2.5, 5, 7.5, 10)"; done
qcr_count "$A" "(point)" 7 "QCR_arraycrv_points: 6 points after the array (original + 5 copies) and 1 after Undo"
A="$(qcr_case arraycrv_counts)"
qcr_count "$A" "the number of items must be a number from 2 to" 4 "QCR_arraycrv_counts: 0, 1, -4 and 1e9 are refused with the range"
qcr_has "$A" "ArrayCrv: 3 object(s) placed along the curve" "QCR_arraycrv_counts: the 3 typed afterwards still arrays"
A="$(qcr_case arraysrf_centroid)"
qcr_has "$A" "Centroid 5,5,5.5" "QCR_arraysrf_centroid: the copy's volume centroid is (5,5,5.5)"
A="$(qcr_case arraycrvonsrf_count)"
qcr_has "$A" "ArrayCrvOnSrf: the number of items must be a number from 2 to" "QCR_arraycrvonsrf_count: 200000 items is refused with the range"
qcr_has "$A" "ArrayCrvOnSrf: 3 object(s) placed along the curve" "QCR_arraycrvonsrf_count: the 3 typed afterwards still arrays"

A="$(qcr_case moveface_undo_redo)"
qcr_num "$(qcr_vol "$A" 1)" 1500 0.001 "QCR_moveface_undo_redo: the top face moved up 5 gives 1500"
qcr_num "$(qcr_vol "$A" 2)" 1000 0.001 "QCR_moveface_undo_redo: Undo restores the box (1000)"
qcr_num "$(qcr_vol "$A" 3)" 1500 0.001 "QCR_moveface_undo_redo: Redo gives 1500 again"

A="$(qcr_case pocket_depth_bad)"
qcr_count "$A" "the depth must be a number from the model tolerance" 2 "QCR_pocket_depth_bad: 0 and -3 are refused"
qcr_has "$A" "Pocket: exact kernel route (MakePocket)" "QCR_pocket_depth_bad: the 3 typed afterwards cuts the exact pocket"
qcr_num "$(qcr_vol "$A" 0)" 962.30 0.002 "QCR_pocket_depth_bad: 1000 - pi 2^2 3"
qcr_has "$A" "(polysurface)" "QCR_pocket_depth_bad: the result is an exact polysurface"

# ---------------------------------------------------------------------------------------------------------------------------------
# A dimension that was Moved / Rotated / Copied before Save3dm. A Dino 8 dimension keeps what it measures (DimP0 / DimP1 / DimPlane*)
# as tags next to its baked curves, and Save3dm rebuilds the real dimension from those tags; SceneObject::Transform moved only the
# curves, so the file held the dimension at its ORIGINAL place (Move up 100: reopened with DimP0 = 0,0,0). Two app processes (write,
# then a fresh-process reopen, as tests/sections/63_dimwr.sh does), the reopened tags read with GetUserText. DimLinear 0,0,0 -> 10,0,0:
#   Move 0,100,0         : DimP0 = (0,100,0), DimP1 = (10,100,0), plane origin (0,100,0)
#   Rotate 90 about 0    : DimP1 = (0,10,0), plane x axis (0,1,0)
#   Copy by 0,100,0      : one dimension at DimP0 = (0,0,0) and one at (0,100,0)
qcr_exec() {  # qcr_exec <script>
  if [ "$QCR_PLAIN" = 1 ]; then timeout 300 "$BIN" --smoke 30 --script "$1" 2>&1; else timeout 300 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$1" 2>&1; fi
}
# qcr_tagnear <text> <tag> <x> <y> <z> <what>: some "<n>: <tag> = a,b,c" line within 1e-6 of (x,y,z)
qcr_tagnear() {
  local seen
  seen="$({ grep -E ": $2 = " <<<"$1" || true; } | sed -E "s/.*: $2 = //")"
  if [ -n "$seen" ] && awk -F, -v x="$3" -v y="$4" -v z="$5" 'BEGIN { ok = 0 } NF == 3 { dx = $1 - x; dy = $2 - y; dz = $3 - z; if (dx < 0) dx = -dx; if (dy < 0) dy = -dy; if (dz < 0) dz = -dz; if (dx < 1e-6 && dy < 1e-6 && dz < 1e-6) ok = 1 } END { exit !ok }' <<<"$seen"; then
    qcr_ok "$6"
  else
    qcr_bad "$6 (no $2 = $3,$4,$5; reopened: $(sort -u <<<"$seen" | tr '\n' ' '))"
  fi
}
for mode in move rotate copy; do
  case "$mode" in
    move)   QCR_OPS=$'SelAll\nMove\n0,0,0\n0,100,0' ;;
    rotate) QCR_OPS=$'SelAll\nRotate\n0,0,0\n90' ;;
    copy)   QCR_OPS=$'SelAll\nCopy\n0,0,0\n0,100,0\nEnter' ;;
  esac
  printf 'SetActiveViewport Top\nNew\nDimLinear 0,0,0 10,0,0 5,-3,0\n%s\nSaveAs %s/qcr_dim_%s.3dm\n' "$QCR_OPS" "$TMPW" "$mode" > "$TMPW/qcr_dim_${mode}_w.txt"
  printf 'Open %s/qcr_dim_%s.3dm\nSelAll\nGetUserText\n' "$TMPW" "$mode" > "$TMPW/qcr_dim_${mode}_r.txt"
  qcr_exec "$TMPW/qcr_dim_${mode}_w.txt" >/dev/null || true
  qcr_exec "$TMPW/qcr_dim_${mode}_r.txt" > "$TMPW/qcr_dim_${mode}_r.out" || true
done
QCR_DIM_move="$(cat "$TMPW/qcr_dim_move_r.out")"
QCR_DIM_rotate="$(cat "$TMPW/qcr_dim_rotate_r.out")"
QCR_DIM_copy="$(cat "$TMPW/qcr_dim_copy_r.out")"
qcr_tagnear "$QCR_DIM_move" DimP0 0 100 0 "QCR_dim_move: a DimLinear moved up 100 is saved with DimP0 = (0,100,0)"
qcr_tagnear "$QCR_DIM_move" DimP1 10 100 0 "QCR_dim_move: ... DimP1 = (10,100,0)"
qcr_tagnear "$QCR_DIM_move" DimPlaneOrigin 0 100 0 "QCR_dim_move: ... the dimension plane origin moved with it"
qcr_tagnear "$QCR_DIM_rotate" DimP1 0 10 0 "QCR_dim_rotate: a DimLinear rotated 90 degrees about the origin is saved with DimP1 = (0,10,0)"
qcr_tagnear "$QCR_DIM_rotate" DimPlaneX 0 1 0 "QCR_dim_rotate: ... and its plane x axis turned to (0,1,0)"
qcr_tagnear "$QCR_DIM_copy" DimP0 0 100 0 "QCR_dim_copy: the copy 100 up is saved at DimP0 = (0,100,0)"
qcr_tagnear "$QCR_DIM_copy" DimP0 0 0 0 "QCR_dim_copy: the original is still saved at DimP0 = (0,0,0)"
