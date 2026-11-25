# ---------------------------------------------------------------------------
# UXMESHSEL_: selecting mesh faces by measure and mesh edges by the angle between the two faces, on two meshes whose face geometry is known in closed form.
# Rows D8-RHINO-SELECT-0070 SelMeshFacesByArea, 0071 SelMeshFacesByAspectRatio, 0072 SelMeshFacesByDraftAngle, 0073 SelMeshFacesByEdgeLength,
# 0069 SelMeshEdges. The ExtractMeshFacesBy* commands share the measures and the range test, and the same box is used to show that they pick the same faces.
# Scene: id 1 = MeshBox 0,0,0 10,6,0 4, a 10 x 6 x 4 box of six quads: two of 10 x 6 (area 60, sides 10 and 6, aspect 10/6 = 1.667, face plane square to Z),
# two of 10 x 4 (area 40, aspect 2.5, parallel to Z), two of 6 x 4 (area 24, aspect 1.5, parallel to Z); its 12 edges are all 90 degree joins (Euler V - E + F
# = 8 - 12 + 6 = 2). id 2 = MeshPlane over 10 x 10, tessellated 8 x 8 into 128 right triangles with legs 1.25 and hypotenuse 1.25 sqrt 2 = 1.7678: area
# 1.25 x 1.25 / 2 = 0.78125, aspect sqrt 2 = 1.4142, flat so every face plane is square to Z; 81 vertices, 208 edges (72 + 72 + 64), 32 on the boundary, so 176
# interior edges (Euler V - E + F = 81 - 208 + 128 = 1). The semantics of the measures are written on each command (area in [min, max]; aspect = longest / shortest
# side, selected when greater than the limit; edge length = every side in [min, max]; draft angle = angle between the face plane and the CPlane Z axis,
# 0 for a wall parallel to Z); that these are Rhino's exact definitions is recalled, needs owner check. Not tested: picking the faces with the mouse.
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXMESHSEL (not run on Windows: it uses the Linux headless launch)"
else
UXMS_S="$TMPW/uxmeshsel_script.txt"
{
  echo "@wait 2"; echo "SetActiveViewport Top"
  echo "MeshBox 0,0,0 10,6,0 4"; echo "MeshPlane 100,0,0 110,10,0"
  echo "UXBEGIN_none"; echo "SelNone"; echo "SelMeshFacesByArea 1"; echo "SelMeshEdges 45"
  echo "UXBEGIN_area"; echo "SelNone"; echo "SelID 1"
  for a in "30 50" "20 30" "55" "0 100" "100 200"; do echo "SelMeshFacesByArea $a"; done
  echo "UXBEGIN_aspect"; for a in 2 1.6 1.4 3; do echo "SelMeshFacesByAspectRatio $a"; done
  echo "UXBEGIN_edge"; for a in "5 10" "4 6" "4 10" "7 10"; do echo "SelMeshFacesByEdgeLength $a"; done
  echo "UXBEGIN_draft"; for a in 80 "0 1" "10 80" "0 90"; do echo "SelMeshFacesByDraftAngle $a"; done
  echo "UXBEGIN_bad"; echo "SelMeshFacesByArea abc"; echo "SelMeshFacesByArea"; echo "SelMeshEdges 200"
  echo "UXBEGIN_boxedges"; for a in 45 90 91; do echo "SelMeshEdges $a"; done
  echo "UXBEGIN_plane"; echo "SelNone"; echo "SelID 2"
  echo "SelMeshFacesByArea 0.78 0.79"; echo "SelMeshFacesByEdgeLength 1.2 1.8"; echo "SelMeshFacesByEdgeLength 1.2 1.5"
  echo "SelMeshFacesByAspectRatio 1.4"; echo "SelMeshFacesByAspectRatio 1.5"; echo "SelMeshFacesByDraftAngle 89"
  echo "SelMeshEdges 0"; echo "SelMeshEdges 1"
  # The Extract twins on fresh copies of the box (each extraction takes the faces out of its box): the same measures and ranges must pick the same number of faces.
  x=200
  ext() { local b="$1" c="$2" a="$3"; x=$((x + 20)); echo "UXBEGIN_$b"; echo "MeshBox $x,0,0 $((x + 10)),6,0 4"; echo "SelLast"; echo "$c $a"; }
  ext xdraft80 ExtractMeshFacesByDraftAngle "80 90"; ext xdraft0 ExtractMeshFacesByDraftAngle "0 1"
  ext xedge510 ExtractMeshFacesByEdgeLength "5 10"; ext xedge46 ExtractMeshFacesByEdgeLength "4 6"
  ext xarea ExtractMeshFacesByArea "30 50"; ext xaspect ExtractMeshFacesByAspectRatio "2 1000"
  echo "UXBEGIN_end"
} > "$UXMS_S"
if ux_launch "$UXMS_S"; then
  uxms_counts() {  # block command expected-counts-as-one-string label
    local got
    got="$(ux_lines "$1" | { grep -F "$2" || true; } | sed -n 's/.*: \([0-9][0-9]*\) matching.*/\1/p' | tr '\n' ' ' | sed 's/ $//')"
    if [ "$got" = "$3" ]; then ux_ok "UXMESHSEL_$2: $4 (matches: $got)"; else ux_bad "UXMESHSEL_$2: $4 (matches '$got', expected '$3')"; ux_lines "$1" | head -8 | sed 's/^/     actual: /' || true; fi
  }
  uxms_counts area SelMeshFacesByArea "2 2 2 6 0" "box faces by area: [30,50] gets the two 10 x 4 faces, [20,30] the two 6 x 4, 55 and up the two 10 x 6, [0,100] all six, [100,200] none"
  uxms_counts aspect SelMeshFacesByAspectRatio "2 4 6 0" "box faces over aspect ratio 2 (the two at 2.5), 1.6 (plus the two at 1.667), 1.4 (all six, the smallest is 1.5), 3 (none)"
  uxms_counts edge SelMeshFacesByEdgeLength "2 2 6 0" "box faces with every side in [5,10] (the two 10 x 6), [4,6] (the two 6 x 4), [4,10] (all), [7,10] (none)"
  uxms_counts draft SelMeshFacesByDraftAngle "2 4 0 6" "box faces by draft angle: 80 and up the top and bottom, [0,1] the four walls, [10,80] none, [0,90] all"
  ux_has UXMESHSEL_noselection none "with no mesh selected the face command says so" "SelMeshFacesByArea: select meshes or SubDs first"
  ux_has UXMESHSEL_noselection none "with no mesh selected the edge command says so" "Select meshes or SubDs first"
  ux_has UXMESHSEL_badargs bad "a non-number is refused" "SelMeshFacesByArea: expected a number, got 'abc'"
  ux_has UXMESHSEL_badargs bad "a missing number is refused" "SelMeshFacesByArea: expected 1 or 2 number(s), got 0"
  ux_has UXMESHSEL_badargs bad "an angle above 180 is refused" "SelMeshEdges: the angle must be a number from 0 to 180 degrees, got '200'"
  ux_has UXMESHSEL_ExtractDraft xdraft80 "ExtractMeshFacesByDraftAngle 80 90 takes the top and bottom, as the Sel command does" "ExtractMeshFacesByDraftAngle: extracted 2 of 6 faces"
  ux_has UXMESHSEL_ExtractDraft xdraft0 "ExtractMeshFacesByDraftAngle 0 1 takes the four walls, as the Sel command does" "ExtractMeshFacesByDraftAngle: extracted 4 of 6 faces"
  ux_has UXMESHSEL_ExtractEdge xedge510 "ExtractMeshFacesByEdgeLength 5 10 takes only the two faces whose every side is in range (not all six, whose longest side is)" "ExtractMeshFacesByEdgeLength: extracted 2 of 6 faces"
  ux_has UXMESHSEL_ExtractEdge xedge46 "ExtractMeshFacesByEdgeLength 4 6 takes the two 6 x 4 faces" "ExtractMeshFacesByEdgeLength: extracted 2 of 6 faces"
  ux_has UXMESHSEL_ExtractArea xarea "ExtractMeshFacesByArea 30 50 takes the two 10 x 4 faces" "ExtractMeshFacesByArea: extracted 2 of 6 faces"
  ux_has UXMESHSEL_ExtractAspect xaspect "ExtractMeshFacesByAspectRatio 2 and up takes the two faces at 2.5" "ExtractMeshFacesByAspectRatio: extracted 2 of 6 faces"
  got="$(ux_lines boxedges | sed -n 's/.*: \([0-9][0-9]*\) matching.*/\1/p' | tr '\n' ' ' | sed 's/ $//')"
  if [ "$got" = "12 12 0" ]; then ux_ok "UXMESHSEL_SelMeshEdges: all 12 box edges are 90 degree joins (45 and 90 select 12, 91 selects 0)"; else ux_bad "UXMESHSEL_SelMeshEdges: box edges by angle gave '$got', expected '12 12 0'"; fi
  got="$(ux_lines plane | { grep -E "matching (face|edge)" || true; } | sed -n 's/.*: \([0-9][0-9]*\) matching.*/\1/p' | tr '\n' ' ' | sed 's/ $//')"
  if [ "$got" = "128 128 0 128 0 128 176 0" ]; then ux_ok "UXMESHSEL_plane: the 128 flat triangles match area 0.78125, sides 1.25 / 1.768, aspect sqrt 2, draft 90; the 176 interior edges are flat joins"; else ux_bad "UXMESHSEL_plane: counts '$got', expected '128 128 0 128 0 128 176 0'"; ux_lines plane | head -12 | sed 's/^/     actual: /' || true; fi
fi
fi
