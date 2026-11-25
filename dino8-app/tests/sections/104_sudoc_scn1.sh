# ---------------------------------------------------------------------------
# SUDOC_SCN1: scenes (named views): create 5 by script, the list order and names match, delete one and the remaining order holds (D8-SKETCHUP-SCENE-0001).
# Five perspective scenes S1..S5 are saved one after the other, the camera turned by RotateView 30 in between; the Camera report before each save gives the camera the scene must
# hold. NamedView List is run after the five saves (5 views, S1 S2 S3 S4 S5) and after NamedView Delete S3 (4 views, S1 S2 S4 S5). The document is saved to a .3dm and read back by
# view3dm_check (a plain openNURBS reader, tests/view3dm_check.cpp): the four views in the file are S1 S2 S4 S5 in that order, each with the camera location and target the app
# reported when it saved it. sudoc_view.py compares (the Camera report has 4 significant digits). Scope: add and delete and the order are tested; rename and reorder by
# drag have no command here.
# ---------------------------------------------------------------------------
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip SUDOC_SCN1 (not run on Windows: it uses the Linux headless launch)"
else
SUDOC_SCN1_BIN="$(dirname "$BIN")/view3dm_check"
if [ ! -x "$SUDOC_SCN1_BIN" ] || ! command -v python3 >/dev/null 2>&1; then
  echo "FAIL SUDOC_SCN1_tool: view3dm_check was not built next to $BIN or python3 is missing"; fail=1
else
  . "$HERE/ux_common.sh"
  SUDOC_SCN1_D="$TMPW/sudoc_scn1"; mkdir -p "$SUDOC_SCN1_D"
  {
    echo "@wait 2"; echo "SetActiveViewport Perspective"; echo "Point 0,0,0"
    for i in 1 2 3 4 5; do
      [ "$i" -gt 1 ] && echo "RotateView 30"
      echo "SUDOCBEGIN_c$i"; echo "Camera"; echo "@key Escape"; echo "NamedView Save S$i"
    done
    echo "SUDOCBEGIN_list1"; echo "NamedView List"
    echo "NamedView Delete S3"
    echo "SUDOCBEGIN_list2"; echo "NamedView List"
    echo "SaveAs $SUDOC_SCN1_D/scn1.3dm"
    echo "SUDOCBEGIN_end"
  } > "$TMPW/sudoc_scn1_script.txt"
  if ux_launch "$TMPW/sudoc_scn1_script.txt"; then
    printf '%s\n' "$UX_OUT" > "$SUDOC_SCN1_D/app.log"
    "$SUDOC_SCN1_BIN" dump "$SUDOC_SCN1_D/scn1.3dm" > "$SUDOC_SCN1_D/dump.txt" 2>&1 || true
    SUDOC_SCN1_R="$(python3 "$HERE/sudoc_view.py" scn1 "$SUDOC_SCN1_D/app.log" "$SUDOC_SCN1_D/dump.txt" 2>&1)" && SUDOC_SCN1_RC=0 || SUDOC_SCN1_RC=$?
    echo "$SUDOC_SCN1_R"
    if [ "$SUDOC_SCN1_RC" != "0" ] || grep -q '^FAIL' <<<"$SUDOC_SCN1_R"; then fail=1; fi
    if ! grep -q '^ok ' <<<"$SUDOC_SCN1_R"; then ux_bad "SUDOC_SCN1_check: the checker printed no result"; fi
  fi
fi
fi
