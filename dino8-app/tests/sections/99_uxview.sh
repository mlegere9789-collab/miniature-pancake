# ---------------------------------------------------------------------------
# UXVIEW_: viewport activation commands proved through the command layer.
# Rows D8-RHINO-VIEW-0039 NextViewport, 0062 PrevViewport, 0037 NextOrthoViewport, 0038 NextPerspectiveViewport, 0079 SetActiveViewport.
# Oracles: the Rhino 8 help text kept in data/commands.json ("NextViewport activates the next viewport", "PrevViewport activates the previous
# viewport", "NextOrthoViewport activates the next viewport with an orthogonal projection", "NextPerspectiveViewport ... perspective
# projection", "SetActiveViewport activates a specified named viewport ... a viewport with the name you type must exist"), Rhino's standard
# four-view layout (Top, Perspective, Front, Right) and the arithmetic of a cycle: N steps of Next return to the start, Prev undoes Next,
# the ortho step never lands on the perspective viewport and the perspective step lands on it.
# Not tested (needs a real window): the highlighted viewport title and Ctrl+Tab.
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXVIEW (not run on Windows: needs python3 and the Linux headless launch)"
else
UXVIEW_S="$TMPW/uxview_script.txt"
cat > "$UXVIEW_S" <<EOS
@wait 2
UXBEGIN_layout4
4View
UXBEGIN_next
SetActiveViewport Top
NextViewport
NextViewport
NextViewport
NextViewport
UXBEGIN_prev
PrevViewport
PrevViewport
NextViewport
UXBEGIN_ortho
SetActiveViewport Front
NextOrthoViewport
NextOrthoViewport
NextOrthoViewport
UXBEGIN_persp
NextPerspectiveViewport
NextPerspectiveViewport
UXBEGIN_setactive
SetActiveViewport Front
SetActiveViewport right
SetActiveViewport Nowhere
UXBEGIN_layout3
3View
UXBEGIN_cycle3
NextViewport
NextViewport
NextViewport
UXBEGIN_end
EOS
if ux_launch "$UXVIEW_S"; then
  ux_eq UXVIEW_4View layout4 "4View is Rhino's standard four-view layout with the perspective viewport active" "4View: Top, Perspective (active), Front, Right"
  ux_eq UXVIEW_NextViewport next "from Top four steps visit Perspective, Front, Right and wrap back to Top" \
    "Active viewport: Top" "Active viewport: Perspective" "Active viewport: Front" "Active viewport: Right" "Active viewport: Top"
  ux_eq UXVIEW_PrevViewport prev "from Top Prev wraps to Right then Front, and Next undoes it (back to Right)" \
    "Active viewport: Right" "Active viewport: Front" "Active viewport: Right"
  ux_eq UXVIEW_NextOrthoViewport ortho "from Front the ortho step goes Right, Top, Front and never stops on Perspective" \
    "Active viewport: Front" "Active viewport: Right" "Active viewport: Top" "Active viewport: Front"
  ux_eq UXVIEW_NextPerspectiveViewport persp "the perspective step lands on the one perspective viewport, also when repeated" \
    "Active viewport: Perspective" "Active viewport: Perspective"
  ux_eq UXVIEW_SetActiveViewport setactive "a viewport is activated by name (any letter case); an unknown name is refused and lists the viewports" \
    "Active viewport: Front" "Active viewport: Right" "! No viewport 'Nowhere'. Viewports: Top, Perspective, Front, Right (active)"
  ux_eq UXVIEW_3View layout3 "3View keeps the same standard views in three viewports" "3View: Top, Front, Perspective (active)"
  ux_eq UXVIEW_3View cycle3 "three steps of NextViewport go round the three viewports and come back" \
    "Active viewport: Top" "Active viewport: Front" "Active viewport: Perspective"
fi
fi
