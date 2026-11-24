# ---------------------------------------------------------------------------
# UXSEL2_: selection by object type, identity and name on a hand-built scene whose counts follow from how it is built.
# Rows D8-RHINO-SELECT-0035 SelCrv, 0064 SelLine, 0087 SelPlanarCrv, 0091 SelPt, 0068 SelMesh, 0021 SelClosedMesh, 0082 SelOpenMesh,
# 0056 SelID, 0078 SelName, 0054 SelGroup, 0046 SelDup (and 0047 SelDupAll, same scene).
# Scene, in creation order (ids 1..12): ids 1, 2, 3 are three identical lines (0,0,0 to 10,0,0), id 4 a different line, id 5 a circle,
# id 6 an arc, id 7 a closed planar polyline, id 8 an open polyline of four points that are not coplanar (0,60,0 5,60,0 5,65,0 5,65,5: the
# triple product of the three edge vectors is 5 x 5 x 5 = 125, not 0), id 9 a closed mesh box, id 10 an open one-face mesh plane, ids 11 and 12 points.
# Oracles are counts read off that construction: curves 8; lines 4; planar curves 7 (all but id 8); points 2; meshes 2 of which 1 closed
# (Euler: V - E + F = 8 - 12 + 6 = 2, no naked edge) and 1 open (4 naked edges); SelID n selects exactly the object with id n; two objects named
# Ring give 2; 7 curves grouped then SelGroup gives 7; of three identical lines SelDup keeps the first and selects the 2 copies, SelDupAll selects all 3.
# Not tested: window/crossing picks and the sub-object filters (need pointer input).
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXSEL2 (not run on Windows: it uses the Linux headless launch)"
else
UXSEL2_S="$TMPW/uxsel2_script.txt"
cat > "$UXSEL2_S" <<EOS
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
MeshPlane 100,0,0 110,10,0 1 1
Point 0,-10,0
Point 1,-10,0
SelNone
UXBEGIN_crv
SelCrv
@expect_selected 8
SelNone
UXBEGIN_line
SelLine
@expect_selected 4
SelNone
UXBEGIN_planar
SelPlanarCrv
@expect_selected 7
SelNone
UXBEGIN_pt
SelPt
@expect_selected 2
SelNone
UXBEGIN_mesh
SelMesh
@expect_selected 2
SelNone
UXBEGIN_closedmesh
SelClosedMesh
@expect_selected 1
SelNone
UXBEGIN_openmesh
SelOpenMesh
@expect_selected 1
SelNone
UXBEGIN_id
SelID 5
@expect_selected 1
SelNone
SelID 99
@expect_selected 0
UXBEGIN_name
SelNone
SelName Ring
@expect_selected 2
SelNone
SelName Nothing
@expect_selected 0
UXBEGIN_dup
SelNone
SelDup
@expect_selected 2
SelNone
SelDupAll
@expect_selected 3
SelNone
UXBEGIN_group
SelCrv
Group
SelNone
SelGroup
@expect_selected 8
UXBEGIN_end
EOS
if ux_launch "$UXSEL2_S"; then
  uxsel2_n() {  # block label expected-count
    if grep -qF "ok   expect_selected $3 (got $3)" <<<"$(ux_lines "$1")"; then ux_ok "UXSEL2_$2: exactly $3 selected"; else ux_bad "UXSEL2_$2: expected $3 selected"; ux_lines "$1" | head -6 | sed 's/^/     actual: /' || true; fi
  }
  uxsel2_n crv SelCrv 8
  uxsel2_n line SelLine 4
  uxsel2_n planar SelPlanarCrv 7
  uxsel2_n pt SelPt 2
  uxsel2_n mesh SelMesh 2
  uxsel2_n closedmesh SelClosedMesh 1
  uxsel2_n openmesh SelOpenMesh 1
  if grep -qF "ok   expect_selected 1 (got 1)" <<<"$(ux_lines id)" && grep -qF "ok   expect_selected 0 (got 0)" <<<"$(ux_lines id)"; then ux_ok "UXSEL2_SelID: id 5 selects exactly that object, an id that does not exist selects none"; else ux_bad "UXSEL2_SelID: expected 1 then 0"; ux_lines id | head -8 | sed 's/^/     actual: /' || true; fi
  if grep -qF "ok   expect_selected 2 (got 2)" <<<"$(ux_lines name)" && grep -qF "ok   expect_selected 0 (got 0)" <<<"$(ux_lines name)"; then ux_ok "UXSEL2_SelName: the two objects named Ring are selected, an unused name selects none"; else ux_bad "UXSEL2_SelName: expected 2 then 0"; ux_lines name | head -8 | sed 's/^/     actual: /' || true; fi
  if grep -qF "ok   expect_selected 2 (got 2)" <<<"$(ux_lines dup)" && grep -qF "ok   expect_selected 3 (got 3)" <<<"$(ux_lines dup)"; then ux_ok "UXSEL2_SelDup: of three identical lines SelDup selects the 2 copies and SelDupAll all 3"; else ux_bad "UXSEL2_SelDup: expected 2 then 3"; ux_lines dup | head -8 | sed 's/^/     actual: /' || true; fi
  uxsel2_n group SelGroup 8
  if grep -q '^FAIL' <<<"$UX_OUT"; then ux_bad "UXSEL2_script: an @expect line failed"; echo "$UX_OUT" | grep '^FAIL' | head -5; fi
fi
fi
