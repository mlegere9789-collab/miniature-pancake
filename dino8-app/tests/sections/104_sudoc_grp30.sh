# ---------------------------------------------------------------------------
# SUDOC_GRP30: Make Group, 30 faces, then Ungroup (D8-SKETCHUP-ORG-0001). 35 unit planes (faces): 30 at x = 2 i (i = 0..29), 5 loose ones at x = 100 + 2 j (j = 0..4), a
# millimetre document. The 30 are grouped; SelGroup then reaches exactly those 30, which Export writes to g30.obj, read by the stand-alone rwi_oracle.py (the "o" groups are its
# parts): 30 parts, bounding box (0,0,0)-(59,1,0) (the last grouped plane ends at 2 x 29 + 1), none of the 5 loose planes (they would reach x = 109). Ungroup restores the loose
# state: SelGroup then selects nothing (the app's own expect_selected line) and SelAll writes all 35 planes to all.obj: 35 parts, bounding box (0,0,0)-(109,1,0).
# Decoys: the oracle must reject 31 parts for g30.obj and 30 for all.obj. Scope: the objects are separate B-reps, so "geometry no longer merges" has no counterpart here.
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip SUDOC_GRP30 (needs python3)"
else
. "$HERE/rwi_lib.sh"
SUDOC_G_D="$TMPW/sudoc_grp30"; mkdir -p "$SUDOC_G_D"
{
  echo "SetActiveViewport Top"; echo "New"
  for i in $(seq 0 29); do echo "Plane $((2 * i)),0,0 $((2 * i + 1)),1,0"; done
  for j in $(seq 0 4); do echo "Plane $((100 + 2 * j)),0,0 $((101 + 2 * j)),1,0"; done
  echo "SelID $(seq -s ' ' 1 30)"
  echo "Group"
  echo "SelNone"
  echo "SelGroup"
  echo "Export $SUDOC_G_D/g30.obj"
  echo "Ungroup"
  echo "SelNone"
  echo "SelGroup"
  echo "@expect_selected 0"
  echo "SelAll"
  echo "Export $SUDOC_G_D/all.obj"
} > "$TMPW/sudoc_grp30_script.txt"
rwi_app "$TMPW/sudoc_grp30_script.txt"
rwi_has "created with 30 object(s)" "SUDOC_GRP30: Group reports a group of 30 objects"
rwi_has "30 object(s) selected" "SUDOC_GRP30: SelGroup selects exactly the 30 grouped faces"
rwi_oracle "SUDOC_GRP30: g30.obj parsed independently: the 30 grouped faces only: 30 parts, bbox (0,0,0)-(59,1,0)" obj "$SUDOC_G_D/g30.obj" --parts 30 --min 0,0,0 --max 59,1,0 --tol 1e-6
if python3 -I "$HERE/rwi_oracle.py" obj "$SUDOC_G_D/g30.obj" --parts 31 >/dev/null 2>&1; then rwi_bad "SUDOC_GRP30_decoy: the oracle accepted 31 parts for g30.obj"; else rwi_ok "SUDOC_GRP30_decoy: the oracle rejects 31 parts for g30.obj"; fi
rwi_has "ok   expect_selected 0 (got 0)" "SUDOC_GRP30: after Ungroup the group is gone: SelGroup selects nothing"
rwi_oracle "SUDOC_GRP30: all.obj parsed independently: 35 faces: 35 parts, bbox (0,0,0)-(109,1,0)" obj "$SUDOC_G_D/all.obj" --parts 35 --min 0,0,0 --max 109,1,0 --tol 1e-6
if python3 -I "$HERE/rwi_oracle.py" obj "$SUDOC_G_D/all.obj" --parts 30 >/dev/null 2>&1; then rwi_bad "SUDOC_GRP30_decoy: the oracle accepted 30 parts for all.obj"; else rwi_ok "SUDOC_GRP30_decoy: the oracle rejects 30 parts for all.obj"; fi
fi
