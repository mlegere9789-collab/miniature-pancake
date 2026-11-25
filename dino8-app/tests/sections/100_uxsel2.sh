# ---------------------------------------------------------------------------
# UXSEL2_: selection by object type, identity, name, group and duplicate on a hand-built scene whose results follow from how it is built. Every step checks
# the count AND which objects were selected (bare SelID lists the selected ids), and the scene holds decoys that a wrong implementation would pick up.
# Rows D8-RHINO-SELECT-0035 SelCrv, 0064 SelLine, 0087 SelPlanarCrv, 0091 SelPt, 0068 SelMesh, 0021 SelClosedMesh, 0082 SelOpenMesh,
# 0056 SelID, 0078 SelName, 0054 SelGroup, 0046 SelDup (and 0047 SelDupAll, same scene).
# Scene, in creation order (ids 1..15): ids 1, 2, 3 three identical lines (0,0,0 to 10,0,0); id 4 a different line; id 5 a circle and id 6 an arc, both named
# Ring; id 7 a closed planar polyline; id 8 an open polyline of four points that are not coplanar (0,60,0 5,60,0 5,65,0 5,65,5: the triple product of the
# three edge vectors is 5 x 5 x 5 = 125, not 0); id 9 a closed mesh box; id 10 an open one-face mesh plane; ids 11 and 12 points; id 13 a second closed mesh
# box (so closed and open meshes are 2 and 1, and a swapped or merged predicate shows); ids 14 and 15 two diagonals (0,100,0 to 10,110,0 and 0,110,0 to
# 10,100,0), which have the same degree, control point count, length and bounding box but are not the same geometry (a duplicate test that compares a text
# summary would call them duplicates).
# Oracles are read off that construction: curves 1-8 and 14-15; lines 1-4 and 14-15; planar curves all but 8; points 11 and 12; meshes 9, 10 and 13, of which
# 9 and 13 are closed (Euler V - E + F = 8 - 12 + 6 = 2, no naked edge) and 10 is open (4 naked edges); SelID 5 selects id 5 and nothing else; the objects
# named Ring are 5 and 6; of the three identical lines SelDup keeps the first and selects 2 and 3, SelDupAll selects 1, 2 and 3 and neither touches the diagonals;
# two named groups (Alpha = 1 2 3, Beta = 5 6) are found by name, an unknown name finds nothing, and no name finds every grouped object (1 2 3 5 6).
# That Rhino's commands select the same sets is recalled, needs owner check; SelDup leaving the first of identical objects unselected is also recalled.
# Not tested: window and crossing picks, sub-object filters (need pointer input).
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXSEL2 (not run on Windows: it uses the Linux headless launch)"
else
UXSEL2_S="$TMPW/uxsel2_script.txt"
{
  cat <<EOS
@wait 2
SetActiveViewport Top
Line 0,0,0 10,0,0
Line 0,0,0 10,0,0
Line 0,0,0 10,0,0
Line 20,0,0 30,0,0
Circle 40,0,0 5
SelLast
SetObjectName Ring
Arc 60,0,0 60,5,0 65,0,0
SelLast
SetObjectName Ring
Polyline 0,40,0 5,40,0 5,45,0 0,45,0 0,40,0
Enter
Polyline 0,60,0 5,60,0 5,65,0 5,65,5
Enter
MeshBox 60,20,0 70,26,0 4
MeshPlane 100,0,0 110,10,0
Point 0,-10,0
Point 1,-10,0
MeshBox 80,40,0 90,46,0 4
Line 0,100,0 10,110,0
Line 0,110,0 10,100,0
SelNone
EOS
  step() {  # block-name, commands... : SelNone, the commands, then the selected ids
    local b="$1"; shift
    echo "UXBEGIN_$b"; echo "SelNone"; for c in "$@"; do echo "$c"; done; echo "SelID"; echo "SelNone"
  }
  step crv "SelCrv"; step line "SelLine"; step planar "SelPlanarCrv"; step pt "SelPt"; step mesh "SelMesh"
  step closedmesh "SelClosedMesh"; step openmesh "SelOpenMesh"
  step id5 "SelID 5"; step id99 "SelID 99"
  step ring "SelName Ring"; step nothing "SelName Nothing"
  step dup "SelDup"; step dupall "SelDupAll"
  echo "UXBEGIN_makegroups"; echo "SelNone"; echo "SelID 1 2 3"; echo "Group"; echo "SetGroupName Alpha"; echo "SelNone"; echo "SelID 5 6"; echo "Group"; echo "SetGroupName Beta"; echo "SelNone"
  step alpha "SelGroup Alpha"; step beta "SelGroup Beta"; step nogroup "SelGroup Nope"; step allgroups "SelGroup"
  echo "UXBEGIN_end"
} > "$UXSEL2_S"
if ux_launch "$UXSEL2_S"; then
  uxsel2_ids() {  # block label expected-ids(space separated, sorted numerically; empty for none)
    local got
    got="$(ux_lines "$1" | { grep -E '^Object id [0-9]+$' || true; } | sed 's/Object id //' | sort -n | tr '\n' ' ' | sed 's/ $//')"
    if [ "$got" = "$3" ]; then ux_ok "UXSEL2_$2: selects exactly objects [$got]"; else ux_bad "UXSEL2_$2: selected [$got], expected [$3]"; ux_lines "$1" | head -8 | sed 's/^/     actual: /' || true; fi
  }
  uxsel2_ids crv SelCrv "1 2 3 4 5 6 7 8 14 15"
  uxsel2_ids line SelLine "1 2 3 4 14 15"
  uxsel2_ids planar SelPlanarCrv "1 2 3 4 5 6 7 14 15"
  uxsel2_ids pt SelPt "11 12"
  uxsel2_ids mesh SelMesh "9 10 13"
  uxsel2_ids closedmesh SelClosedMesh "9 13"
  uxsel2_ids openmesh SelOpenMesh "10"
  uxsel2_ids id5 SelID "5"
  uxsel2_ids id99 SelID_missing ""
  ux_has UXSEL2_SelID_missing id99 "an id that does not exist is reported" "No object with id 99"
  uxsel2_ids ring SelName "5 6"
  uxsel2_ids nothing SelName_unused ""
  uxsel2_ids dup SelDup "2 3"
  uxsel2_ids dupall SelDupAll "1 2 3"
  uxsel2_ids alpha SelGroup_Alpha "1 2 3"
  uxsel2_ids beta SelGroup_Beta "5 6"
  uxsel2_ids nogroup SelGroup_Nope ""
  ux_has UXSEL2_SelGroup_Nope nogroup "an unknown group name is reported" "SelGroup: no group named 'Nope'"
  uxsel2_ids allgroups SelGroup_all "1 2 3 5 6"
  if grep -q '^FAIL' <<<"$UX_OUT"; then ux_bad "UXSEL2_script: an @expect line failed"; echo "$UX_OUT" | grep '^FAIL' | head -5; fi
fi
fi
