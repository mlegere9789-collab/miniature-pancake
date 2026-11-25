# ---------------------------------------------------------------------------
# GRP_: native ON_Group table in .3dm (grp_export_script.txt, grp_reopen_script.txt). Save3dm now writes the user's own groups (the
# Group command) as real ON_Groups with the members' attributes joined to them, and Load3dm reads a native group table (a Rhino-made
# file with no "Dino8.GroupId" strings) into Document groups. group3dm_check is a plain OpenNURBS ONX_Model reader/author, independent
# of Dino 8's Save3dm / Load3dm. Internal groups (a Leader's baked pieces) are NOT exposed in the group table.
#   export   : Group1 {point 0,0,0; point 1,0,0}, Group2 {line at y=5; line at y=6}, loose point 9,9,9 -> exactly 2 groups, right members
#   leader   : the same plus a Leader (internal group) -> still exactly 2 groups, and no Leader piece carries a group
#   reopen   : Dino's own file -> SelGroup finds the 4 grouped objects
#   fixture  : OpenNURBS-authored Alpha {p1,p2}, Beta {p3,line}, p5 in both (Alpha, Beta), p4 loose -> SelGroup finds 5 (was 0)
#   resave   : the fixture re-saved by Dino 8 -> groups Alpha / Beta with the same membership (p5 keeps its outermost group, Beta)
# ---------------------------------------------------------------------------
GRPBIN="$(dirname "$BIN")/group3dm_check"
if [ -x "$GRPBIN" ]; then
  mkdir -p "$TMPW/grp"
  "$GRPBIN" fixture "$TMPW/grp/grp_fixture.3dm" >/dev/null || { echo "FAIL GRP_fixture: group3dm_check could not author the fixture"; fail=1; }
  sed "s|@TMP@|$TMPW/grp|g" "$HERE/grp_export_script.txt" > "$TMPW/grp_export_script.txt"
  sed "s|@TMP@|$TMPW/grp|g" "$HERE/grp_reopen_script.txt" > "$TMPW/grp_reopen_script.txt"
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    GRE="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/grp_export_script.txt" 2>&1)" || { echo "$GRE" | tail -20; echo "FAIL GRP_run: export script exited non-zero"; fail=1; GRE=""; }
    GRI="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/grp_reopen_script.txt" 2>&1)" || { echo "$GRI" | tail -20; echo "FAIL GRP_run: reopen script exited non-zero"; fail=1; GRI=""; }
  else
    GRE="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/grp_export_script.txt" 2>&1)" || { echo "$GRE" | tail -20; echo "FAIL GRP_run: export script exited non-zero"; fail=1; GRE=""; }
    GRI="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/grp_reopen_script.txt" 2>&1)" || { echo "$GRI" | tail -20; echo "FAIL GRP_run: reopen script exited non-zero"; fail=1; GRI=""; }
  fi
  grp_has() { if grep -qFx -- "$2" <<<"$1"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected line: $2"; fail=1; fi; }
  grp_dump() { "$GRPBIN" dump "$1" 2>&1 || echo "dump failed: $1"; }
  GRD_E="$(grp_dump "$TMPW/grp/grp_export.3dm")"
  grp_has "$GRD_E" "groups: 2" "GRP_export: the saved file has exactly 2 native groups (an OpenNURBS reader, not Load3dm)"
  grp_has "$GRD_E" "group 0 name=Group1" "GRP_export: group 0 is Group1 (the Group command's own name)"
  grp_has "$GRD_E" "group 1 name=Group2" "GRP_export: group 1 is Group2"
  grp_has "$GRD_E" "object point at=(0,0,0) groups=Group1" "GRP_export: the first point is in Group1"
  grp_has "$GRD_E" "object point at=(1,0,0) groups=Group1" "GRP_export: the second point is in Group1"
  grp_has "$GRD_E" "object curve at=(0,5,0) groups=Group2" "GRP_export: the first line is in Group2"
  grp_has "$GRD_E" "object curve at=(0,6,0) groups=Group2" "GRP_export: the second line is in Group2"
  grp_has "$GRD_E" "object point at=(9,9,9) groups=" "GRP_export: the loose point is in no group"
  GRD_L="$(grp_dump "$TMPW/grp/grp_leader.3dm")"
  grp_has "$GRD_L" "groups: 2" "GRP_leader: adding a Leader (an internal group) leaves exactly 2 native groups"
  GRP_N="$(grep -c "groups=Group" <<<"$GRD_L" || true)"
  [ "$GRP_N" = "4" ] && echo "ok   GRP_leader: only the 4 user-grouped objects carry a group; no Leader piece does" || { echo "FAIL GRP_leader: $GRP_N objects carry a group (want 4)"; fail=1; }
  grp_obj() { grep -c "^object" <<<"$1" || true; }
  GRP_NO="$(grp_obj "$GRD_L")"
  [ "${GRP_NO:-0}" -ge 6 ] 2>/dev/null && echo "ok   GRP_leader: the Leader is in the file too ($GRP_NO objects)" || { echo "FAIL GRP_leader: object count '$GRP_NO'"; fail=1; }
  grp_sel() { if grep -qF -- "$2" <<<"$1"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; { grep -E "object\(s\) selected|Opened" <<<"$1" || true; } | head -4 | sed 's/^/     actual:   /'; fail=1; fi; }
  grp_sel "$GRI" "4 object(s) selected" "GRP_reopen: Dino 8's own file reopens with its 4 grouped objects (SelGroup)"
  grp_sel "$GRI" "5 object(s) selected" "GRP_fixture: an OpenNURBS-authored native group table is read: SelGroup finds the 5 grouped objects (0 before)"
  GRD_R="$(grp_dump "$TMPW/grp/grp_resave.3dm")"
  grp_has "$GRD_R" "groups: 2" "GRP_resave: the re-saved fixture has 2 native groups"
  grp_has "$GRD_R" "group 0 name=Alpha" "GRP_resave: group 0 keeps the name Alpha"
  grp_has "$GRD_R" "group 1 name=Beta" "GRP_resave: group 1 keeps the name Beta"
  grp_has "$GRD_R" "object point at=(1,0,0) groups=Alpha" "GRP_resave: p1 in Alpha"
  grp_has "$GRD_R" "object point at=(2,0,0) groups=Alpha" "GRP_resave: p2 in Alpha"
  grp_has "$GRD_R" "object point at=(3,0,0) groups=Beta" "GRP_resave: p3 in Beta"
  grp_has "$GRD_R" "object curve at=(0,5,0) groups=Beta" "GRP_resave: the line in Beta"
  grp_has "$GRD_R" "object point at=(4,0,0) groups=" "GRP_resave: p4 still in no group"
  grp_has "$GRD_R" "object point at=(5,0,0) groups=Beta" "GRP_resave: p5 (in Alpha and Beta) keeps its outermost group Beta"
else
  echo "FAIL group3dm_check was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the native .3dm group checks"
  fail=1
fi
