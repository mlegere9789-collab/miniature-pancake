# ---------------------------------------------------------------------------
# BFMERG_: Flow Register row geometry_nodes/Merge by Distance (Merge by Distance), flowrows_gen.py. The graph flowrows_bfmerg.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: two 3 x 3 grids side by side: 18 vertices, 24 edges; welded: 15 vertices (the shared column of 3 merges), 22 edges (2 shared edges), 8 faces, area 2; two identical unit cubes weld to 8 / 12 / 6 with volume 1 (duplicate faces are removed); distance 0.1 welds only coincident vertices; above the grid spacing everything chains together; a line of spacing 1 stays whole at distance 0.5 and collapses to one vertex at 1.5. The position of a welded group (the mean here) is untested.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfmerg.dflow|g" "$HERE/flowrows_bfmerg_script.txt" > "$TMPW/flowrows_bfmerg_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmerg_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMERG_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmerg_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMERG_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFMERG_$1: $3 (measured '$got')"; else echo "FAIL BFMERG_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 75 node(s)" <<<"$FR_OUT"; then echo "ok   BFMERG_solve: the player solved all 75 nodes (every node type is registered)"; else echo "FAIL BFMERG_solve: expected 'solved 75 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 19 " <<<"$FR_OUT"; then echo "ok   BFMERG_objects: exactly 19 point(s) baked"; else echo "FAIL BFMERG_objects: expected 19 baked objects"; fail=1; fi
fr_chk 1 "18" "two grids side by side share a column but are not welded:: 18 points"
fr_chk 2 "24" "two grids side by side share a column but are not welded:: 24 edges"
fr_chk 3 "8" "two grids side by side share a column but are not welded:: 8 faces"
fr_chk 4 "15" "welded: the 3 shared vertices and 2 shared edges merge:: 15 points"
fr_chk 5 "22" "welded: the 3 shared vertices and 2 shared edges merge:: 22 edges"
fr_chk 6 "8" "welded: the 3 shared vertices and 2 shared edges merge:: 8 faces"
fr_chk 7 "32" "welded: the 3 shared vertices and 2 shared edges merge:: 32 corners"
fr_chk 8 "0" "area 2"
fr_chk 9 "8" "two identical cubes weld into one (duplicate faces go):: 8 points"
fr_chk 10 "12" "two identical cubes weld into one (duplicate faces go):: 12 edges"
fr_chk 11 "6" "two identical cubes weld into one (duplicate faces go):: 6 faces"
fr_chk 12 "0" "volume 1"
fr_chk 13 "15" "a distance of 0.1 is below the grid spacing 0.5 and welds only the shared column:: 15 points"
fr_chk 14 "1" "above the spacing 0.5 every vertex of a grid chains into one:: 1 points"
fr_chk 15 "5" "spacing 1 is above the distance 0.5: nothing merges:: 5 points"
fr_chk 16 "4" "spacing 1 is above the distance 0.5: nothing merges:: 4 edges"
fr_chk 17 "1" "distance 1.5 chains the five vertices of a line into one:: 1 points"
fr_chk 18 "0" "distance 1.5 chains the five vertices of a line into one:: 0 edges"
fr_chk 19 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
