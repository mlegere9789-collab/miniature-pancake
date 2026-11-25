# ---------------------------------------------------------------------------
# R2RANGETREE_: the data-tree graph of smoke.sh's "Dino Flow data trees" block (flow_tree_graph.dflow: Range(0,10,5) -> Graft Tree ->
# Flatten Tree -> List Item(2) -> Construct Point -> Bake) run on its own, with the counts Grasshopper's Range gives: Steps + 1
# numbers (the Grasshopper Primer: 10 steps return 11 values), so 5 steps give 0 2 4 6 8 10 - six items, six grafted branches.
# smoke.sh's own block carries the same counts; this section lets the claim be checked without running the whole BASE section.
# ---------------------------------------------------------------------------
sed "s|@TREEFILE@|$HEREW/flow_tree_graph.dflow|g" "$HERE/flow_tree_script.txt" > "$TMPW/flow_tree_r2_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RT_OUT="$(timeout 900 "$BIN" --smoke 100 --script "$TMPW/flow_tree_r2_script.txt" 2>&1)" || { echo "$RT_OUT" | tail -20; echo "FAIL R2RANGETREE_run: script exited non-zero"; fail=1; RT_OUT=""; }
else
  RT_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/flow_tree_r2_script.txt" 2>&1)" || { echo "$RT_OUT" | tail -20; echo "FAIL R2RANGETREE_run: script exited non-zero"; fail=1; RT_OUT=""; }
fi
rt_chk() { if grep -qE "$1" <<<"$RT_OUT"; then echo "ok   R2RANGETREE_$2"; else echo "FAIL R2RANGETREE_$2 (no line matching '$1')"; fail=1; fi; }
rt_chk "^ok +expect_objects 1 " "objects: the tree graph baked exactly one point"
rt_chk "Range=6 items" "range: Range(0,10,5) produced six items (0 2 4 6 8 10)"
rt_chk "Graft Tree=6 items in 6 branches" "graft: six items, one branch each"
rt_chk "Flatten Tree=6 items;" "flatten: the six branches collapse back into one list"
rt_chk "List Item=1 item" "item: List Item picks one value"
rt_chk "  4,0,0" "value: item 2 of 0 2 4 6 8 10 is 4"
