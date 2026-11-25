# ---------------------------------------------------------------------------
# GHTFLAT_: Flow Register row D8-GH-TREES-0009 (Flatten parameter modifier), flowrows6_gen.py. The graph flowrows_ghtflat.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: flatten puts every item in one branch {0}: 0..5 cut in pairs is 3 branches {0;0} {0;1} {0;2} of 2 items (paths 0 0, 0 1, 0 2); flattened it is 1 branch of 6 with path {0}; a grafted (depth 3) copy flattens to the same; a tree that already is the single branch {0} stays; the modifier is set the way a user sets it, on the input port (the GrasshopperPort command, which shares its code with the node editor's right-click port menu), by index or by port name, and a bad modifier, node or input is refused.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghtflat.dflow|g" "$HERE/flowrows_ghtflat_script.txt" > "$TMPW/flowrows_ghtflat_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtflat_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTFLAT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtflat_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTFLAT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHTFLAT_$1: $3 (measured '$got')"; else echo "FAIL GHTFLAT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHTFLAT_$2"; else echo "FAIL GHTFLAT_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHTFLAT_$3 (measured $n)"; else echo "FAIL GHTFLAT_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 45 node(s)" <<<"$FR_OUT"; then echo "ok   GHTFLAT_solve: the player solved all 45 nodes (every node type is registered)"; else echo "FAIL GHTFLAT_solve: expected 'solved 45 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 21 " <<<"$FR_OUT"; then echo "ok   GHTFLAT_objects: exactly 21 point(s) baked"; else echo "FAIL GHTFLAT_objects: expected 21 baked objects"; fail=1; fi
fr_chk 1 "3" "no modifier on 0..5 cut in pairs: 3 branch(es)"
fr_chk 2 "2 2 2" "no modifier on 0..5 cut in pairs: items per branch = '2 2 2'"
fr_chk 3 "0 0 0 1 0 2" "no modifier on 0..5 cut in pairs: path indices = '0 0 0 1 0 2'"
fr_chk 4 "1" "flatten: one branch {0} with all six items: 1 branch(es)"
fr_chk 5 "6" "flatten: one branch {0} with all six items: items per branch = '6'"
fr_chk 6 "0" "flatten: one branch {0} with all six items: path indices = '0'"
fr_chk 7 "1" "flatten of a depth 3 tree is the same single branch {0}: 1 branch(es)"
fr_chk 8 "6" "flatten of a depth 3 tree is the same single branch {0}: items per branch = '6'"
fr_chk 9 "0" "flatten of a depth 3 tree is the same single branch {0}: path indices = '0'"
fr_chk 10 "1" "flatten of a tree that is one branch {0} changes nothing: 1 branch(es)"
fr_chk 11 "4" "flatten of a tree that is one branch {0} changes nothing: items per branch = '4'"
fr_chk 12 "0" "flatten of a tree that is one branch {0} changes nothing: path indices = '0'"
fr_chk 13 "1" "flatten set through the port name Tree: one branch"
fr_chk 14 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPort: Tree Statistics (#11) input 0 Tree modifiers: F" "GrasshopperPort Flatten on Tree Statistics #11 input Tree: the modifiers are now 'F'"
fr_has "GrasshopperPort: Tree Statistics (#20) input 0 Tree modifiers: F" "GrasshopperPort Flatten on Tree Statistics #20 input Tree: the modifiers are now 'F'"
fr_has "GrasshopperPort: Tree Statistics (#29) input 0 Tree modifiers: F" "GrasshopperPort Flatten on Tree Statistics #29 input Tree: the modifiers are now 'F'"
fr_has "GrasshopperPort: Tree Statistics (#37) input 0 Tree modifiers: F" "GrasshopperPort Flatten on Tree Statistics #37 input Tree: the modifiers are now 'F'"
fr_has "GrasshopperPort: unknown modifier 'Sideways'" "an unknown modifier is refused with a message"
fr_has "GrasshopperPort: no node #9999" "a node id that does not exist is refused"
fr_has "GrasshopperPort: Tree Statistics has no input 7" "an input index that does not exist is refused"
fr_has "with 21 freshly-solved object(s)" "the re-solve baked exactly 21 object(s)"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
