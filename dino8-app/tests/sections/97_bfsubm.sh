# ---------------------------------------------------------------------------
# BFSUBM_: Flow Register row geometry_nodes/Subdivide Mesh (Subdivide Mesh), flowrows_gen.py. The graph flowrows_bfsubm.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: splitting every quad into four (edge midpoints and a centre) and every triangle into four: V' = V + E (+ F for quads), E' = 2E + 4F (quads) or 2E + 3F (triangles), F' = 4F: cube 8/12/6 -> 26/48/24 -> 98/192/96; icosahedron 12/30/20 -> 42/120/80; 3 x 3 grid 9/12/4 -> 25/40/16; area and volume do not move (24 and 8 for the size-2 cube). The Blender triangle and n-gon splitting rules are from recollection (n-gons are flagged here).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfsubm.dflow|g" "$HERE/flowrows_bfsubm_script.txt" > "$TMPW/flowrows_bfsubm_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsubm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSUBM_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsubm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSUBM_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSUBM_$1: $3 (measured '$got')"; else echo "FAIL BFSUBM_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 78 node(s)" <<<"$FR_OUT"; then echo "ok   BFSUBM_solve: the player solved all 78 nodes (every node type is registered)"; else echo "FAIL BFSUBM_solve: expected 'solved 78 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 22 " <<<"$FR_OUT"; then echo "ok   BFSUBM_objects: exactly 22 point(s) baked"; else echo "FAIL BFSUBM_objects: expected 22 baked objects"; fail=1; fi
fr_chk 1 "26" "cube level 1: V' = V + E + F, E' = 2E + 4F, F' = 4F:: 26 points"
fr_chk 2 "48" "cube level 1: V' = V + E + F, E' = 2E + 4F, F' = 4F:: 48 edges"
fr_chk 3 "24" "cube level 1: V' = V + E + F, E' = 2E + 4F, F' = 4F:: 24 faces"
fr_chk 4 "96" "cube level 1: V' = V + E + F, E' = 2E + 4F, F' = 4F:: 96 corners"
fr_chk 5 "0" "surface unchanged: area 24"
fr_chk 6 "0" "volume 8"
fr_chk 7 "98" "level 2 repeats the recurrence (26/48/24 -> 98/192/96):: 98 points"
fr_chk 8 "192" "level 2 repeats the recurrence (26/48/24 -> 98/192/96):: 192 edges"
fr_chk 9 "96" "level 2 repeats the recurrence (26/48/24 -> 98/192/96):: 96 faces"
fr_chk 10 "42" "icosahedron level 1 (triangles: V' = V + E, E' = 2E + 3F, F' = 4F):: 42 points"
fr_chk 11 "120" "icosahedron level 1 (triangles: V' = V + E, E' = 2E + 3F, F' = 4F):: 120 edges"
fr_chk 12 "80" "icosahedron level 1 (triangles: V' = V + E, E' = 2E + 3F, F' = 4F):: 80 faces"
fr_chk 13 "240" "icosahedron level 1 (triangles: V' = V + E, E' = 2E + 3F, F' = 4F):: 240 corners"
fr_chk 14 "0" "volume of the icosahedron of circumradius 2 is unchanged"
fr_chk 15 "25" "3 x 3 grid:: 25 points"
fr_chk 16 "40" "3 x 3 grid:: 40 edges"
fr_chk 17 "16" "3 x 3 grid:: 16 faces"
fr_chk 18 "0" "area 1"
fr_chk 19 "8" "Level 0 changes nothing:: 8 points"
fr_chk 20 "12" "Level 0 changes nothing:: 12 edges"
fr_chk 21 "6" "Level 0 changes nothing:: 6 faces"
fr_chk 22 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
