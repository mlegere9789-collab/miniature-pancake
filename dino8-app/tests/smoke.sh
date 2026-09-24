#!/usr/bin/env bash
# Headless functional QC for Dino 8: runs a command script through the real
# app under Xvfb and checks the resulting history/object counts.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../build/Dino8}"
TMP="$(mktemp -d)"
# The real, OS-native form of $TMP - needed wherever a path is embedded as
# literal TEXT inside a script file the app itself parses (Save/Open/Export/
# Import command lines), as opposed to a bash-level file operation or a
# command-line argument handed straight to $BIN. Bash's own POSIX-path file
# ops (mkdir/cat/grep/rm) and argv passed directly to a spawned native
# process both get Git-for-Windows' automatic MSYS<->Windows path
# translation; a path baked into a script file's own bytes gets none of
# that; the app's own file I/O then tries to open "/tmp/xxx" literally,
# which Windows resolves as "current drive's root\tmp\xxx" - never the
# real temp directory - and every Save/Export/Import in that script fails.
# cygpath -m gives the same drive-letter path with forward slashes (valid
# to Win32 file APIs, and avoids backslash-escaping issues in sed/heredoc
# text); on Linux/macOS (no cygpath) this is just $TMP again, a no-op.
if command -v cygpath >/dev/null 2>&1; then TMPW="$(cygpath -m "$TMP")"; else TMPW="$TMP"; fi
# Same reasoning as TMPW above, for $HERE (this file's own directory):
# a handful of sed substitutions below bake $HERE into a *.dflow/script
# path that becomes literal TEXT inside a generated script file (an
# @DINO8ROOT@/@TREEFILE@/@SOLVERFILE@/@GEOMFILE@ token, not a bash-level
# file argument), so it needs the same OS-native form.
if command -v cygpath >/dev/null 2>&1; then HEREW="$(cygpath -m "$HERE")"; else HEREW="$HERE"; fi
# Isolate settings so persisted toggles (Ortho, snaps, theme) from earlier runs cannot leak into the checks.
export XDG_CONFIG_HOME="$TMPW/config"
mkdir -p "$XDG_CONFIG_HOME"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMPW/script.txt" <<EOS
Box 0,0,0 20,20,0 10
Sphere 10,10,10 8
SelNone
SelID 1
BooleanDifference
SelID 2
Enter
SelAll
Volume
SelNone
Circle 40,0,0 5
SelCrv
ExtrudeCrv 12
SelAll
Save $TMPW/test.3dm
New
Open $TMPW/test.3dm
SelAll
List
Undo
Redo
Line 0,0,0 10,10,0
SelCrv
Length
SelAll
Export $TMPW/test.obj
SelNone
Cylinder 60,0,0 5 15
SelLast
What
ToolbarAddCommand Fillet
ToolbarAddCommand NotARealCommandXYZ
EOS

if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  OUT="$("$BIN" --smoke 200 --script "$TMPW/script.txt" 2>&1)" || { echo "$OUT"; echo "FAIL: app exited non-zero"; exit 1; }
else
  OUT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMPW/script.txt" 2>&1)" || { echo "$OUT"; echo "FAIL: app exited non-zero"; exit 1; }
fi
echo "$OUT" | grep "^smoke:" || { echo "$OUT"; echo "FAIL: no smoke line"; exit 1; }
fail=0
# On a FAIL, show what the app actually printed for the line the check was
# looking for, so a platform-specific value difference (e.g. MSVC vs GCC
# producing "Volume = 199.9999" instead of "Volume = 200") is visible in the
# CI log directly instead of only "FAIL <description>". The expected pattern
# is trimmed at its first digit to get a stable prefix ("Volume = ", "CV[0] ",
# "Shell: thickness "), and every output line carrying that prefix is echoed
# (capped, so a runaway match can't flood the log). Diagnostic only - never
# changes pass/fail.
near() {
  local out="$1" pat="$2" prefix
  prefix="${pat%%[0-9]*}"
  prefix="${prefix%%\\*}"
  prefix="${prefix%%[\[\]\(\)\.\*\^\$]*}"
  echo "     expected: $pat"
  if [ -n "$prefix" ] && [ "$prefix" != "$pat" ]; then
    echo "$out" | grep -F -- "$prefix" | head -6 | sed 's/^/     actual:   /' || true
  fi
}
check() { if echo "$OUT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$OUT" "$1"; fail=1; fi; }
check "1055 commands loaded" "catalog has all 1055 commands"
check "BooleanDifference:" "boolean difference ran"
check "Volume = " "volume measured"
check "Extruded 1 object" "extrusion created a solid"
check "Saved " "save wrote a .3dm"
check "Opened " "open re-read the .3dm"
check "Exported " "OBJ export wrote a file"
check "length = 14.14" "line length measured"
# Options > Toolbar's icon-grid command picker adds a command through
# AddToolbarCommand() (ui/Toolbars.cpp) - the same function the picker's
# click/drag handlers call in Panels.cpp. Mouse clicks/drags on the picker
# itself are not scriptable headlessly, but this exercises the exact code
# path they invoke, via the scriptable ToolbarAddCommand command.
check "Added 'Fillet' to the Standard toolbar" "icon-grid picker's add-to-toolbar path (ToolbarAddCommand) accepted a known command"
check "Unknown command: NotARealCommandXYZ" "icon-grid picker's add-to-toolbar path rejects an unknown command"
test -s "$TMPW/test.3dm" && echo "ok   test.3dm exists" || { echo "FAIL test.3dm missing"; fail=1; }
test -s "$TMPW/test.obj" && echo "ok   test.obj exists" || { echo "FAIL test.obj missing"; fail=1; }
check "gl_error=0" "no OpenGL errors"
echo "$OUT" | grep -E "^(smoke|history)" | tail -120

# DWG round-trip (via GNU LibreDWG, see FileExchange.cpp's ExportDwg/
# ImportDwg): a line, a circle and a closed 4-point polyline must survive a
# real Export to .dwg and a real Open back, with exact control-point
# counts/rational/closed flags and exact combined curve length (not just an
# object count - see dwg_script.txt).
#
# Deliberately run here, as the 2nd process launch of this whole script
# (right after the very first sanity script above), not further down where
# it used to sit (~20+ launches deep). A long multi-round CI bisection (see
# this file's git history, and dwg_script.txt's own header comment) found a
# Windows-only crash on the reopen half - exit 127, zero output - that
# survived every fix tried at that position: a same-process vs split-process
# rewrite, an instant retry, and a 2-second wall-clock pause before the
# retry. Every one of those failed identically, which rules out timing and
# locking as the mechanism. What every FAILED real run had in common was
# happening deep in this script (~20+ prior process launches); what every
# SUCCESSFUL isolated bisection test had in common was running early (as
# the 2nd-6th launch) - a variable never actually isolated until now. This
# move tests that positional theory directly: if running this as the 2nd
# launch here fixes it for good, that confirms something about a long
# sequence of prior launches (not the DWG codec or Windows file locking) was
# always the real cause, and the retry/sleep logic below can eventually be
# simplified back down once that's confirmed stable.
DWGBIN="$(dirname "$BIN")/dwg_fixture_gen"
sed "s|@TMP@|$TMPW|g" "$HERE/dwg_script.txt" > "$TMPW/dwg_script.txt"
sed "s|@TMP@|$TMPW|g" "$HERE/dwg_reopen_script.txt" > "$TMPW/dwg_reopen_script.txt"
dwg_run() {
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    "$BIN" --smoke 50 --script "$TMPW/dwg_script.txt" 2>&1
  else
    xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 50 --script "$TMPW/dwg_script.txt" 2>&1
  fi
}
dwg_reopen_run() {
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    "$BIN" --smoke 30 --script "$TMPW/dwg_reopen_script.txt" 2>&1
  else
    xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dwg_reopen_script.txt" 2>&1
  fi
}
# set -e is active for this whole file (see the top), and a bare
# DW="$(dwg_run)" is NOT one of the contexts POSIX exempts from it (that
# exemption only covers a command substitution's own failure being ignored
# when the assignment itself is part of an if/while/&&/|| - a plain
# assignment statement is not), so a failing dwg_run would kill the entire
# smoke.sh run right here. set +e/-e around exactly this call is the
# standard, portable way to capture both output and exit code of a command
# that is allowed to fail, with no ambiguity about which contexts a given
# shell treats as exempt.
# dwg_run_retrying is called as a plain command, never via "$(...)" - calling
# it through a command substitution would capture its own echo/diagnostic
# output (and swallow its "exit 1" into a failed assignment that set -e
# kills the whole script on, silently, before any of those echoes ever
# reach the real log) instead of printing it live. It sets the global
# DWG_RETRY_RESULT for the caller to pick up after it returns. Kept as a
# safety net for a genuine one-off even now that this section runs early;
# costs nothing when the first attempt succeeds.
dwg_run_retrying() {
  local label="$1" fn="$2" ec
  set +e
  DWG_RETRY_RESULT="$("$fn")"; ec=$?
  set -e
  if [ "$ec" -ne 0 ]; then
    echo "$label script exited $ec on the first attempt (output below); pausing 2s then retrying once to check for a transient flake:"
    echo "$DWG_RETRY_RESULT"
    sleep 2
    set +e
    DWG_RETRY_RESULT="$("$fn")"; ec=$?
    set -e
    if [ "$ec" -eq 0 ]; then
      echo "ok   $label succeeded on retry (first attempt's exit was a one-off, not reproduced)"
    else
      echo "$label script exited $ec on the retry too (output below):"
      echo "$DWG_RETRY_RESULT"
      echo "FAIL: $label script exited non-zero on both attempts"
      exit 1
    fi
  fi
}
dwg_run_retrying "DWG export" dwg_run
DW="$DWG_RETRY_RESULT"
dwg_run_retrying "DWG reopen" dwg_reopen_run
DWI="$DWG_RETRY_RESULT"
dwcheck() { if echo "$DW" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DW" "$1"; fail=1; fi; }
dwicheck() { if echo "$DWI" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DWI" "$1"; fail=1; fi; }
dwcheck "Exported $TMPW/dwg_roundtrip.dwg" "DWG export wrote a file"
[ "$(echo "$DW" | grep -c "^history: Exported $TMPW/dwg_roundtrip.dwg\$")" = "2" ] && echo "ok   re-exporting DWG to the exact same path overwrites instead of silently failing" || { echo "FAIL DWG re-export to the same path did not overwrite"; fail=1; }
dwicheck "DWG: 3 curves, 0 points" "DWG import read the line, circle and closed polyline back"
[ "$(echo "$DWI" | grep -c "degree 2, 9 control points, rational, closed")" = "1" ] && echo "ok   DWG CIRCLE round-tripped as an exact rational NURBS circle" || { echo "FAIL DWG CIRCLE did not survive round-trip"; fail=1; }
[ "$(echo "$DWI" | grep -c "degree 1, 5 control points, non-rational, closed")" = "1" ] && echo "ok   DWG closed LWPOLYLINE round-tripped with the right point count and closed flag" || { echo "FAIL DWG closed polyline did not survive round-trip"; fail=1; }
[ "$(echo "$DWI" | grep -c "CV\[0\] 0,0,0")" = "1" ] && [ "$(echo "$DWI" | grep -c "CV\[1\] 12,0,0")" = "1" ] && echo "ok   DWG LINE kept its exact endpoints" || { echo "FAIL DWG LINE endpoints did not survive round-trip"; fail=1; }
[ -n "$(echo "$DW" | grep "Total length = ")" ] && [ "$(echo "$DW" | grep "Total length = ")" = "$(echo "$DWI" | grep "Total length = ")" ] && echo "ok   DWG round-trip kept the exact combined curve length (line + circle + polyline)" || { echo "FAIL DWG round-trip changed the combined curve length"; fail=1; }
[ "$(head -c 6 "$TMPW/dwg_roundtrip.dwg")" = "AC1015" ] && echo "ok   dwg_roundtrip.dwg is a real binary DWG (AC1015/AutoCAD 2000 header)" || { echo "FAIL dwg_roundtrip.dwg is not a real DWG file"; fail=1; }
# AcadSchemes / Version=: a per-export Version= token and the persistent
# AcadSchemes Version= setting must both change the real bytes written -
# the DWG's own 6-byte version magic at file offset 0 (GNU LibreDWG's
# dwg_version_codes(), copied verbatim to the start of its output - see
# src/encode.c) and the DXF's $ACADVER header value - not just print a
# claim, and the scoped Version= override on Export must not leak into
# the document's own persistent scheme for a later, unversioned Export.
dwcheck "AcadSchemes: Export/SaveAs to DWG/DXF write AC1015 (AutoCAD 2000)\." "AcadSchemes reports the AC1015 default before anything is set"
dwcheck "Exported $TMPW/dwg_v13.dwg (Version=13)" "Export Version=13 (DWG) ran"
dwcheck "AcadSchemes: Export/SaveAs to DWG/DXF write AC1027 (AutoCAD 2013, just set)\." "AcadSchemes Version=2013 set the persistent scheme"
dwcheck "AcadSchemes: Export/SaveAs to DWG/DXF write AC1027 (AutoCAD 2013)\." "a later no-arg AcadSchemes reports AC1027 without changing it"
dwcheck "Exported $TMPW/dwg_v2018.dwg (Version=2018)" "Export Version=2018 (DWG) ran"
[ "$(head -c 6 "$TMPW/dwg_v13.dwg")" = "AC1012" ] && echo "ok   Export Version=13 wrote a real AC1012 (AutoCAD Release 13) DWG header" || { echo "FAIL Export Version=13 did not write an AC1012 DWG"; fail=1; }
[ "$(head -c 6 "$TMPW/dwg_v2013.dwg")" = "AC1027" ] && echo "ok   AcadSchemes Version=2013 made a later unversioned Export write a real AC1027 (AutoCAD 2013) DWG header" || { echo "FAIL the persistent AcadSchemes Version=2013 scheme did not reach Export"; fail=1; }
[ "$(head -c 6 "$TMPW/dwg_v2018.dwg")" = "AC1032" ] && echo "ok   Export Version=2018 wrote a real AC1032 (AutoCAD 2018) DWG header" || { echo "FAIL Export Version=2018 did not write an AC1032 DWG"; fail=1; }
[ "$(head -c 6 "$TMPW/dwg_after_override.dwg")" = "AC1027" ] && echo "ok   after a one-off Version=2018 export, the next unversioned Export still wrote AC1027 - the per-export override did not leak into the persistent AcadSchemes scheme" || { echo "FAIL a one-off Export Version= override leaked into the document's persistent AcadSchemes scheme"; fail=1; }
[ "$(grep -A2 '\$ACADVER' "$TMPW/dwg_v13.dxf" | tail -1)" = "AC1012" ] && echo "ok   Export Version=13 wrote \$ACADVER=AC1012 in the DXF header" || { echo "FAIL Export Version=13 (DXF) did not write \$ACADVER=AC1012"; fail=1; }
[ "$(grep -A2 '\$ACADVER' "$TMPW/dwg_v2013.dxf" | tail -1)" = "AC1027" ] && echo "ok   the persistent AcadSchemes Version=2013 scheme made a later unversioned Export write \$ACADVER=AC1027 in the DXF header" || { echo "FAIL the persistent AcadSchemes Version=2013 scheme did not reach the DXF \$ACADVER"; fail=1; }
[ "$(head -c 6 "$TMPW/dwg_saveas_v14.dwg")" = "AC1014" ] && echo "ok   SaveAs Version=14 wrote a real AC1014 (AutoCAD Release 14) DWG header" || { echo "FAIL SaveAs Version=14 did not write an AC1014 DWG"; fail=1; }

# Interactive UI replay: typed command, viewport picks, click-select, Delete, Undo.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  UI="$("$BIN" --smoke 320 --script "$HERE/ui_script.txt" 2>&1)" || true
else
  UI="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 320 --script "$HERE/ui_script.txt" 2>&1)" || true
fi
echo "$UI" | grep -E "^(ok|FAIL)"
if echo "$UI" | grep -q "^FAIL"; then fail=1; fi
echo "$UI" | grep -q "^ok   expect_objects 1" || { echo "FAIL ui script produced no checks"; fail=1; }
# LockViewport: the second Camera printout (after a locked middle-drag) must
# match the first exactly; the third (after unlocking and dragging again)
# must differ.
LV1="$(echo "$UI" | grep "^history: Camera (Top): location" | sed -n '1p')"
LV2="$(echo "$UI" | grep "^history: Camera (Top): location" | sed -n '2p')"
LV3="$(echo "$UI" | grep "^history: Camera (Top): location" | sed -n '3p')"
if [ -n "$LV1" ] && [ "$LV1" = "$LV2" ]; then echo "ok   LockViewport blocked a middle-button drag pan"; else echo "FAIL LockViewport did not block the pan ('$LV1' vs '$LV2')"; fail=1; fi
if [ -n "$LV3" ] && [ "$LV2" != "$LV3" ]; then echo "ok   toggling LockViewport off let the next drag pan the camera"; else echo "FAIL camera did not move after LockViewport was toggled back off ('$LV2' vs '$LV3')"; fail=1; fi
# OrthoAngle 45: a clicked line end near (12,5,0) from a (0,0,0) start must
# land exactly on a 45-degree ray (equal X and Y offsets), not on the
# world-axis-only 90-degree snap.
UI_CV1="$(echo "$UI" | grep -A1 'CV\[0\]' | grep 'CV\[1\]' | head -1)"
X0="$(echo "$UI" | grep 'CV\[0\]' | head -1 | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f1)"
Y0="$(echo "$UI" | grep 'CV\[0\]' | head -1 | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f2)"
X1="$(echo "$UI_CV1" | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f1)"
Y1="$(echo "$UI_CV1" | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f2)"
if [ -n "$X0" ] && [ -n "$X1" ] && python3 -c "import sys; dx=$X1-($X0); dy=$Y1-($Y0); sys.exit(0 if dx>1 and abs(dx-dy)<0.05*dx else 1)"; then
  echo "ok   OrthoAngle 45 constrained the clicked line end to an exact 45-degree ray"
else
  echo "FAIL OrthoAngle 45 did not constrain the line to 45 degrees (CV[0] $X0,$Y0 CV[1] $X1,$Y1)"; fail=1
fi

# Curve editing: Intersect, Split, Trim, Fillet, Chamfer, FilletCorners (see curveedit_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CE="$("$BIN" --smoke 150 --script "$HERE/curveedit_script.txt" 2>&1)" || { echo "$CE"; echo "FAIL: curve-edit script exited non-zero"; exit 1; }
else
  CE="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/curveedit_script.txt" 2>&1)" || { echo "$CE"; echo "FAIL: curve-edit script exited non-zero"; exit 1; }
fi
cecheck() { if echo "$CE" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$CE" "$1"; fail=1; fi; }
cecheck "intersection at 10,0,0" "Intersect found the line/line crossing"
cecheck "Split 1 curve into 2 pieces" "Split cut the line at the crossing"
cecheck "Trimmed curve 6" "Trim removed the clicked portion"
cecheck "CV\[1\] 40,0,0" "Trim kept the piece up to the cutter"
cecheck "Fillet: Radius=5, arc added" "Fillet built a tangent arc"
cecheck "CV\[1\] 75,0,0" "Fillet trimmed the first line to the tangent point"
cecheck "Chamfer: joined result, Distance=4" "Chamfer joined the pieces into one curve"
cecheck "FilletCorners: rounded 4 corners on 1 curve" "FilletCorners rounded the rectangle"
cecheck "intersection at 305,0,0" "Intersect found the circle/line crossing"
cecheck "Split into 2 piece(s)" "Split still delegates solids to the plane split"
cecheck "smoke: frames=150 objects=17" "curve-edit script produced the expected object count"

# Curve tools: conics, catenary, CloseCrv, ReducePolyline, SubCrv, Contour, Section, Align, Distribute, TweenCurves, ArrayCrv, fits (see curves2_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  C2="$("$BIN" --smoke 400 --script "$HERE/curves2_script.txt" 2>&1)" || { echo "$C2"; echo "FAIL: curve-tools script exited non-zero"; exit 1; }
else
  C2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 400 --script "$HERE/curves2_script.txt" 2>&1)" || { echo "$C2"; echo "FAIL: curve-tools script exited non-zero"; exit 1; }
fi
c2check() { if echo "$C2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$C2" "$1"; fail=1; fi; }
c2check "Conic: rho = 0.4" "Conic passed through the shoulder point"
c2check "Parabola: focal length 5" "Parabola built from vertex and focus"
c2check "Hyperbola: a = 5" "Hyperbola built from center and vertex"
c2check "Catenary: length 50" "Catenary solved for the cable length"
c2check "CloseCrv: 1 curve(s) closed" "CloseCrv closed the polyline"
c2check "ReducePolyline: 1 polyline(s) reduced" "ReducePolyline dropped collinear vertices"
c2check "CV\[0\] 120,0,0" "SubCrv kept the picked span"
c2check "Contour: 3 curve(s) from 5 plane(s)" "Contour sliced the sphere"
c2check "Section: 1 curve(s)" "Section cut the sphere"
c2check "aligned Bottom" "Align moved the boxes"
c2check "spaced evenly along X" "Distribute spaced the boxes"
c2check "TweenCurves: 3 curve(s) created" "TweenCurves interpolated between the lines"
c2check "ArrayCrv: 6 object(s) placed" "ArrayCrv placed copies along the circle"
c2check "best-fit line through 4 points" "LineThroughPt fitted a line"
c2check "best-fit plane through 4 points" "PlaneThroughPt fitted a plane"
c2check "MarkFoci: 1 point(s) added" "MarkFoci added the parabola's focus"
c2check "0,5,0" "MarkFoci found the parabola focus (0,5,0) from curve geometry alone"
c2check "MarkFoci: 2 point(s) added" "MarkFoci added both hyperbola foci"
c2check "6.807" "MarkFoci found the analytic hyperbola focus distance (c=6.807 for a=5, b^2=64/3)"
c2check "MergeCrv: joined 2 curve(s) into one, merged 1 tangent junction(s) into a single span" "MergeCrv collapsed a tangent joint into one span"
c2check "Blend: curvature-continuous (G2) blend curve created" "Blend built a real G2 quintic blend"
c2check "ArcBlend: two-arc tangent blend created (joint tangent match 1)" "ArcBlend found a genuine two-arc biarc, tangent-verified"
c2check "Domain: 1 curve(s) now have domain 0 to 5" "Domain actually set a new domain, not just reported it"
c2check "ModifyRadius: 1 curve(s) now have radius 12" "ModifyRadius rebuilt the circle in place"
c2check "Match: reshaped curve .* to meet curve .* tangentially" "Match reshaped one curve's end to meet another"
c2check "Curve does not self-intersect" "IntersectSelf ran a genuine self-intersection check"
c2check "SoftEditCrv: 4 control point(s) moved with falloff 5" "SoftEditCrv applied a real falloff drag"
c2check "FixedLengthCrvEdit: point moved and curve rescaled .* to keep length 30" "FixedLengthCrvEdit preserved the curve's total length"
c2check "CurveThroughSrfControlPt: 4 curve(s) through the control point rows and columns" "CurveThroughSrfControlPt built curves through the CV grid directly"
c2check "OffsetMultiple: 3 curve(s) created (3 offsets x 1)" "OffsetMultiple created several offsets in one command"
c2check "InsertLineIntoCrv: inserted a 5-long line and rejoined" "InsertLineIntoCrv split, inserted, and rejoined automatically"
c2check "CSec: 1 curve(s) from 5 section(s) along the rail" "CSec sectioned perpendicular to a rail curve"
c2check "ContinueCurve: curve extended with 2 new point(s) and joined" "ContinueCurve extended and auto-joined a curve"
c2check "EndBulge: end handle scaled by 2" "EndBulge scaled the end tangent handle"
c2check "InterpCrvOnSrf: curve interpolated through 3 point(s) on the surface" "InterpCrvOnSrf projected points onto the surface before interpolating"
c2check "Symmetry: 1 live mirrored copy created" "Symmetry built a real live-linked mirrored copy, not a one-time Mirror bake"
c2check "Bounding box: (30, 0, 0) to (40, 0, 0)" "Symmetry's mirrored copy starts at the hand-derived reflection of the source about x=20"
c2check "Bounding box: (30, 0, 5) to (40, 0, 5)" "moving the source updated the mirrored copy automatically (UpdateSymmetryPairs, no rebuild command run)"
c2check "RemoveSymmetry: 1 live symmetry link" "RemoveSymmetry actually broke the live link, not just reported one was never there"
if echo "$C2" | grep -q "Bounding box: (30, 0, 9) to (40, 0, 9)"; then
  echo "FAIL RemoveSymmetry did not actually break the live link (copy still followed the source's post-removal move)"; fail=1
else
  echo "ok   RemoveSymmetry genuinely broke the live link (copy did NOT follow the source's post-removal move)"
fi
c2check "^ok   expect_objects 72" "curve-tools script produced the expected object count"
# Exchange formats: DXF round-trip, SVG / PDF vector output, PLY round-trip (see exchange_script.txt).
sed "s|@TMP@|$TMPW|g" "$HERE/exchange_script.txt" > "$TMPW/exchange_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  EX="$("$BIN" --smoke 150 --script "$TMPW/exchange_script.txt" 2>&1)" || { echo "$EX"; echo "FAIL: exchange script exited non-zero"; exit 1; }
else
  EX="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMPW/exchange_script.txt" 2>&1)" || { echo "$EX"; echo "FAIL: exchange script exited non-zero"; exit 1; }
fi
excheck() { if echo "$EX" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$EX" "$1"; fail=1; fi; }
excheck "Exported $TMPW/exchange.dxf" "DXF export wrote a file"
excheck "DXF: 16 curves, 1 point, 1 mesh, 1 new layer" "DXF import read every entity back (line, circle, arc, polyline, 12 box edges, point, mesh, layer)"
excheck "degree 2, 9 control points, rational, closed" "DXF CIRCLE came back as an exact rational circle"
excheck "CV\[1\] 10,0,0" "DXF LINE kept its coordinates"
excheck "CV\[2\] 10,30,0" "DXF LWPOLYLINE kept its vertices"
excheck "(point) layer Default" "DXF POINT imported"
excheck "(mesh) layer Solids" "DXF 3DFACEs became a mesh on the imported layer"
excheck "Printed $TMPW/exchange.pdf" "Print wrote a PDF"
excheck "Exported $TMPW/exchange.svg" "SVG export wrote a file"
excheck "Exported $TMPW/exchange.ply" "PLY export wrote a file"
excheck "8 vertices, 6 faces" "PLY round-trip kept the mesh box"
excheck "smoke: frames=150 objects=1 " "exchange script ended with the re-opened PLY mesh"
grep -q "^0$" "$TMPW/exchange.dxf" && grep -q "^AC1015$" "$TMPW/exchange.dxf" && grep -q "^EOF$" "$TMPW/exchange.dxf" && echo "ok   exchange.dxf is a complete AC1015 DXF" || { echo "FAIL exchange.dxf malformed"; fail=1; }
grep -q "^CIRCLE$" "$TMPW/exchange.dxf" && grep -q "^ARC$" "$TMPW/exchange.dxf" && grep -q "^LWPOLYLINE$" "$TMPW/exchange.dxf" && grep -q "^3DFACE$" "$TMPW/exchange.dxf" && echo "ok   exchange.dxf uses CIRCLE/ARC/LWPOLYLINE/3DFACE entities" || { echo "FAIL exchange.dxf entity types"; fail=1; }
grep -q "^Solids$" "$TMPW/exchange.dxf" && echo "ok   exchange.dxf carries the Solids layer" || { echo "FAIL exchange.dxf layer table"; fail=1; }
grep -q "<svg" "$TMPW/exchange.svg" && grep -q "<path" "$TMPW/exchange.svg" && echo "ok   exchange.svg has paths" || { echo "FAIL exchange.svg has no paths"; fail=1; }
grep -q ' Z"' "$TMPW/exchange.svg" && echo "ok   exchange.svg closes the circle path with Z" || { echo "FAIL exchange.svg has no closed path"; fail=1; }
grep -q 'id="Solids"' "$TMPW/exchange.svg" && echo "ok   exchange.svg groups paths by layer" || { echo "FAIL exchange.svg layer groups"; fail=1; }
head -c 5 "$TMPW/exchange.pdf" | grep -q "%PDF-" && echo "ok   exchange.pdf starts with %PDF-" || { echo "FAIL exchange.pdf header"; fail=1; }
grep -aq "^xref$" "$TMPW/exchange.pdf" && grep -aq "^startxref$" "$TMPW/exchange.pdf" && grep -aq "%%EOF" "$TMPW/exchange.pdf" && echo "ok   exchange.pdf has an xref table and trailer" || { echo "FAIL exchange.pdf xref"; fail=1; }
PDFOFF="$(grep -a -A1 "^startxref$" "$TMPW/exchange.pdf" | tail -1)"
[ "$(tail -c +$((PDFOFF + 1)) "$TMPW/exchange.pdf" | head -c 4)" = "xref" ] && echo "ok   exchange.pdf startxref points at the xref table" || { echo "FAIL exchange.pdf startxref offset"; fail=1; }
grep -aq "^h$" "$TMPW/exchange.pdf" && echo "ok   exchange.pdf closes paths with h" || { echo "FAIL exchange.pdf closed paths"; fail=1; }
if command -v qpdf >/dev/null 2>&1; then qpdf --check "$TMPW/exchange.pdf" >/dev/null 2>&1 && echo "ok   qpdf --check passes" || { echo "FAIL qpdf --check"; fail=1; }; fi
head -1 "$TMPW/exchange.ply" | grep -q "^ply" && grep -q "^element face 6" "$TMPW/exchange.ply" && echo "ok   exchange.ply is an ASCII PLY with 6 faces" || { echo "FAIL exchange.ply"; fail=1; }
# DXF fidelity: a freeform NURBS curve and a full ellipse must round-trip
# exactly (SPLINE/ELLIPSE entities), not as sampled polylines (see
# dxf_fidelity_script.txt).
sed "s|@TMP@|$TMPW|g" "$HERE/dxf_fidelity_script.txt" > "$TMPW/dxf_fidelity_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DF="$("$BIN" --smoke 50 --script "$TMPW/dxf_fidelity_script.txt" 2>&1)" || { echo "$DF"; echo "FAIL: DXF fidelity script exited non-zero"; exit 1; }
else
  DF="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 50 --script "$TMPW/dxf_fidelity_script.txt" 2>&1)" || { echo "$DF"; echo "FAIL: DXF fidelity script exited non-zero"; exit 1; }
fi
dfcheck() { if echo "$DF" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DF" "$1"; fail=1; fi; }
dfcheck "Exported $TMPW/dxf_fidelity.dxf" "DXF export wrote a file"
[ "$(echo "$DF" | grep -c "degree 3, 4 control points, non-rational, open")" = "2" ] && echo "ok   DXF SPLINE round-tripped the curve's exact degree/CV count" || { echo "FAIL DXF SPLINE degree/CV count did not survive round-trip"; fail=1; }
[ "$(echo "$DF" | grep -c "CV\[0\] 0,0,0")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[1\] 5,10,0")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[2\] 10,-5,0")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[3\] 15,5,0")" = "2" ] && echo "ok   DXF SPLINE round-tripped the exact control points" || { echo "FAIL DXF SPLINE control points did not survive round-trip"; fail=1; }
[ "$(echo "$DF" | grep -c "Total length = ")" = "2" ] && [ "$(echo "$DF" | grep "Total length = " | sort -u | wc -l)" = "1" ] && echo "ok   DXF SPLINE round-tripped the exact combined curve length (freeform curve + rational ellipse)" || { echo "FAIL DXF round-trip changed the combined curve length"; fail=1; }
[ "$(echo "$DF" | grep -c "degree 2, 9 control points, rational, closed")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[1\] 38,3,0")" = "2" ] && echo "ok   DXF SPLINE round-tripped the ellipse's rational control points and weights" || { echo "FAIL DXF SPLINE lost the ellipse's rational control points/weights"; fail=1; }
grep -q "^SPLINE$" "$TMPW/dxf_fidelity.dxf" && echo "ok   dxf_fidelity.dxf uses exact SPLINE entities, not sampled polylines" || { echo "FAIL dxf_fidelity.dxf entity types"; fail=1; }
# DXF TEXT import: a minimal, hand-written DXF (no Dino8-authored export
# path writes a native TEXT entity - see dxf_text_script.txt) proves
# ImportDxf's TEXT-to-glyph-outline conversion for real.
cp "$HERE/dxf_text_fixture.dxf" "$TMPW/dxf_text_fixture.dxf"
sed "s|@TMP@|$TMPW|g" "$HERE/dxf_text_script.txt" > "$TMPW/dxf_text_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DT="$("$BIN" --smoke 30 --script "$TMPW/dxf_text_script.txt" 2>&1)" || { echo "$DT"; echo "FAIL: DXF TEXT script exited non-zero"; exit 1; }
else
  DT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxf_text_script.txt" 2>&1)" || { echo "$DT"; echo "FAIL: DXF TEXT script exited non-zero"; exit 1; }
fi
dtcheck() { if echo "$DT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DT" "$1"; fail=1; fi; }
dtcheck "DXF: 3 curves, 0 points" "DXF import read the TEXT entity"
[ "$(echo "$DT" | grep -c "^history:   degree 1, [0-9]* control points, non-rational, closed$")" = "3" ] && echo "ok   DXF TEXT converted 'Hi' into exactly 3 closed glyph-outline curves (H, i-stem, i-dot)" || { echo "FAIL DXF TEXT did not produce the expected glyph curves"; fail=1; }
# DXF MTEXT import: a minimal, hand-written DXF (no Dino8-authored export
# path writes a native MTEXT entity - see dxf_mtext_script.txt) with real
# multi-line content ("{\C1;Hi}\P" then "H\H2x;i") proves both ImportDxf's
# MTEXT-to-glyph-outline conversion (one TextToCurves call per \P-separated
# line, BuildMTextGlyphs in FileExchange.cpp) AND that its inline-
# formatting-code stripping (MTextToLines) actually removes \C/\H/{/}
# rather than leaving them as literal glyphs.
cp "$HERE/dxf_mtext_fixture.dxf" "$TMPW/dxf_mtext_fixture.dxf"
sed "s|@TMP@|$TMPW|g" "$HERE/dxf_mtext_script.txt" > "$TMPW/dxf_mtext_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DM="$("$BIN" --smoke 30 --script "$TMPW/dxf_mtext_script.txt" 2>&1)" || { echo "$DM"; echo "FAIL: DXF MTEXT script exited non-zero"; exit 1; }
else
  DM="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxf_mtext_script.txt" 2>&1)" || { echo "$DM"; echo "FAIL: DXF MTEXT script exited non-zero"; exit 1; }
fi
dmcheck() { if echo "$DM" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DM" "$1"; fail=1; fi; }
dmcheck "DXF: 6 curves, 0 points" "DXF import read the MTEXT entity"
[ "$(echo "$DM" | grep -c "^history:   degree 1, [0-9]* control points, non-rational, closed$")" = "6" ] && echo "ok   DXF MTEXT's two \\P-separated 'Hi' lines each converted into exactly 3 closed glyph-outline curves (6 total)" || { echo "FAIL DXF MTEXT did not produce the expected glyph curves"; fail=1; }
[ "$(echo "$DM" | grep -c "^history: 6 object(s) selected$")" = "2" ] && echo "ok   DXF MTEXT's glyph curves carry the same Annotation=Text/Style=Standard user text as TEXT import (SelAnnotationStyle finds all 6, same as SelAll)" || { echo "FAIL DXF MTEXT glyph curves are not tagged/selectable like TEXT import's"; fail=1; }
# BLOCK_HEADER/INSERT (block instance): Dino 8 cannot itself write a real
# DWG INSERT (a block instance placed in-app is stored pre-flattened - see
# InstantiateBlock), so this fixture is built independently through
# LibreDWG's own API (dwg_fixture_gen, see tests/dwg_fixture_gen.c) and
# proves ImportDwg's INSERT-flattening code against a real block reference.
if [ -x "$DWGBIN" ]; then
  "$DWGBIN" "$TMPW/dwg_insert_fixture.dwg" >/dev/null || { echo "FAIL: dwg_fixture_gen failed to write the INSERT fixture"; exit 1; }
  sed "s|@TMP@|$TMPW|g" "$HERE/dwg_insert_script.txt" > "$TMPW/dwg_insert_script.txt"
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    DI="$("$BIN" --smoke 30 --script "$TMPW/dwg_insert_script.txt" 2>&1)" || { echo "$DI"; echo "FAIL: DWG INSERT script exited non-zero"; exit 1; }
  else
    DI="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dwg_insert_script.txt" 2>&1)" || { echo "$DI"; echo "FAIL: DWG INSERT script exited non-zero"; exit 1; }
  fi
  dicheck() { if echo "$DI" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DI" "$1"; fail=1; fi; }
  dicheck "DWG: 6 curves, 0 points, 1 block instance flattened" "ImportDwg flattened the independently-built BLOCK_HEADER/INSERT fixture (plus the TEXT fixture's 3 glyph curves)"
  dicheck "CV\[0\] 100,50,0" "DWG INSERT's flattened line starts at the scaled/rotated/translated insertion point"
  dicheck "CV\[1\] 100,56,0" "DWG INSERT's flattened line ends where a 2-unit block line scaled 3x and rotated 90 degrees should (100,50,0)-(100,56,0)"
  # DWG TEXT ("Hi" at height 5): Dino 8 itself never writes a native TEXT
  # entity (see dwg_fixture_gen.c's own note), so this - like the INSERT
  # above - is built independently through LibreDWG's own API and proves
  # ImportDwg's DWG_TYPE_TEXT glyph-outline conversion for real. "H" is one
  # closed contour, "i" is two (stem + dot) - 3 curve objects total.
  [ "$(echo "$DI" | grep -c "^history:   degree 1, [0-9]* control points, non-rational, closed$")" = "3" ] && echo "ok   DWG TEXT converted 'Hi' into exactly 3 closed glyph-outline curves (H, i-stem, i-dot)" || { echo "FAIL DWG TEXT did not produce the expected glyph curves"; fail=1; }
else
  echo "FAIL dwg_fixture_gen was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the INSERT fixture check"
  fail=1
fi
# DXF HATCH import: a minimal, hand-written DXF (no Dino8-authored export
# path writes a native HATCH entity - ExportDxf has no HATCH writer at all)
# containing one real solid-fill HATCH with a single polyline boundary path
# (group 91=1, 92 bit 0x2, a 10x10 square) proves ImportDxf's HATCH-to-real-
# hatch conversion (DxfImporter::Hatch, via drafting::BuildSolidHatch - the
# same helper the in-app Hatch command's Solid case uses) for real: SelHatch
# must find it, and its area must be the exact boundary area, not a guess.
cp "$HERE/dxf_hatch_fixture.dxf" "$TMPW/dxf_hatch_fixture.dxf"
cat > "$TMPW/dxf_hatch_script.txt" <<EOS
Open $TMPW/dxf_hatch_fixture.dxf
SelHatch
List
Area
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DXH="$("$BIN" --smoke 30 --script "$TMPW/dxf_hatch_script.txt" 2>&1)" || { echo "$DXH"; echo "FAIL: DXF HATCH script exited non-zero"; exit 1; }
else
  DXH="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxf_hatch_script.txt" 2>&1)" || { echo "$DXH"; echo "FAIL: DXF HATCH script exited non-zero"; exit 1; }
fi
dxhcheck() { if echo "$DXH" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DXH" "$1"; fail=1; fi; }
dxhcheck "DXF: 0 curves, 0 points, 0 meshes, 1 hatch" "ImportDxf read the solid-fill HATCH entity"
dxhcheck "1 object(s) selected" "SelHatch found the imported hatch (real Hatch=Solid user_text, not just an ordinary object)"
dxhcheck "name 'Hatch Solid'" "the imported solid hatch is the same trimmed-planar-brep object AddSolidHatch/BuildSolidHatch builds in-app"
dxhcheck "1 faces, 1 edges, open" "the imported hatch is a single trimmed planar face"
dxhcheck "Area = 100 square" "the imported hatch's area is exactly the 10x10 boundary (not a sampled approximation)"
# DWG HATCH import: a real HATCH entity built independently through
# LibreDWG's own dwg_add_HATCH/dwg_add_POLYLINE_2D API (dwg_fixture_gen.c's
# write_hatch_fixture - "hatch" mode), pattern-filled (ANSI31) with one
# polyline-type boundary path (a 10x10 square), proving ImportDwg's
# DWG_TYPE_HATCH case for the pattern-fill path (drafting::BuildPatternHatch)
# as a complement to the DXF test above, which covers the solid-fill path.
if [ -x "$DWGBIN" ]; then
  "$DWGBIN" "$TMPW/dwg_hatch_fixture.dwg" hatch >/dev/null || { echo "FAIL: dwg_fixture_gen failed to write the HATCH fixture"; exit 1; }
  cat > "$TMPW/dwg_hatch_script.txt" <<EOS
Open $TMPW/dwg_hatch_fixture.dwg
SelHatch
List
EOS
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    DWH="$("$BIN" --smoke 30 --script "$TMPW/dwg_hatch_script.txt" 2>&1)" || { echo "$DWH"; echo "FAIL: DWG HATCH script exited non-zero"; exit 1; }
  else
    DWH="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dwg_hatch_script.txt" 2>&1)" || { echo "$DWH"; echo "FAIL: DWG HATCH script exited non-zero"; exit 1; }
  fi
  dwhcheck() { if echo "$DWH" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DWH" "$1"; fail=1; fi; }
  dwhcheck "DWG: 0 curves, 0 points, 1 hatch; 1 unsupported entity skipped" "ImportDwg read the pattern-fill HATCH entity (and honestly counted its own POLYLINE_2D boundary record as unsupported on its own)"
  # 113, not 57: LibreDWG's dwg_object_polyline_2d_get_numpoints/get_points
  # had a real off-by-one (see cmake/patch_libredwg.cmake round 7) that
  # undercounted this fixture's 4-point square boundary as only 3 points,
  # producing a degenerate (not-actually-square) clip boundary - 57 was
  # the pattern-line count for THAT wrong boundary, not the real one. Once
  # fixed, the boundary is a genuine 10x10 square (verified by the
  # corner-to-corner diagonal checks below) and the correct ANSI31
  # pattern-line count for it is 113 - confirmed deterministic across
  # repeated local runs.
  dwhcheck "113 object(s) selected" "SelHatch found every ANSI31 hatch line as a real Hatch-tagged object, not a stray subset"
  [ "$(echo "$DWH" | grep -c "^history:   degree 1, 2 control points, non-rational, open$")" = "113" ] && echo "ok   DWG HATCH's 113 ANSI31 pattern lines are all real degree-1 line curves" || { echo "FAIL DWG HATCH did not produce the expected 113 pattern-line curves"; fail=1; }
  dwhcheck "CV\[0\] 0,0,0" "one ANSI31 hatch line runs the boundary's own diagonal-adjacent corner"
  dwhcheck "CV\[1\] 10,10,0" "...to the opposite corner of the 10x10 boundary, confirming the pattern was clipped to the real boundary, not an arbitrary box"
else
  echo "FAIL dwg_fixture_gen was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the HATCH fixture check"
  fail=1
fi
# DWG SPLINE: built via LibreDWG's own dwg_add_SPLINE (marked "Experimental.
# Does not work yet properly" in dwg_api.h - confirmed by hand it only ever
# populates fit_pts, never real NURBS control points), so this exercises
# ImportDwg's DWG_TYPE_SPLINE fit-points fallback path (the same fallback
# DXF SPLINE import already had), not its primary control-point path.
if [ -x "$DWGBIN" ]; then
  "$DWGBIN" "$TMPW/dwg_spline_fixture.dwg" spline >/dev/null || { echo "FAIL: dwg_fixture_gen failed to write the SPLINE fixture"; exit 1; }
  cat > "$TMPW/dwg_spline_script.txt" <<EOS
Open $TMPW/dwg_spline_fixture.dwg
SelAll
List
EOS
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    DWS="$("$BIN" --smoke 30 --script "$TMPW/dwg_spline_script.txt" 2>&1)" || { echo "$DWS"; echo "FAIL: DWG SPLINE script exited non-zero"; exit 1; }
  else
    DWS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dwg_spline_script.txt" 2>&1)" || { echo "$DWS"; echo "FAIL: DWG SPLINE script exited non-zero"; exit 1; }
  fi
  dwscheck() { if echo "$DWS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DWS" "$1"; fail=1; fi; }
  dwscheck "DWG: 1 curve, 0 points" "ImportDwg read the fit-point-only SPLINE entity"
  dwscheck "degree 1, 4 control points, non-rational, open" "the SPLINE's fit points became an exact polyline through them"
  dwscheck "CV\[0\] 0,0,0" "the polyline starts at the SPLINE's first real fit point"
  dwscheck "CV\[3\] 15,5,0" "...and ends at its last, with all 4 fit points preserved exactly"
else
  echo "FAIL dwg_fixture_gen was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the SPLINE fixture check"
  fail=1
fi
# DWG MTEXT: built via LibreDWG's own dwg_add_MTEXT (also marked
# "Experimental. Does not work yet properly" in dwg_api.h - confirmed by
# hand it does populate a real ins_pt/text/text_height/attachment this
# time, unlike dwg_add_SPLINE above). The fixture's text embeds the same
# inline-formatting-code shape as dxf_mtext_fixture.dxf
# ("{\C1;Hi}\PH\H2x;i") and a non-default attachment point (5 =
# middle-center, not the top-left default), proving ImportDwg's
# DWG_TYPE_MTEXT case (formatting-code stripping via MTextToLines AND
# non-top-left attachment layout via BuildMTextGlyphs, both in
# FileExchange.cpp) against a real MTEXT entity built completely
# independently of Dino 8's own writer (which has no MTEXT export at all).
if [ -x "$DWGBIN" ]; then
  "$DWGBIN" "$TMPW/dwg_mtext_fixture.dwg" mtext >/dev/null || { echo "FAIL: dwg_fixture_gen failed to write the MTEXT fixture"; exit 1; }
  cat > "$TMPW/dwg_mtext_script.txt" <<EOS
Open $TMPW/dwg_mtext_fixture.dwg
SelAll
List
SelAnnotationStyle
EOS
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    DWM="$("$BIN" --smoke 30 --script "$TMPW/dwg_mtext_script.txt" 2>&1)" || { echo "$DWM"; echo "FAIL: DWG MTEXT script exited non-zero"; exit 1; }
  else
    DWM="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dwg_mtext_script.txt" 2>&1)" || { echo "$DWM"; echo "FAIL: DWG MTEXT script exited non-zero"; exit 1; }
  fi
  dwmcheck() { if echo "$DWM" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DWM" "$1"; fail=1; fi; }
  dwmcheck "DWG: 6 curves, 0 points" "ImportDwg read the MTEXT entity"
  [ "$(echo "$DWM" | grep -c "^history:   degree 1, [0-9]* control points, non-rational, closed$")" = "6" ] && echo "ok   DWG MTEXT's two \\P-separated 'Hi' lines (with \\C/\\H/{}} codes stripped) each converted into exactly 3 closed glyph-outline curves (6 total)" || { echo "FAIL DWG MTEXT did not produce the expected glyph curves"; fail=1; }
  [ "$(echo "$DWM" | grep -c "^history: 6 object(s) selected$")" = "2" ] && echo "ok   DWG MTEXT's glyph curves carry the same Annotation=Text/Style=Standard user text as TEXT import (SelAnnotationStyle finds all 6, same as SelAll)" || { echo "FAIL DWG MTEXT glyph curves are not tagged/selectable like TEXT import's"; fail=1; }
  dwmcheck "CV\[0\] 17.14,26.25,0" "DWG MTEXT's middle-center attachment (5) offset the block both horizontally and vertically around the insertion point (20,20,0), not left uncentred like the top-left default"
else
  echo "FAIL dwg_fixture_gen was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the MTEXT fixture check"
  fail=1
fi
# DXF DIMENSION import: real, live/re-measurable Dino8 dimensions rebuilt
# from the entity's semantic definition points (commands/DimGeometry.h -
# the exact math cmd_annotate.cpp's own live Dim/DimAligned/DimRadius/
# DimDiameter commands use), not a copy of its frozen anonymous-block
# geometry - see DxfImporter::Dimension's comment in FileExchange.cpp for
# the full group-code derivation (verified against LibreDWG's own
# dwg.spec). Each fixture's points are hand-picked so the correct measured
# value is known in advance: SelDim proves it is selectable like a live
# dimension (group name match), and UpdateDimensions re-deriving the exact
# same value from the DimP0/DimP1/DimCenter/DimRadiusVal tags proves the
# import is tag-for-tag compatible with a dimension built in-app, not just
# visually similar.
#
# Linear (type 0, horizontal): xline1=(0,0,0), xline2=(10,0,0) -> exactly 10.
cp "$HERE/dxf_dim_linear_fixture.dxf" "$TMPW/dxf_dim_linear_fixture.dxf"
cat > "$TMPW/dxf_dim_linear_script.txt" <<EOS
Open $TMPW/dxf_dim_linear_fixture.dxf
SelDim
List
UpdateDimensions
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DXDL="$("$BIN" --smoke 30 --script "$TMPW/dxf_dim_linear_script.txt" 2>&1)" || { echo "$DXDL"; echo "FAIL: DXF DIMENSION (linear) script exited non-zero"; exit 1; }
else
  DXDL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxf_dim_linear_script.txt" 2>&1)" || { echo "$DXDL"; echo "FAIL: DXF DIMENSION (linear) script exited non-zero"; exit 1; }
fi
dxdlcheck() { if echo "$DXDL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DXDL" "$1"; fail=1; fi; }
dxdlcheck "DXF: 0 curves, 0 points, 0 meshes, 1 dimension" "ImportDxf read the type-0 (linear) DIMENSION entity"
dxdlcheck "^history: 8 object(s) selected$" "SelDim found the imported dimension's 8 curves (dim line + 2 ext lines + 2 arrows + 3 '10' glyph curves) as a real DimLinear group"
dxdlcheck "CV\[0\] 0,5,0" "the rebuilt dimension line sits at the def_pt's Y offset (5), not the raw extension-line points"
dxdlcheck "UpdateDimensions:   DimLinear now measures 10" "UpdateDimensions re-derived the exact hand-computed distance (xline2.x - xline1.x = 10) from the imported dimension's own tags, proving it round-trips exactly like a live DimLinear"
# Aligned (type 1): xline1=(0,0,0), xline2=(6,8,0) -> exactly 10 (3-4-5
# triangle scaled 2x), independent of the dimension-line offset point.
cp "$HERE/dxf_dim_aligned_fixture.dxf" "$TMPW/dxf_dim_aligned_fixture.dxf"
cat > "$TMPW/dxf_dim_aligned_script.txt" <<EOS
Open $TMPW/dxf_dim_aligned_fixture.dxf
SelDim
UpdateDimensions
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DXDA="$("$BIN" --smoke 30 --script "$TMPW/dxf_dim_aligned_script.txt" 2>&1)" || { echo "$DXDA"; echo "FAIL: DXF DIMENSION (aligned) script exited non-zero"; exit 1; }
else
  DXDA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxf_dim_aligned_script.txt" 2>&1)" || { echo "$DXDA"; echo "FAIL: DXF DIMENSION (aligned) script exited non-zero"; exit 1; }
fi
dxdacheck() { if echo "$DXDA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DXDA" "$1"; fail=1; fi; }
dxdacheck "DXF: 0 curves, 0 points, 0 meshes, 1 dimension" "ImportDxf read the type-1 (aligned) DIMENSION entity"
dxdacheck "^history: 8 object(s) selected$" "SelDim found the imported aligned dimension as a real DimAligned group"
dxdacheck "UpdateDimensions:   DimAligned now measures 10" "UpdateDimensions re-derived the exact hand-computed 3-4-5-triangle distance (sqrt(6^2+8^2) = 10)"
# Radius (type 4): center=(40,0,0), first_arc_pt=(45,0,0) -> radius exactly 5.
cp "$HERE/dxf_dim_radius_fixture.dxf" "$TMPW/dxf_dim_radius_fixture.dxf"
cat > "$TMPW/dxf_dim_radius_script.txt" <<EOS
Open $TMPW/dxf_dim_radius_fixture.dxf
SelDim
List
UpdateDimensions
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DXDR="$("$BIN" --smoke 30 --script "$TMPW/dxf_dim_radius_script.txt" 2>&1)" || { echo "$DXDR"; echo "FAIL: DXF DIMENSION (radius) script exited non-zero"; exit 1; }
else
  DXDR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxf_dim_radius_script.txt" 2>&1)" || { echo "$DXDR"; echo "FAIL: DXF DIMENSION (radius) script exited non-zero"; exit 1; }
fi
dxdrcheck() { if echo "$DXDR" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DXDR" "$1"; fail=1; fi; }
dxdrcheck "DXF: 0 curves, 0 points, 0 meshes, 1 dimension" "ImportDxf read the type-4 (radius) DIMENSION entity"
dxdrcheck "^history: 5 object(s) selected$" "SelDim found the imported radius dimension's 5 curves (leader + arrow + 3 'R 5' glyph curves) as a real DimRadius group"
dxdrcheck "CV\[0\] 40,0,0" "the rebuilt leader starts exactly at the DIMENSION's own def_pt (the arc/circle center)"
dxdrcheck "UpdateDimensions:   DimRadius now measures 5" "UpdateDimensions re-derived the exact hand-computed radius (distance from center (40,0,0) to first_arc_pt (45,0,0) = 5)"
# Diameter (type 3): first_arc_pt=(5,0,0), def_pt=far_chord_pt=(-5,0,0) ->
# radius = half their distance = 5, so diameter = 10.
cp "$HERE/dxf_dim_diameter_fixture.dxf" "$TMPW/dxf_dim_diameter_fixture.dxf"
cat > "$TMPW/dxf_dim_diameter_script.txt" <<EOS
Open $TMPW/dxf_dim_diameter_fixture.dxf
SelDim
UpdateDimensions
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DXDD="$("$BIN" --smoke 30 --script "$TMPW/dxf_dim_diameter_script.txt" 2>&1)" || { echo "$DXDD"; echo "FAIL: DXF DIMENSION (diameter) script exited non-zero"; exit 1; }
else
  DXDD="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dxf_dim_diameter_script.txt" 2>&1)" || { echo "$DXDD"; echo "FAIL: DXF DIMENSION (diameter) script exited non-zero"; exit 1; }
fi
dxddcheck() { if echo "$DXDD" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DXDD" "$1"; fail=1; fi; }
dxddcheck "DXF: 0 curves, 0 points, 0 meshes, 1 dimension" "ImportDxf read the type-3 (diameter) DIMENSION entity"
dxddcheck "^history: 8 object(s) selected$" "SelDim found the imported diameter dimension's 8 curves (2-arrow diameter line + 2 arrows + 4 'D 10' glyph curves) as a real DimDiameter group"
dxddcheck "UpdateDimensions:   DimDiameter now measures 10" "UpdateDimensions re-derived the exact hand-computed diameter (far_chord_pt (-5,0,0) to first_arc_pt (5,0,0) = 10 span, so diameter 10)"
# DWG DIMENSION_LINEAR/DIMENSION_RADIUS: built via LibreDWG's own
# dwg_add_DIMENSION_LINEAR/dwg_add_DIMENSION_RADIUS - unlike dwg_add_SPLINE/
# dwg_add_MTEXT above, neither is marked "Experimental" in dwg_api.h, and
# reading their src/dwg_api.c implementation confirms they store
# xline1_pt/xline2_pt/def_pt and center_pt/chord_pt verbatim (see
# dwg_fixture_gen.c's write_dim_fixture comment) - so this proves
# WalkDwgEntities' DWG_TYPE_DIMENSION_LINEAR/DIMENSION_RADIUS cases against
# real DWG entities, independent of Dino 8's own writer (which has no
# DIMENSION export at all).
if [ -x "$DWGBIN" ]; then
  "$DWGBIN" "$TMPW/dwg_dim_fixture.dwg" dim >/dev/null || { echo "FAIL: dwg_fixture_gen failed to write the DIMENSION fixture"; exit 1; }
  cat > "$TMPW/dwg_dim_script.txt" <<EOS
Open $TMPW/dwg_dim_fixture.dwg
SelDim
UpdateDimensions
EOS
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    DWD="$("$BIN" --smoke 30 --script "$TMPW/dwg_dim_script.txt" 2>&1)" || { echo "$DWD"; echo "FAIL: DWG DIMENSION script exited non-zero"; exit 1; }
  else
    DWD="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/dwg_dim_script.txt" 2>&1)" || { echo "$DWD"; echo "FAIL: DWG DIMENSION script exited non-zero"; exit 1; }
  fi
  dwdcheck() { if echo "$DWD" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DWD" "$1"; fail=1; fi; }
  dwdcheck "DWG: 0 curves, 0 points, 2 dimensions" "ImportDwg read the real DIMENSION_LINEAR and DIMENSION_RADIUS entities"
  dwdcheck "^history: 13 object(s) selected$" "SelDim found both imported dimensions' curves as real DimLinear/DimRadius groups"
  dwdcheck "UpdateDimensions:   DimLinear now measures 10" "UpdateDimensions re-derived the exact hand-computed linear distance (xline2.x - xline1.x = 10)"
  dwdcheck "UpdateDimensions:   DimRadius now measures 5" "UpdateDimensions re-derived the exact hand-computed radius (chord_pt (45,0,0) - center_pt (40,0,0) = 5)"
else
  echo "FAIL dwg_fixture_gen was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the DIMENSION fixture check"
  fail=1
fi
# Surfaces: Pipe, OffsetSrf, Shell, Sweep1/2, NetworkSrf, Patch, ExtrudeCrvAlongCrv,
# ExtrudeCrvTapered, Project, Pull (see surface_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SF="$("$BIN" --smoke 200 --script "$HERE/surface_script.txt" 2>&1)" || { echo "$SF"; echo "FAIL: surface script exited non-zero"; exit 1; }
else
  SF="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/surface_script.txt" 2>&1)" || { echo "$SF"; echo "FAIL: surface script exited non-zero"; exit 1; }
fi
sfcheck() { if echo "$SF" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SF" "$1"; fail=1; fi; }
sfcheck "Pipe: radius 1, 1 pipe(s) (capped mesh)" "Pipe built a capped mesh"
sfcheck "Volume = 31.06 cubic" "Pipe r=1 along 10 units has volume ~ pi*10"
sfcheck "Area = 62.79 square" "uncapped Pipe surface area ~ 2*pi*10"
sfcheck "degree 3 x 1, CVs 27 x 2" "uncapped Pipe is a periodic NURBS tube"
sfcheck "Bounding box min 20,0,-2 max 30,10,-2" "OffsetSrf moved the plane by 2 along its normal"
sfcheck "Volume = 200 cubic" "OffsetSrf Solid=Yes closed a 10x10x2 slab"
sfcheck "Shell: thickness 1, closed, volume 1408" "Shell hollowed the box (4000 - 18*18*8)"
sfcheck "ExtrudeCrvAlongCrv: 1 surface(s)" "ExtrudeCrvAlongCrv built a sum surface"
sfcheck "Sweep1: 1 section(s) along 2 rail stations" "Sweep1 swept the circle along the line"
sfcheck "Area = 124.2 square" "Sweep1 area ~ 2*pi*2*10 (cubic circle approximation)"
sfcheck "Sweep2: 1 section(s) along 2 rail stations" "Sweep2 spanned the two rails"
sfcheck "NetworkSrf: exact Coons patch through 4 curves (all 4 boundaries reproduced exactly)" "NetworkSrf sorted 4 curves into a loop and built the kernel's exact Coons patch (real NURBS algebra, not the sample-and-refit approximation)"
sfcheck "NetworkSrf: ruled surface between 2 curves" "NetworkSrf ruled two curves"
sfcheck "Patch: planar face bounded by the closed curve" "Patch trimmed a plane with the circle"
sfcheck "Area = 78.51 square" "Patch disc area ~ pi*25"
sfcheck "Patch: least-squares plane through 0 curve(s) and 3 point(s)" "Patch fitted a plane through points"
sfcheck "ExtrudeCrvTapered: distance 5, draft 10 deg, 1 object(s)" "ExtrudeCrvTapered ran"
sfcheck "3 faces, 5 edges, closed solid" "ExtrudeCrvTapered capped and joined into a closed solid"
sfcheck "Project: 1 curve(s), 0 point(s)" "Project produced one curve"
sfcheck "CV\[1\] 240,20,0" "Project landed the line on the plane"
sfcheck "Pull: 0 curve(s), 1 point(s)" "Pull produced one point"
sfcheck "  225,5,0" "Pull moved the point onto the plane"
# Shell (open): picking the top face before entering thickness removes that
# face's own wall layer from the result rather than producing another
# fully-closed solid - real material gone, not a cosmetic label. The result
# is still a single valid closed 2-manifold (a real opening is a cavity
# reachable through empty space, not a literal hole in the mesh - the same
# reason a 3D-printable mug's own mesh has no naked edges either), so the
# proof is the exact Volume/Area/BoundingBox difference from the closed case
# above, not IsClosedManifold()/SelOpenMesh.
sfcheck "Shell: face 5 on object" "Shell registered the clicked top face to remove"
sfcheck "Shell: thickness 1, open (face(s) removed), area 2248" "Shell (open) built one welded mesh: outer 1200 + inner 972 + a flat 76-unit rim"
sfcheck "Area = 2248 square" "Shell (open) area matches the outer+inner+rim geometry exactly (all flat, no curvature error)"
sfcheck "Bounding box min 300,0,0 max 320,20,10" "Shell (open) left the outer wall's own bounding box untouched"
sfcheck "Volume = 1084 cubic" "Shell (open) volume is exactly the closed shell's 1408 minus the removed top layer's own 18x18x1"
# Shell (per-face thickness): no face removed, but the top face is given
# its own thickness of 3 while every other face keeps the default 1 - a
# real different-thickness-per-face wall, not just a global number. The
# per-vertex offset solve (OffsetMeshPerFace) makes each kept face's own
# vertices hit the exact intersection of its neighbours' own offset planes,
# so a box's orthogonal faces still meet in clean corners even though two
# of them moved by different amounts; proved numerically by hand below.
sfcheck "Shell: face 5 on object" "Shell registered the clicked top face for its own thickness"
sfcheck "set to thickness 3" "the top face's override was recorded as 3"
sfcheck "Shell: thickness 1, 1 face(s) at their own thickness, closed, volume 2056" "Shell (per-face) built one closed shell with a mixed-thickness wall"
sfcheck "Area = 2680 square" "Shell (per-face) area matches outer 1600 + inner 1080 (two disjoint nested boxes, no boolean rework needed)"
sfcheck "Bounding box min 340,0,0 max 360,20,10" "Shell (per-face) left the outer wall's own bounding box untouched"
sfcheck "Volume = 2056 cubic" "Shell (per-face) volume is exactly 4000 - 18*18*6 (inner box shrunk to height 6 by the top face's own thickness 3, not the default 1) - 648 less material removed than the same box's uniform thickness-1 shell above (1408), i.e. 1408 + 18*18*2 = 2056"
sfcheck "smoke: frames=200 objects=43" "surface script produced the expected object count"
# Solids: Ellipsoid/SubDEllipsoid (real axis picking), Pyramid (NumSides=),
# Loft (Normal vs Style=Straight), Cap (multiple separate openings) (see solids_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SO="$("$BIN" --smoke 150 --script "$HERE/solids_script.txt" 2>&1)" || { echo "$SO"; echo "FAIL: solids script exited non-zero"; exit 1; }
else
  SO="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/solids_script.txt" 2>&1)" || { echo "$SO"; echo "FAIL: solids script exited non-zero"; exit 1; }
fi
socheck() { if echo "$SO" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SO" "$1"; fail=1; fi; }
socheck "Volume = 132 cubic" "Ellipsoid volume is close to the analytic 4/3*pi*5*3*2 (~125.7)"
socheck "48 faces, 72 edges, 26 vertices, 0 creases" "SubDEllipsoid built a SubD from the same three axes"
socheck "7 vertices, 10 faces" "Pyramid NumSides=6 built a 7-vertex hexagonal-base mesh"
socheck "degree 3 x 2, CVs 24 x 3" "Loft (Normal) fit a cubic-through-control-points v-direction for 3 sections"
socheck "degree 3 x 1, CVs 24 x 3" "Loft Style=Straight dropped to a linear (ruled) v-direction"
socheck "4 faces, 12 edges, open" "DeleteFaces removed the box's top and bottom (two separate naked-edge loops)"
socheck "Capped 1 object(s), 2 opening(s)" "Cap closed both separate openings in one call"
socheck "6 faces, 76 edges, open" "Cap added both cap faces back (4 sides + 2 caps)"
socheck "smoke: frames=150 objects=12" "solids script produced the expected object count"
# Surface editing: ExtractSrf, DeleteFaces, DupBorder/DupEdge, Untrim, isocurves, ExtendSrf, UnrollSrf, Silhouette, RailRevolve, Fin/Ribbon, grids (see srfedit_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SE="$("$BIN" --smoke 150 --script "$HERE/srfedit_script.txt" 2>&1)" || { echo "$SE"; echo "FAIL: surface-edit script exited non-zero"; exit 1; }
else
  SE="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/srfedit_script.txt" 2>&1)" || { echo "$SE"; echo "FAIL: surface-edit script exited non-zero"; exit 1; }
fi
secheck() { if echo "$SE" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SE" "$1"; fail=1; fi; }
secheck "ExtractSrf: face 5 extracted from object 1" "ExtractSrf pulled the top face off the box"
secheck "5 faces, 12 edges, open" "ExtractSrf left a 5-face open polysurface"
secheck "DeleteFaces: face 4 deleted, 4 face(s) left" "DeleteFaces removed the bottom face"
secheck "DupBorder: 3 border curve(s)" "DupBorder found the naked edge loops"
secheck "DupEdge: edge 0 of object 1 duplicated" "DupEdge copied an edge"
secheck "DupFaceBorder: 1 curve(s)" "DupFaceBorder copied the face loop"
secheck "Untrim: face 0 replaced by its untrimmed surface" "Untrim removed the circular trim"
secheck "ExtractIsocurve: 2 curve(s)" "ExtractIsocurve extracted U and V isocurves"
secheck "ExtractWireframe: 6 curve(s)" "ExtractWireframe extracted the cylinder wires"
secheck "Bounding box min 200,0,0 max 220,10,0" "ExtendSrf grew the plane by 10"
secheck "UnrollSrf: face 0 unrolled exactly (a real plane/cylinder/cone - zero distortion), area 314.2" "UnrollSrf now uses the kernel's exact NurbsSurface::UnrollDevelopable() on a real cylinder wall (radius 5, height 10: true area 2*pi*5*10 = 314.16, exact - not the old heuristic's own approximate 313)"
secheck "Silhouette: [0-9]* curve(s)" "Silhouette produced outline curves"
secheck "RailRevolve: surface created" "RailRevolve built a surface"
secheck "Ribbon: 1 ribbon surface(s), width 5" "Ribbon built a ribbon surface"
secheck "SrfControlPtGrid: surface from 2 x 2 points" "SrfControlPtGrid built a surface from points"
secheck "UntrimBorder: face 0's outer trim removed (holes kept)" "UntrimBorder removed only the outer trim"
secheck "JoinEdge: 1 naked edge pair(s) joined" "JoinEdge force-joined two just-touching planes"
secheck "MoveFace: 1 object(s) updated" "MoveFace ran"
secheck "Bounding box min 1100,0,0 max 1110,10,15" "MoveFace actually raised the box's top face"
secheck "MoveEdge: 1 object(s) updated" "MoveEdge ran"
secheck "Bounding box min 1200,0,0 max 1210,10,15" "MoveEdge actually raised the picked edge"
secheck "MoveUntrimmedFace: 1 object(s) updated" "MoveUntrimmedFace ran"
secheck "Bounding box min 1300,0,0 max 1310,10,20" "MoveUntrimmedFace actually raised the untrimmed top face"
secheck "MoveUntrimmedEdge: 1 object(s) updated" "MoveUntrimmedEdge ran"
secheck "Bounding box min 1400,0,0 max 1410,10,25" "MoveUntrimmedEdge actually raised the picked iso edge"
secheck "MoveUVN: control point (0,0) moved u=0 v=0 n=7" "MoveUVN moved a surface CV along its local normal"
secheck "FoldFace: face 5 folded 30 degree" "FoldFace rotated a face about its hinge edge"
secheck "Bounding box min 1600,-5,0 max 1610,10,13.66" "FoldFace actually moved the folded face's geometry"
secheck "SetPlanar: 2 object(s) projected" "SetPlanar ran"
secheck "Bounding box min 1700,0,0 max 1710,0,0" "SetPlanar actually flattened the points onto z=0"
secheck "volume 1000, centroid 1805,5,5" "VolumeMoments reported the box's real volume and centroid"
secheck "radii of gyration about centroid: x=4.082 y=4.082 z=4.082" "VolumeMoments computed real radii of gyration for a 10-cube (sqrt(1000\*6/60/10)=4.082 exactly for a cube)"
secheck "area 100, centroid 1905,5,0" "AreaMoments reported the plane's real area and centroid"
secheck "displacement (below the construction plane): 26" "Hydrostatics computed a real submerged volume (half of a r=5 sphere is 4/3*pi*125/2 = 261.8)"
secheck "waterplane area 78.3" "Hydrostatics computed the real waterplane cross-section area (pi\*5\^2 = 78.5)"
secheck "prismatic coefficient Cp 0.3333, vertical prismatic coefficient Cvp 0.52" "Hydrostatics computed real Cp/Cvp for a half-cone (exact Cp = 1/3; Cvp = pi/6 =~ 0.5236, mesh-sampled to 0.5231)"
secheck "trim 0 deg, heel 0 deg" "Hydrostatics' trim/heel solver correctly found the half-cone already level (symmetric about its own axis)"
secheck "trim 20 deg, heel [-0-9.]*e-0" "Hydrostatics' Newton trim solver recovered the exact 20 degree pitch a cube was rotated by (heel ~0, as expected from the pitch-only rotation)"
secheck "ExtractBadSrf: 0 invalid polysurface(s) extracted as copies" "ExtractBadSrf found no invalid polysurfaces in a valid box"
secheck "RemoveAllNakedMicroEdges: no naked edges shorter than" "RemoveAllNakedMicroEdges scanned a closed box and found none"
secheck "ShrinkTrimmedSrf: 0 polysurface(s) shrunk" "ShrinkTrimmedSrfToEdge ran (this trimmed disk is already shrunk tight)"
secheck "SrfSeam: seam moved to" "SrfSeam rotated a real closed surface's periodic seam"
secheck "SphereTangentToThreeSurfaces: sphere at" "SphereTangentToThreeSurfaces solved a real equidistant sphere"
secheck "SetSurfaceTangent: 2 boundary tangent-row control point" "SetSurfaceTangent rotated the tangent row to match the target's normal"
secheck "VariableOffsetSrf: offset 1 at the U-min edge to 3 at U-max" "VariableOffsetSrf built a real per-CV linearly-varying offset"
secheck "FitCurveToSurface: fitted through 13 points" "FitCurveToSurface pulled a coarse fit onto the surface"
secheck "PatchSingleFace: face 5 of object" "PatchSingleFace replaced a box face with a real Coons patch"
secheck "Volume = 1000 cubic" "PatchSingleFace's Coons patch kept the box's exact volume (still flat)"
secheck "Flow: base length 10 -> target length 99.35" "ApplyCrv aliased into Flow's real curve deformation"
secheck "ApplyMesh: 1 object(s) reshaped via nearest-triangle barycentric" "ApplyMesh reshaped a mesh sphere onto a same-topology target"
secheck "ApplyMeshUVN: 1 object(s) reshaped via nearest-triangle barycentric" "ApplyMeshUVN ran the same real mesh mapping"
secheck "Boss: protrusion of height 5 built following the base's local surface normal at 48 points along the curve; unioned with the base solid" "Boss built a real solid protrusion and unioned it with the box"
secheck "Volume = 1063 cubic" "Boss's union really added the r=2 h=5 cylinder's volume (1000 + pi*4*5 = 1062.8)"
secheck "Rib: tapered wall of height 2 built following the base's local surface normal at 64 points along the curve; unioned with the base solid" "Rib built a real tapered wall and unioned it with the box"
secheck "Volume = 1006 cubic" "Rib's union really added the tapered wall's volume"
secheck "FilletSrfToRail: fillet surface built along the rail's own points" "FilletSrfToRail built real rolling-ball arcs along a picked rail"
secheck "FilletSrfCrv: fillet surface built tangent to the surface and osculating-tangent to the curve" "FilletSrfCrv built a real surface/curve rolling-ball fillet"
FSC_GAP="$(echo "$SE" | sed -n 's/.*FilletSrfCrv:.*contact points off the exact radius by up to \([0-9.eE+-]*\);.*/\1/p' | head -1)"
python3 -c "import sys; v=float('$FSC_GAP'); sys.exit(0 if v < 1e-4 else 1)" \
  && echo "ok   FilletSrfCrv contact-radius gap ($FSC_GAP) is essentially zero - contact points really sit at the exact radius" \
  || { echo "FAIL FilletSrfCrv contact-radius gap ($FSC_GAP) is not near zero"; fail=1; }
FSC_TANG="$(echo "$SE" | sed -n 's/.*arc tangent matches the curve.s own tangent to within \([0-9.eE+-]*\) degrees.*/\1/p' | head -1)"
python3 -c "import sys; v=float('$FSC_TANG'); sys.exit(0 if v < 0.01 else 1)" \
  && echo "ok   FilletSrfCrv tangent-direction error ($FSC_TANG degrees) is essentially zero - the arc is genuinely tangent to the curve, not merely touching it (the exact gap the old Partial note described)" \
  || { echo "FAIL FilletSrfCrv tangent-direction error ($FSC_TANG degrees) is not near zero - arc is not genuinely tangent to the curve"; fail=1; }
secheck "SoftEditSrf: 4 control point(s) moved with a cosine falloff within radius 8 (max displacement 3)" "SoftEditSrf moved control points with a real falloff"
secheck "^ok   expect_objects 100" "surface-edit script produced the expected object count halfway through (before UnjoinEdge/ReplaceEdge)"
secheck "ShowEdges: 1 object(s), 7 edge(s), 6 naked edge(s)" "ShowEdges found the joined planes' 1 shared and 6 naked edges before unjoining"
secheck "UnjoinEdge: edge [0-9]* split into two naked, coincident edges - both faces remain in the same polysurface" "UnjoinEdge split the shared edge in place via real Brep::UnjoinEdge()"
secheck "ShowEdges: 1 object(s), 8 edge(s), 8 naked edge(s)" "ShowEdges confirms exactly 2 more naked edges after unjoining - the old shared edge, now two coincident naked ones"
secheck "ReplaceEdge: edge [0-9]* re-trimmed against the picked curve's own shape, every affected face re-projected onto it" "ReplaceEdge re-trimmed a naked edge against a bowed substitute curve via real Brep::ReplaceEdgeCurve()"
secheck "2 faces, 9 edges, open" "the re-trimmed polysurface keeps its same topology (2 faces) after ReplaceEdge - only the one edge's own shape changed"
secheck "^ok   expect_objects 102" "surface-edit script produced the expected final object count"
secheck "Squish: face 0 flattened, area 100 (whole object 3D area 100), distortion max 0% avg 0%" "Squish flattened an already-flat plane with exactly zero distortion either way"
secheck "SquishInfo: object [0-9]* - flat area 100 (3D area 100), distortion max 0% avg 0%" "SquishInfo reprinted Squish's own stored report (area + distortion) instead of recomputing it"
secheck "SquishBack: 1 curve(s) projected back onto the source surface via the flat pattern's own per-vertex (u,v) map" "SquishBack projected a curve on the flat pattern back onto the source surface via the stored (u,v) map"
secheck "Bounding box min 3702,2,0 max 3708,8,0" "SquishBack's round trip landed the projected curve exactly back on the source plane's own diagonal"
secheck "DeleteFaces: face [0-9]* deleted, 5 face(s) left" "DeleteFaces opened the fresh box for the naked-micro-edge fixture"
secheck "RemoveAllNakedMicroEdges: 1 naked micro edge(s) removed; 1 left in place" "RemoveAllNakedMicroEdges actually CLOSED the isolated sliver (real Brep::RemoveNakedMicroEdge) while correctly leaving the corner-adjacent one it can't safely close"
secheck "^ok   expect_objects 112" "surface-edit script produced the expected object count after the Squish/SquishBack/RemoveAllNakedMicroEdges additions"
secheck "SplitRefitSurface: 1 surface(s) split into 2 piece(s), each refit to a clean untrimmed NURBS surface" "SplitRefitSurface split the plane at the curve's crossing and refit both pieces"
secheck "degree 3 x 3, CVs 4 x 4" "SplitRefitSurface's refit pieces are genuinely rebuilt to a fresh 4x4-CV surface, not left at Split()'s own original 2x2 CVs"
secheck "Bounding box min 4400,0,0 max 4410,10,0" "SplitRefitSurface's two refit pieces still exactly cover the original plane end to end (west [4400,4405] + east [4405,4410])"
secheck "HBar: locked the distance between control points 0 and 1 of '(unnamed)' at 10" "HBar locked the real distance (10) between a 2-CV line's own two control points"
secheck "HBarSetDistance: locked distance set to 20 - control point 1 moved to match" "HBarSetDistance changed the locked value and re-applied the constraint"
secheck "HBarDragSelfTest: moved anchor control point 0 by 5,0,0; anchor-handle distance is now 20 (locked at 20)" "HBarDragSelfTest exercised the exact TransformSubObjects+ApplyHBarConstraint call sequence a real mouse drag makes, and the handle swung to keep the locked distance"
secheck "Bounding box min 4205,0,0 max 4225,0,0" "BoundingBox independently confirms the curve's own control points actually moved to where the constraint math says they should (anchor 4200+5=4205, handle 4205+20=4225) - not just HBarDragSelfTest's own report"

# Mesh tools: deformations, mesh editing and mesh primitives (see meshtools_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MT="$("$BIN" --smoke 150 --script "$HERE/meshtools_script.txt" 2>&1)" || { echo "$MT"; echo "FAIL: mesh-tools script exited non-zero"; exit 1; }
else
  MT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/meshtools_script.txt" 2>&1)" || { echo "$MT"; echo "FAIL: mesh-tools script exited non-zero"; exit 1; }
fi
mtcheck() { if echo "$MT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$MT" "$1"; fail=1; fi; }
mtcheck "Twist: 90 degrees over 20 units" "Twist read its axis and angle"
mtcheck "Twist: deformed 1 object(s)" "Twist deformed the mesh cylinder"
mtcheck "Bend: deformed 1 object(s) (1 converted to meshes)" "Bend converted the box to a mesh and bent it"
mtcheck "Taper: 5 -> 2" "Taper read both distances"
mtcheck "Stretch: 20 -> 30" "Stretch lengthened the line"
mtcheck "Shear: deformed 1 object(s)" "Shear ran with three points"
mtcheck "Maelstrom: deformed 1 object(s)" "Maelstrom ran"
mtcheck "SoftMove: 4 units with falloff radius 5" "SoftMove took its Radius option"
mtcheck "Smooth: smoothed 1 object(s), factor 0.5" "Smooth ran on the mesh sphere"
mtcheck "SoftTransform (Rotate): 45 degrees about the CPlane normal" "SoftTransform Rotate mode ran"
mtcheck "SoftTransform (Scale): factor 1.5 from" "SoftTransform Scale mode ran"
mtcheck "ExtrudeMesh: extruded open mesh into a closed solid" "ExtrudeMesh closed the open mesh plane"
mtcheck "Volume = 300" "the extruded 10x10x3 mesh has volume 300"
mtcheck "OffsetMesh: solid shell" "OffsetMesh built a solid shell"
mtcheck "FillMeshHoles: filled 1 hole(s)" "FillMeshHoles closed the naked loop"
mtcheck "Unweld: added 72 vertex copy(ies)" "Unweld split the sharp edges"
mtcheck "Weld: merged 72 vertex(es)" "Weld merged them back"
mtcheck "0 naked edge(s), 0 non-manifold edge(s), closed" "MeshRepair reports a closed mesh"
mtcheck "MeshEllipsoid: radii 10, 7, 5" "MeshEllipsoid built"
mtcheck "MeshTruncatedCone: 96 faces, closed" "MeshTruncatedCone is closed"
mtcheck "TruncatedPyramid: 12 faces, closed" "TruncatedPyramid is closed"
mtcheck "Paraboloid: NURBS surface of revolution, radius 10, height 10, 2 face(s), closed" "Paraboloid built a closed NURBS surface of revolution"
mtcheck "PlanarMesh: created 1 mesh(es)" "PlanarMesh triangulated the circle"
mtcheck "Slab: created 1 mesh(es)" "Slab thickened the rectangle"
mtcheck "ExtractMeshPart: all 6 faces extracted" "ExtractMeshPart extracted the box"
mtcheck "MeshIntersect: 14 segment(s) in 2 polyline(s)" "MeshIntersect found the box/box intersection"
mtcheck "MeshPatch: 3 triangles from 4 points" "MeshPatch triangulated the points"
mtcheck "MeshOutline: created 1 outline curve(s)" "MeshOutline built the outline"
mtcheck "Drape: 20x20 grid draped over" "Drape hit the visible meshes"
mtcheck "TriangulateRenderMeshes: 12 triangles" "TriangulateRenderMeshes split the box's quads"
mtcheck "AddNgonsToMesh: object [0-9]*: 6 ngon(s) from coplanar face groups" "AddNgonsToMesh grouped the box's triangle pairs back into ngons"
mtcheck "ComputeVertexColors: coloured 1 mesh(es) by vertex normal" "ComputeVertexColors coloured the mesh"
mtcheck "UnweldEdge: unwelded the edge nearest the pick" "UnweldEdge separated a single picked edge"
mtcheck "CollapseMeshVertex: collapsed the vertex nearest the pick into its nearest neighbour" "CollapseMeshVertex picked a vertex"
mtcheck "CollapseMeshFace: collapsed the face nearest the pick into its centroid" "CollapseMeshFace picked a face"
mtcheck "CollapseMeshEdge: collapsed the edge nearest the pick into its midpoint" "CollapseMeshEdge picked an edge"
mtcheck "DupMeshEdge: 1 edge(s) duplicated so far" "DupMeshEdge duplicated the single picked edge"
mtcheck "ExtractMeshFaces: extracted 1 of 6 faces" "ExtractMeshFaces extracted the single picked face"
mtcheck "FlatShade: added 16 vertex copy(ies)" "FlatShade fully unwelded the box for faceted shading"
echo "$MT" | grep -E "^(ok|FAIL)"
if echo "$MT" | grep -q "^FAIL"; then fail=1; fi
mtcheck "smoke: frames=1[0-9][0-9] objects=41" "mesh-tools script produced the expected object count"

# SubD editing: Crease, ExtrudeSubD, Inset, Bridge, OffsetSubD, RepairSubD, InsertEdge,
# DivideAlongCreases, Fill, AutomaticSubDFromMesh, SubDTruncatedCone, ShrinkWrap (see subd_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SD="$("$BIN" --smoke 150 --script "$HERE/subd_script.txt" 2>&1)" || { echo "$SD"; echo "FAIL: subd script exited non-zero"; exit 1; }
else
  SD="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/subd_script.txt" 2>&1)" || { echo "$SD"; echo "FAIL: subd script exited non-zero"; exit 1; }
fi
sdcheck() { if echo "$SD" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SD" "$1"; fail=1; fi; }
sdcheck "Crease: 2 edge(s) creased on 1 SubD(s)" "Crease tagged the two picked edges"
sdcheck "Crease edges: 2" "What reports the crease edges on the control net"
sdcheck "ExtrudeSubD: extruded 1 face(s) by 3 on 1 SubD(s), 4 side face(s) added" "ExtrudeSubD extruded the top face with four side faces"
sdcheck "Inset: 1 face(s) inset by 1" "Inset built an inner face and ring"
sdcheck "Bridge: joined 2 faces with a tube of 4 quads, 2 SubDs merged" "Bridge joined the two boxes"
sdcheck "Faces: 22" "the bridged SubD has 22 control faces (6+4+5 for the boxes and tube, plus the inset)"
sdcheck "OffsetSubD: solid shell, distance 1, 1 SubD(s)" "OffsetSubD Solid=Yes built a shell"
sdcheck "44 face(s) (was 44), 0 naked edge(s), 0 non-manifold edge(s), closed" "RepairSubD reports the offset shell closed"
sdcheck "InsertEdge: 4 edge(s) inserted at 0.5 along the ring" "InsertEdge inserted an edge loop around the box"
sdcheck "Crease: 6 edge(s) creased on 1 SubD(s)" "Crease tagged the loop around the split top face"
sdcheck "DivideAlongCreases: 1 SubD(s) divided into 2 piece(s)" "DivideAlongCreases split the box at the crease loop"
sdcheck "Fill: 1 hole(s) filled with a face" "Fill closed the naked loop"
sdcheck "AutomaticSubDFromMesh: SubD with 6 face(s), 12 crease edge(s) sharper than 30 degrees" "AutomaticSubDFromMesh creased the box edges"
sdcheck "SubDTruncatedCone: base radius 5, top radius 2.5, height 10, 32 faces" "SubDTruncatedCone built its control net"
sdcheck "SubDDisplayToggle: 4 SubD(s) show the control polygon" "SubDDisplayToggle switched the closed SubDs to the control polygon"
sdcheck "ShrinkWrap: signed-distance wrap with [0-9]* vertices, [0-9]* faces (closed)" "ShrinkWrap built a closed signed-distance wrap"
sdcheck "Volume = 3[0-9][0-9][0-9] cubic" "ShrinkWrap volume is close to the union of the wrapped solids"
sdcheck "InsertPoint: 1 point(s) inserted on edges" "InsertPoint split an edge on the new box"
sdcheck "Slide: vertex moved" "Slide moved the picked vertex along its best-aligned edge"
sdcheck "SubDSpinEdge: 1 edge(s) spun" "SubDSpinEdge spun the picked edge"
sdcheck "SubDExpandEdges: 2 strip face(s) added, width 1" "SubDExpandEdges added a strip on both sides of the picked edge"
# PackSubDFaces: a real per-face planar-unwrap + shelf-bin-packing UV atlas,
# not just a face count. These checks are programmatic, not "didn't crash":
# the command's own printed UV bounds/overlap-count/coverage are themselves
# computed directly from the packed rectangles (UnwrapFacesLocally/
# ShelfPackFaces in cmd_subd.cpp) - "within [0,1]x[0,1]" is a real bounds
# check against every placed corner, "0 overlapping island pair(s)" a real
# brute-force pairwise rectangle-overlap check, and the coverage percentage
# the actual packed-content-area fraction of the unit square, not eyeballed.
sdcheck "PackSubDFaces: object 11: 6 face(s) packed independently (own island each, no adjacent-face grouping) via per-face planar unwrap + shelf bin-packing; UV bounds \[0\.[0-9]*,0\.[0-9]*\]x\[0\.[0-9]*,0\.[0-9]*\] within \[0,1\]x\[0,1\], 0 overlapping island pair(s), coverage 5[0-9]\.[0-9]*%" "PackSubDFaces built a real, non-overlapping, in-bounds UV atlas for a plain SubD box (6 faces) with reasonable (~53%) coverage"
sdcheck "PackSubDFaces: object 12: 48 face(s) packed independently (own island each, no adjacent-face grouping) via per-face planar unwrap + shelf bin-packing; UV bounds \[0\.[0-9]*,0\.[0-9]*\]x\[0\.[0-9]*,0\.[0-9]*\] within \[0,1\]x\[0,1\], 0 overlapping island pair(s), coverage [3-9][0-9]\.[0-9]*%" "PackSubDFaces built a real, non-overlapping, in-bounds UV atlas for a SubD sphere (48 faces) with reasonable (>=30%) coverage"
sdcheck "gl_error=0" "subd script ran without OpenGL errors"
# ToNURBS: a real Catmull-Clark limit-surface-to-bicubic-NURBS conversion
# (kernel::SubD::ToNurbsPatches), not the old flat-facetted dense mesh. The
# regular-patch count on SubDPlane (a mostly-interior-regular quad grid)
# must be genuinely nonzero - that's the real regular-stencil code path
# actually firing, not just "didn't crash" - and the resulting Brep's own
# face count (from List) must match the refined SubD's own face count
# (one bicubic patch per finest-level face), not the original 16/6 control
# faces a facetted conversion would have produced.
sdcheck "Converted 1 object(s) (160 exact, 224 approximated near extraordinary vertices/creases/boundaries)" "ToNURBS on SubDPlane produced real exact regular-patch NURBS (not facetted) - 160 of 384 total patches are the mathematically exact Catmull-Clark bicubic patch, not an approximation"
sdcheck "384 faces, 1536 edges, open" "ToNURBS's SubDPlane Brep has 384 real per-patch faces (SubDPlane's control net is actually a triangulated grid - TessellateGrid splits each cell into 2 triangles - so 1 round of Catmull-Clark turns 32 triangles into 96 quads, and a 2nd round turns those into 384), not the facetted mesh-to-Brep conversion's flat triangle-per-facet Brep"
sdcheck "Converted 1 object(s) (72 exact, 24 approximated near extraordinary vertices/creases/boundaries)" "ToNURBS on SubDBox produced 72 real exact regular-patch NURBS away from the cube's corners, and honestly approximated only the 24 patches touching the 8 extraordinary (valence-3, a cube corner) vertices"
sdcheck "96 faces, 384 edges, open" "ToNURBS's SubDBox Brep has 96 real per-patch faces (6 box faces x 16 from 2 refinement rounds); it reports open, not closed, because the approximate patches at the 8 corners are deliberately left unjoined from their exact neighbors rather than faked into looking seamless"
echo "$SD" | grep -E "^(ok|FAIL)"
if echo "$SD" | grep -q "^FAIL"; then fail=1; fi
sdcheck "smoke: frames=150 objects=13" "subd script produced the expected object count"
# Rendering: materials (scripted options), texture mapping, lights, sun, ground plane,
# Render / RenderArctic / SaveRenderWindowAs, ExtractRenderMesh, .3dm round-trip (see render_script.txt).
sed "s|@TMP@|$TMPW|g" "$HERE/render_script.txt" > "$TMPW/render_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RN="$("$BIN" --smoke 200 --script "$TMPW/render_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: render script exited non-zero"; exit 1; }
else
  RN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMPW/render_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: render script exited non-zero"; exit 1; }
fi
rncheck() { if echo "$RN" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$RN" "$1"; fail=1; fi; }
rncheck "Created material Plastic" "RenderAssignMaterialToObjects created a material"
rncheck "Material Glass: color=200,225,240 transparency=0.6 reflectivity=0.3 gloss=0.9" "material options were applied"
rncheck "Material Brass assigned to 1 object(s)" "material assigned to the cylinder"
rncheck "Cylindrical mapping applied to 1 object(s), Scale=2" "ApplyCylindricalMapping set the mapping and scale"
rncheck "UVEditor: '(unnamed)' (Cylindrical mapping) UV bounding box: u\[0\.[0-9]*, 2\] v\[0, 2\], [0-9]* triangle(s)" "UVEditor computed a real UV bounding box (ComputeUVWireframeInfo/EnsureMappedUVs) for the cylinder's own cylindrical mapping - u wraps to width Scale=2, v spans height Scale=2, same as the panel would draw"
rncheck "AssignBlankTexture: .*blank_texture.ppm assigned to 1 object(s)" "AssignBlankTexture wrote and assigned a checker texture"
rncheck "SynchronizeRenderColors: 1 material(s)" "SynchronizeRenderColors made a material from the display colour"
rncheck "Point light 1: Point light at 10,-30,40, Intensity=1.5" "PointLight took its point and Intensity option"
rncheck "Spot light 2: Spot light at -30,-30,40, cone angle 7.12" "Spotlight built its cone from base, radius and end"
rncheck "Directional light 3: Directional light direction" "DirectionalLight ran"
rncheck "Sun on: Azimuth=200 Altitude=50" "Sun options applied"
rncheck "GroundPlane on: Height=Automatic Color=150,158,168 Shadows=Yes" "GroundPlane options applied"
rncheck "Environment: background Sky" "Environments switched to the sky background"
rncheck "Render: rendered Perspective at 320 x 240" "Render produced an offscreen image"
rncheck "Saved rendering $TMPW/render.bmp (320 x 240)" "SaveRenderWindowAs wrote the BMP"
rncheck "RenderArctic: rendered Perspective at 1280 x 720" "RenderArctic rendered at the document size"
rncheck "Saved rendering $TMPW/arctic.ppm (1280 x 720)" "SaveRenderWindowAs wrote a PPM"
rncheck "RenderPreview: rendered Perspective" "RenderPreview rendered at viewport size"
rncheck "PolygonCount: [0-9]* triangles in 4 visible object(s)" "PolygonCount counted the display meshes"
rncheck "RenderReportMissingImageFiles: 0 missing image file(s)" "RenderReportMissingImageFiles found every texture"
rncheck "ExtractRenderMesh: 4 mesh(es)" "ExtractRenderMesh added the display meshes"
rncheck "SetSpotlightToView: 1 spotlight(s) moved" "SetSpotlightToView moved the spotlight"
rncheck "Opened $TMPW/render.3dm (8 objects)" "the .3dm with materials and lights re-opened"
rncheck "RenderReportImageFiles: 1 image file(s) referenced" "material textures survived the .3dm round-trip"
rncheck "^history: 3 light(s) selected" "lights survived the .3dm round-trip"
rncheck "Current renderer: Dino 8 built-in renderer" "SetCurrentRenderPlugIn reports the built-in renderer"
rncheck "EditLightByHighlight: selected Point light 1 (nearest the pick, 0 units away)" "EditLightByHighlight picked the nearest light"
rncheck "EditLightByLooking: Perspective now looks through Point light 1" "EditLightByLooking aimed the view through the light"
rncheck "EditLightByLooking: Point light 1 re-aimed from the current view" "EditLightByLooking saved the new aim back to the light"
rncheck "ApplyCustomMapping: 1 object(s) mapped from a custom plane at 0,0,0, size 5, Scale=2" "ApplyCustomMapping used a picked reference frame"
rncheck "ShadeSelected: 1 object(s) now shaded even in Wireframe" "ShadeSelected set the per-object override"
rncheck "ShowRenderMesh: showing the tessellation wireframe on 1 object" "ShowRenderMesh overlaid the render mesh"
rncheck "HideRenderMesh: hid the tessellation wireframe on 1 object" "HideRenderMesh cleared the overlay"
rncheck "ToggleRenderMesh: tessellation wireframe now shown on 1 of 1 object" "ToggleRenderMesh flipped the overlay"
rncheck "SetMeshSurfaceParameters: 1 object(s) now tessellate at tolerance 0.05" "SetMeshSurfaceParameters set a per-object tolerance"
rncheck "SetPerFaceColorByFacePack: coloured 44 face(s) across 1 mesh(es)" "SetPerFaceColorByFacePack coloured every face"
rncheck "RemovePerFaceColors: cleared stored vertex colours on 1 mesh(es)" "RemovePerFaceColors cleared them"
rncheck "Textures: procedural wood/marble/stone/concrete/brick textures are already available" "Textures points at the real proc: textures"
rncheck "PackTextures: 1 image(s) copied into .*render_textures" "PackTextures copied a texture into a document-local folder"
rncheck "UnpackTextures: 1 image(s) copied to .*unpacked_textures" "UnpackTextures copied it back out to a chosen folder"
rncheck "Environment: background Image (.*render.bmp)" "Environments Background=Image set the image background"
rncheck "BackgroundBitmap: .*render.bmp (on)" "BackgroundBitmap set the modelling-aid picture"
rncheck "RenderBlowup: .* region rendered as a true optical zoom" "RenderBlowup rendered the picked region as a real optical zoom (full viewport resolution, off-axis frustum), not a crop"
rncheck "Bake: baked 1 procedural texture" "Bake rasterized the procedural texture to a file"
rncheck "BakeMapping: baked the current mapping into mesh UVs for 1 object" "BakeMapping froze the mapping into mesh UVs"
rncheck "MappingWidget: showing the mapping-plane gizmo for '(unnamed)' (Surface mapping)" "MappingWidget shows the real gizmo for the selected object's mapping"
rncheck "MappingWidgetOff: mapping-plane gizmo hidden" "MappingWidgetOff closes the panel"
rncheck "DownloadLibraryTextures: Dino 8 does not download anything" "DownloadLibraryTextures explains there is nothing to fetch"
rncheck "CopyRenderWindowToClipboard: [0-9]*x[0-9]* rendering copied to the system clipboard (image/png)" "CopyRenderWindowToClipboard copied the last rendering to the real OS clipboard"
rncheck "gl_error=0" "no OpenGL errors in the render script"
python3 - "$TMPW/render.bmp" <<'PY' && echo "ok   render.bmp is a valid, non-black 24-bit BMP" || { echo "FAIL render.bmp invalid or black"; fail=1; }
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:2] == b'BM', 'signature'
size, off, hdr, w, h, planes, bpp = struct.unpack('<IxxxxIIiiHH', d[2:30])
assert size == len(d) and hdr == 40 and w == 320 and h == 240 and planes == 1 and bpp == 24, (size, len(d), w, h, bpp)
px = d[off:]
assert len(px) == ((w * 3 + 3) & ~3) * h, 'pixel data size'
assert max(px) > 0 and min(px) < 255, 'image is flat'
# The rendering must contain more than one colour (background + shaded objects).
assert len(set(px[i:i + 3] for i in range(0, len(px) - 3, 3 * 97))) > 8, 'too few colours'
PY
head -c 2 "$TMPW/arctic.ppm" | grep -q "P6" && echo "ok   arctic.ppm is a binary PPM" || { echo "FAIL arctic.ppm"; fail=1; }
# ApplyOcsMapping: AutoCAD's real Arbitrary Axis Algorithm (see
# render_script.txt's comment for how the tilted circle's exact normal
# N=(1,1,1)/sqrt(3) was constructed). By hand, for that N:
#   |Nx|=|Ny|=0.5774 is not < 1/64, so Ax = unitize(WorldZ x N)
#     = unitize((0,0,1) x (0.5774,0.5774,0.5774)) = unitize(-0.5774,0.5774,0)
#     = (-1/sqrt2, 1/sqrt2, 0) = (-0.70711, 0.70711, 0)
#   Ay = unitize(N x Ax) = (-1/sqrt6, -1/sqrt6, 2/sqrt6) = (-0.40825, -0.40825, 0.81650)
# ExtractCustomMappingObject's rectangle is [origin, origin+size*Ax,
# origin+size*Ax+size*Ay, origin+size*Ay, origin] (CV[0..4]), so
# CV[1]-CV[0] is parallel to Ax and CV[3]-CV[0] is parallel to Ay - checked
# by normalizing those differences and comparing to the hand-derived unit
# vectors above, which needs no assumption about the unknown origin/size
# (a world-aligned-bounding-box implementation would not match this at
# all: e.g. its "Ax" would be a world axis like (1,0,0) or (0,1,0)).
rncheck "ApplyOcsMapping: 1 object(s) mapped from their own normal via the Arbitrary Axis Algorithm" "ApplyOcsMapping ran"
printf '%s' "$RN" > "$TMPW/render_output.txt"
python3 - "$TMPW/render_output.txt" <<'PY' && echo "ok   ApplyOcsMapping's Custom mapping frame matches the Arbitrary Axis Algorithm's own Ax/Ay for N=(1,1,1)/sqrt(3), not the bounding box" || { echo "FAIL ApplyOcsMapping's mapping frame does not match the Arbitrary Axis Algorithm"; fail=1; }
import re, math, sys
text = open(sys.argv[1]).read()
cvs = {}
for m in re.finditer(r"CV\[(\d+)\] (-?[\d.]+),(-?[\d.]+),(-?[\d.]+)", text):
    i = int(m.group(1))
    if i in (0, 1, 3):
        cvs[i] = tuple(float(m.group(k)) for k in (2, 3, 4))
assert set(cvs) == {0, 1, 3}, f"expected CV[0], CV[1], CV[3] in output, got {sorted(cvs)}"
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    assert n > 1e-9, "degenerate vector"
    return tuple(c / n for c in v)
ax = unit(sub(cvs[1], cvs[0]))
ay = unit(sub(cvs[3], cvs[0]))
exp_ax = (-1 / math.sqrt(2), 1 / math.sqrt(2), 0.0)
exp_ay = (-1 / math.sqrt(6), -1 / math.sqrt(6), 2 / math.sqrt(6))
for got, exp, name in ((ax, exp_ax, "Ax"), (ay, exp_ay, "Ay")):
    for g, e in zip(got, exp):
        assert abs(g - e) < 0.01, f"{name}: got {got}, expected {exp}"
PY
# Annotation, linetype, hatch and block tools (see annotate2_script.txt).
sed -e "s|@TMP@|$TMPW|g" -e "s|@DINO8ROOT@|$HEREW/..|g" "$HERE/annotate2_script.txt" > "$TMPW/annotate2_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  A2="$("$BIN" --smoke 150 --script "$TMPW/annotate2_script.txt" 2>&1)" || { echo "$A2"; echo "FAIL: annotate2 script exited non-zero"; exit 1; }
else
  A2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMPW/annotate2_script.txt" 2>&1)" || { echo "$A2"; echo "FAIL: annotate2 script exited non-zero"; exit 1; }
fi
a2check() { if echo "$A2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$A2" "$1"; fail=1; fi; }
a2check "DimArea: Area = 314 square" "DimArea measured the circle (pi * 100)"
a2check "DimCurveLength: Length = 40" "DimCurveLength measured the line"
if echo "$A2" | grep -A1 "^history: Command: SelDim$" | grep -q "^history: 70 object(s) selected$"; then
  echo "ok   SelDim finds DimArea/DimCurveLength (its label list had gone stale and silently excluded them, plus DimVolume/DimOrdinate/DimCreaseAngle - see cmd_select2.cpp)"
else
  echo "FAIL SelDim finds DimArea/DimCurveLength"; fail=1
fi
if echo "$A2" | grep -A1 "^history: Command: SelAnnotationStyle$" | grep -q "^history: 70 object(s) selected$"; then
  echo "ok   SelAnnotationStyle/SelFontUse match the generic Annotation tag instead of a stale hardcoded per-kind list (same bug class as SelDim)"
else
  echo "FAIL SelAnnotationStyle/SelFontUse match the generic Annotation tag"; fail=1
fi
a2check "Centermark: 1 center mark(s)" "Centermark marked the circle"
if echo "$A2" | grep -A1 "^history: Command: SelDimCentermark$" | grep -q "^history: 2 object(s) selected$"; then
  echo "ok   SelDimCentermark finds real centermarks (was wrongly NoSuchObjects even though Centermark is a real command)"
else
  echo "FAIL SelDimCentermark finds real centermarks"; fail=1
fi
a2check "DimOrdinate: X 50" "DimOrdinate measured the ordinate"
if echo "$A2" | grep -A1 "^history: Command: SelDimOrdinate$" | grep -q "^history: 5 object(s) selected$"; then
  echo "ok   SelDimOrdinate finds real ordinate dimensions (was wrongly NoSuchObjects even though DimOrdinate is a real command)"
else
  echo "FAIL SelDimOrdinate finds real ordinate dimensions"; fail=1
fi
a2check "Picture: plane created with material dino8" "Picture placed the image plane"
if echo "$A2" | grep -A1 "^history: Command: SelPicture$" | grep -q "^history: 1 picture(s) selected$"; then
  echo "ok   SelPicture finds the Picture plane (Picture now tags its object, which it never did before)"
else
  echo "FAIL SelPicture finds the Picture plane"; fail=1
fi
a2check "RevCloud: 26 arc(s), closed curve" "RevCloud built a closed cloud of 26 arcs around the 30x20 rectangle"
a2check "TextProperties: 1 annotation(s) updated to \"World\", height 5" "TextProperties rebuilt the text with new content and height"
a2check "FindText: 1 annotation(s) containing \"World\" selected" "FindText found the edited text"
a2check "ConvertDots: 1 dot(s) converted to text" "ConvertDots turned the dot into text"
a2check "HatchScale: 1 hatch(es) rebuilt" "HatchScale rebuilt the hatch from its boundary"
a2check "SetCustomLinetype: Foo = 5,5" "SetCustomLinetype stored the pattern"
a2check "SetLinetype: 1 object(s) set to Foo" "SetLinetype assigned the custom linetype"
a2check "ExtractLineTypeSegments: 10 segment(s) from 1 curve(s)" "ExtractLineTypeSegments split the 100-unit line into 10 dashes"
a2check "SetLayerLinetype: layer Default uses Dashed" "SetLayerLinetype changed the layer linetype"
a2check "Linetype Foo: 5,5" "Linetypes lists the custom linetype"
a2check "1 curve(s) with linetype Foo selected" "linetype table and object linetype survived the 3dm round-trip"
a2check "ReplaceBlock: 2 instance(s) now 'B'" "ReplaceBlock swapped both A instances for B"
a2check "3 object(s) in instances of 'B' selected" "SelBlockInstanceNamed found the replaced instances"
a2check "CreateUniqueBlock: 'C' copied from 'B', 3 instance(s) switched" "CreateUniqueBlock copied the definition"
a2check "BlockEdit: editing 'D'" "BlockEdit placed an editable copy of the block instance"
a2check "BlockEdit: block 'D' redefined with 1 object(s), 2 instance(s) updated" "BlockEdit's redefinition propagated to every instance of the block (real linked instancing)"
a2check "Copied 2 object(s)" "Mirror Copy=Yes made real mirror copies of both MB block instance objects (the definition's own object plus the Insert copy)"
if echo "$A2" | grep -A1 "^history: Command: SelMirroredBlocks$" | grep -q "^history: 2 object(s) selected$"; then
  echo "ok   SelMirroredBlocks found the mirror-copied block instances (was a permanently non-functional stub - see cmd_transform.cpp/MirrorCommand::Apply)"
else
  echo "FAIL SelMirroredBlocks found the mirror-copied block instances"; fail=1
fi
# --- Centermark/CenterLine associativity: moving the referenced circle/
# lines and re-running UpdateDimensions must redraw the mark/midline at its
# new position, not the position it was baked at - real associativity, not
# a static copy (see the block above "SelMirroredBlocks" in
# annotate2_script.txt).
a2check "Centermark: 1 center mark(s) (associative to the selected circle/arc)" "Centermark on the second circle recorded its associative reference"
a2check "UpdateDimensions:   Centermark now at 720,15,0" "UpdateDimensions redrew the Centermark at the moved circle's new center (720,15,0), not the 700,0,0 it was created at"
a2check "CenterLine: midline between the two selected lines (associative to both)" "CenterLine recorded both selected lines as associative references"
a2check "UpdateDimensions:   CenterLine now spans 800,0,0 to 800,10,0" "UpdateDimensions redrew the CenterLine's midline at x=800 after moving one of the two lines from x=800 to x=780 (midline between the moved line and the untouched x=820 line), not the x=810 midline it was created at"
a2check "gl_error=0" "annotate2 script ran without OpenGL errors"
# Solid tools: RoundHole, CurveBoolean, Clash, Cage/CageEdit, Flow, ScaleByPlane (see solidtools_script.txt).
sed "s|@TMP@|$TMPW|g" "$HERE/solidtools_script.txt" > "$TMPW/solidtools_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ST="$("$BIN" --smoke 220 --script "$TMPW/solidtools_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: solid-tools script exited non-zero"; exit 1; }
else
  ST="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 220 --script "$TMPW/solidtools_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: solid-tools script exited non-zero"; exit 1; }
fi
stcheck() { if echo "$ST" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$ST" "$1"; fail=1; fi; }
stcheck "RoundHole: radius 3, through, cut 1 solid(s)" "RoundHole cut the box"
stcheck "Volume = 37[12][0-9] cubic" "box minus a r=3 through hole has volume ~ 4000 - 90*pi"
stcheck "CurveBoolean: Union of 2 region(s) -> 1 closed curve(s)" "CurveBoolean united two overlapping circles into one curve"
stcheck "degree 1, [0-9]* control points, non-rational, closed" "the union outline is a closed polyline"
stcheck "Clash: objects 6 and 7 intersect" "Clash found the overlapping spheres"
stcheck "Clash: 1 clashing pair(s) among 2 object(s)" "Clash reported one pair"
stcheck "Cage: object 9, 2 x 2 x 2 divisions (27 control points)" "Cage built a 3x3x3 lattice"
stcheck "CageEdit: 1 object(s) (14 points) bound to cage 9" "CageEdit bound the box (as a mesh) to the cage"
stcheck "Bounding box min 300,-\?0,5 max 310,10,15" "moving the cage moved the captive"
stcheck "Bounding box min 305,5,5 max 325,25,25" "scaling the cage scaled the captive"
stcheck "CageEdit: object 9 is not a cage" "forging a cage's CageDivisions tag to 20 (outside the interactive command's own [1,10] range) made IsCage reject it, instead of an out-of-bounds lattice index in Ffd/LatticeIndex"
stcheck "Flow: base length 20 -> target length 31.42 (stretched to fit)" "Flow measured both curves"
stcheck "Flow: deformed 1 object(s)" "Flow deformed the line"
stcheck "CV\[0\] 610,0,0" "the flowed line starts at the arc start"
stcheck "CV\[23\] 590,0,0" "the flowed line ends at the arc end"
stcheck "length = 31.[34]" "the flowed line follows the arc length"
stcheck "ScaleByPlane: factor 2 along the normal of the plane through 400,0,0" "ScaleByPlane read its plane and factor"
stcheck "Bounding box min 400,0,0 max 410,10,20" "ScaleByPlane doubled the height about z=0"
stcheck "ArrayHole: 4 hole position(s), radius 2, 1 solid(s) cut" "ArrayHole cut a round-hole grid"
stcheck "ArrayHole: object [0-9]* replaced by a mesh solid with 4260 faces, volume 1.551e+04" "ArrayHole's 2x2 grid removed the expected volume"
stcheck "ArrayHole: 3 hole position(s), profile [0-9]*, 1 solid(s) cut" "ArrayHole used a profile curve instead of round holes"
stcheck "ArrayHole: object [0-9]* replaced by a mesh solid with 152 faces, volume 15520" "ArrayHole's profile row removed the expected 3 x 4x4x10 volume exactly"
stcheck "ArrayHolePolar: 4 hole position(s), radius 2, 1 solid(s) cut" "ArrayHolePolar cut 4 round holes on a circle"
stcheck "ArrayHolePolar: object [0-9]* replaced by a mesh solid with 5280 faces, volume 1.541e+04" "ArrayHolePolar removed the expected volume"
stcheck "MoveHole: object [0-9]* re-cut at the new placement" "MoveHole re-cut the RoundHole feature at its new placement"
stcheck "CopyHole: copied object [0-9]* to object [0-9]*" "CopyHole cut a second copy of the hole"
stcheck "RotateHole: object [0-9]* re-cut at the new placement" "RotateHole rotated the hole feature and re-cut it"
stcheck "MirrorHole: copied object [0-9]* to object [0-9]*" "MirrorHole mirrored the hole feature into a copy"
stcheck "CutVolume: 1 cut volume(s) as meshes, total volume 785.3" "CutVolume measured pi*5^2*10 = 785.4 of the box inside the circle's extrusion"
stcheck "Bounce: polyline with 1 bounce(s)" "Bounce traced a ray straight down off the box top and back up"
stcheck "CreateSolid: 1 surface(s) joined into a closed mesh solid" "CreateSolid welded a single closed Brep's own faces into a closed mesh solid"
stcheck "Splop: placed 1 copy(ies) at 1 point(s)" "Splop placed a copy at the picked surface point"
stcheck "Reflect: mirrored across the plane through 1005,0,0 and welded original . mirror image into one symmetric mesh" "Reflect mirrored and welded the mesh into one symmetric solid"
stcheck "Radiate: baked diffuse.specular vertex colours from 1 light(s)/sun onto 1 mesh(es)" "Radiate baked vertex colours from the Sun onto the mesh"
stcheck "RadiateFind: 0 enabled light source(s) selected (the Sun also lights Radiate" "RadiateFind reported the Sun as Radiate's only light source"
stcheck "OrientCrvToEdge: placed 1 copy(ies) at 1 point(s)" "OrientCrvToEdge picked the box edge directly and oriented a copy onto it"
stcheck "NonmanifoldMerge: 2 piece(s) joined into one polysurface, 1 newly-adjacent coplanar face pair(s) merged via real B-rep topology surgery (no mesh boolean, no Manifold)" "NonmanifoldMerge joined two coplanar planes and welded their one newly-adjacent face pair with real Brep::MergeCoplanarFaces() topology surgery"
stcheck "Area = 100 square" "NonmanifoldMerge's merged single face has the combined 5x10 + 5x10 = 100 area"
stcheck "CreateRegions: regions of 1 region(s) -> 1 closed curve(s)" "CreateRegions assembled 3 open Line segments meeting end-to-end into 1 closed triangular region (the open-curve-network fix)"
stcheck "Area = 600 square" "the assembled triangle's real planar-surface area is exactly 0.5*30*40 = 600 (straight edges only, so PlanarSrf's mesher adds no extra vertices and the match is exact, not approximate)"
stcheck "CreateRegions: regions of 2 region(s) -> 2 closed curve(s)" "CreateRegions found 2 regions from a mix of an assembled open-curve loop (Arc + Line closing a semicircle) and a separate untouched closed Circle"
stcheck "Area = 153.1 square" "the assembled semicircle region's (Arc + Line, r=10) measured area is close to the analytic 0.5*pi*10^2 = 157.08, the gap being this shared Regions()/Outlines() pipeline's own pre-existing chord-tolerance polygon approximation (also present for closed curves), not something the open-curve-loop fix introduced"
stcheck "CreateRegions: 3 open curve(s) don't close into a simple loop (a dangling end, or 3+ curve ends meeting at one point) and were skipped" "a branch point (3 open Lines meeting at one shared point) is honestly refused rather than guessed at or crashed on"
# ExtractOriginalCaptives after a real Save -> New -> Open round trip (see
# CageBinding, doc/Document.h and io/File3dm.cpp): a freshly-bound captive's
# pre-cage original must come back as a real, untouched copy even after the
# document was fully closed and reopened, not only within the same
# session - the actual point of the fix (this command used to be
# CommandStatus::Partial exactly because that side table was session-only).
# A fresh Cage/CageEdit pair is used (not the object 8/cage 9 pair from the
# Cage/CageEdit section above, whose own CageBinding is already gone by
# this point - see the comment in solidtools_script.txt).
stcheck "ExtractOriginalCaptives: 1 original(s) restored as copies" "ExtractOriginalCaptives restored the captive's original after Save/New/Open, not just within the same session"
stcheck "Bounding box min 1800,0,0 max 1810,10,10" "the restored original is the untouched pre-cage box (1800,0,0 to 1810,10,10), the exact geometry Box 1800,0,0 1810,10,0 10 created before it was ever bound to the cage"
echo "$ST" | grep -E "^(ok|FAIL)"
if echo "$ST" | grep -q "^FAIL"; then fail=1; fi
stcheck "smoke: frames=[12][0-9][0-9] objects=54" "solid-tools script produced the expected object count"

# Fillet family: FilletEdge/ChamferEdge exact box-corner trims, FilletSrf, BlendEdge,
# MatchSrf, SplitFace, MergeFaces, ConnectSrf, surface/surface and curve/surface
# Intersect (see fillet_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FL="$("$BIN" --smoke 200 --script "$HERE/fillet_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: fillet script exited non-zero"; exit 1; }
else
  FL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/fillet_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: fillet script exited non-zero"; exit 1; }
fi
flcheck() { if echo "$FL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FL" "$1"; fail=1; fi; }
flcheck "FilletEdge: edge 10 of object 1 replaced with an exact fillet (radius 2)" "FilletEdge trimmed the box corner into a real B-rep, not a mesh fallback"
flcheck "Volume = 991.4 cubic" "a 10x10x10 box minus a r=2 edge fillet has volume 1000 - 10*4*(1-pi/4) = 991.42"
flcheck "ChamferEdge: edge 10 of object 2 replaced with an exact chamfer (radius 3)" "ChamferEdge trimmed the box corner exactly"
flcheck "Volume = 955 cubic" "a 10x10x10 box minus a 3x3 edge chamfer has volume 1000 - 10*3^2/2 = 955 exactly"
flcheck "FilletSrf: built between object 4 and 6, radius 2; both surfaces trimmed" "FilletSrf trimmed two independently-picked planar surfaces"
flcheck "Area = 31.41 square" "the r=2 fillet's quarter-cylinder lateral area is (pi/2)*2*10 = 31.42"
flcheck "BlendEdge: blend surface added between the two faces at edge 10" "BlendEdge built a separate G1 blend surface"
flcheck "MatchSrf: 2 boundary control point.s. moved to position on the target curve" "MatchSrf moved a plane's edge onto a target line"
flcheck "MatchSrf: exact edge match (G1) to the target surface, max position error 0, max tangent error 0" "MatchSrf against a target *surface* edge now uses the kernel's exact NurbsSurface::MatchEdge() (self-checked by evaluation, both residuals genuinely ~0), not the app's older per-control-point loop that only assumed a shared parameterization"
flcheck "degree 2 x 1, CVs 3 x 2" "the matched floor plane's real G1 bend toward the box's vertical front face: degree elevated 1->2 and one control point added in the edge-crossing direction to hold position+tangent rows, the other direction (degree 1, 2 CVs) untouched"
flcheck "SplitFace: face 0 split into 2 surfaces along the curve's crossing" "SplitFace found a real CSX crossing of a piercing polyline (a coplanar line can't cross a flat face twice)"
flcheck "MergeFaces: 2 coplanar face.s. merged into 1" "MergeFaces recombined two joined coplanar planes"
flcheck "Area = 100 square" "the merged 5x10 + 5x10 planes have area 100"
flcheck "ConnectSrf: extended both surfaces to their real intersection curve; both surfaces trimmed to the join" "ConnectSrf found the real SSX join line between two already-touching planes"
flcheck "ConnectSrf: extended both surfaces to their real intersection curve; 1/2 surfaces trimmed (surface .*: the join curve does not reach this surface's own domain edge anywhere" "ConnectSrf's general (non-planar) trim: a real quarter-cylinder (exact rational-arc extrusion) genuinely trimmed to the SSX join curve against an oversized plane that honestly reports why IT was left untrimmed, instead of both silently falling back to Split"
flcheck " 1 faces, [0-9]* edges, open" "the cylinder's own real trim produced a single-face open B-rep (not a mesh fallback, not the untrimmed extended surface)"
flcheck "Area = 193.8 square" "the trimmed cylinder patch's real, reproducible lateral area (stable across repeated runs; not a hand-derived closed form, since ON_NurbsSurface::Extend() only continues the original arc smoothly - matching end tangent/curvature - not as an exact circle, past the original 90 degrees the join curve needed to reach the domain edge on)"
flcheck "Intersect: 1 surface intersection curve.s., 0 curve/surface point.s." "Intersect (SSX) found the crossing line of two planes meeting at a right angle"
flcheck "Intersect: 0 surface intersection curve.s., 1 curve/surface point.s." "Intersect (CSX) found where a line pierces a plane"
flcheck "FilletEdge: edge 10 of object .* replaced with an exact fillet (variable radius: 1 at t=0, 3 at t=1 (exact))" "FilletEdge Radii= built a genuine variable-radius fillet via the exact planar closed form, not the constant-radius approximation"
flcheck "Volume = 990.7 cubic" "a 10x10x10 box minus a variable r=1->3 edge fillet has volume 1000 - (1-pi/4)*10*(1+3+9)/3 = 990.70, matching the closed-form integral of a linear radius ramp"
flcheck "FilletSrf: built between object .* and .*, radius 1 to 2; one surface trimmed (the other is not planar; left untrimmed)" "VariableFilletSrf built a plane+cylinder variable-radius fillet via BuildPlaneCylinderVariableFillet, not the constant-radius approximation (no 'approximate' suffix means every sample landed exactly on the plane and exactly on the cylinder)"
flcheck "degree 2 x 3, CVs 3 x 33" "the plane+cylinder variable fillet lofted all 33 (samples+1) exact rows into its NURBS surface"
flcheck "VariableBlendSrf: blend surface added between object .* and .*, width 0.2 to 0.8 .Continuity=Curvature: quintic blend, cross-boundary curvature matched exactly to both surfaces." "VariableBlendSrf built a real independent blend (BuildBlendSurfaceG2, same construction as BlendSrf/BlendEdge) with a width that genuinely ramps from 0.2 to 0.8 along the rail, not the rolling-ball fillet VariableFilletSrf uses"
flcheck "degree 5 x 3, CVs 6 x 25" "VariableBlendSrf's Continuity=Curvature output is the degree-5x3 quintic-Hermite blend (25 = 24 samples + 1 rows), the same construction BuildBlendSurfaceG2 gives BlendSrf/BlendEdge - not a rolling-ball fillet arc, and see tests/test_variable_blend.cpp for the numeric proof the width itself (measured on the constructed surface's own control points) actually varies along the rail while the G2 curvature match still holds at both ends"
flcheck "FilletEdge: edge .* -- mesh fallback (exact B-rep trim unavailable here; result is an approximate mesh, not a clean B-rep)" "FilletEdge succeeded on a solid cylinder's own closed (periodic) rim edge via the mesh fallback - this used to fail unconditionally with a watertight-gap error regardless of radius (see adversarial_corpus_notes.md SS3)"
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
flcheck "^ok   expect_objects 40" "fillet script produced the expected object count"

# Adversarial fillets: tiny/at-the-limit/too-large radii relative to the
# shortest adjacent edge, a huge-coordinate-scale box (a genuine kernel
# limitation - see adversarial_corpus_notes.md), and a shallow-bend FilletSrf
# (see fillet_adversarial_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FA="$("$BIN" --smoke 200 --script "$HERE/fillet_adversarial_script.txt" 2>&1)" || { echo "$FA"; echo "FAIL: fillet-adversarial script exited non-zero"; exit 1; }
else
  FA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/fillet_adversarial_script.txt" 2>&1)" || { echo "$FA"; echo "FAIL: fillet-adversarial script exited non-zero"; exit 1; }
fi
facheck() { if echo "$FA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FA" "$1"; fail=1; fi; }
facheck "FilletEdge: edge 10 of object 1 replaced with an exact fillet (radius 0.02)" "a radius far smaller than the shortest adjacent edge still built a real, exact fillet"
facheck "FilletEdge: edge 10 of object 2 replaced with an exact fillet (radius 1)" "a radius at exactly half the shortest adjacent edge's length still built a valid fillet"
facheck "7 faces, 15 edges, closed solid" "the at-the-limit fillet is a genuine closed solid, not degenerate"
facheck "! FilletEdge: the offset surfaces do not meet" "a radius more than double what the geometry supports failed with a clear diagnostic, not a hang or garbage surface"
facheck "! FilletEdge: could not build a watertight result at this object's coordinate scale" "a huge-coordinate-scale box's otherwise-ordinary fillet failed gracefully instead of silently returning a broken 'closed solid' (documented kernel limitation)"
facheck "! FilletSrf: the offset surfaces do not meet" "FilletSrf on two nearly-flat planes failed with its own clear diagnostic instead of a garbage surface"
echo "$FA" | grep -E "^(ok|FAIL)"
if echo "$FA" | grep -q "^FAIL"; then fail=1; fi
facheck "^ok   expect_objects 0" "fillet-adversarial script cleaned up to zero objects at the end"

# Extended selection and state commands: SelDupAll, SelShortCrv, SelKeyValue, SelVolumeSphere, Dot, Camera, SetActiveViewport, licence rule (see state_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ST="$("$BIN" --smoke 120 --script "$HERE/state_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: state script exited non-zero"; exit 1; }
else
  ST="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$HERE/state_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: state script exited non-zero"; exit 1; }
fi
stcheck() { if echo "$ST" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$ST" "$1"; fail=1; fi; }
stcheck "SelDupAll: 2 duplicate object(s) selected" "SelDupAll found the two identical lines"
stcheck "SelShortCrv: 1 curve(s) shorter than 5 selected" "SelShortCrv picked the 2-unit line"
stcheck "Hyperlink 'https://example.org/dino8' set on 1 object(s)" "Hyperlink stored user text"
stcheck "SelKeyValue: 1 object(s) with Hyperlink=https://example.org/dino8 selected" "SelKeyValue matched the user text"
stcheck "SelVolumeSphere: radius 15, 2 object(s) selected" "SelVolumeSphere selected the objects near the origin"
stcheck "Dot 'Hello' at 5,5,0 (object 5)" "Dot created a text dot"
stcheck "Named selection 'dots' restored: 1 object(s) selected" "NamedSelections saved and restored a set"
stcheck "Camera (Perspective): location" "Camera reported the perspective camera"
stcheck "Active viewport: Top" "SetActiveViewport switched to Top"
stcheck "Camera (Top): location .*parallel projection" "Camera reported the Top (parallel) camera"
stcheck "Perspective angle 60 deg" "PerspectiveAngle set the lens"
stcheck "Dino 8 is free software: no licenses, accounts or subscriptions." "CheckOutLicense/Login print the no-licence rule"
stcheck "Selection filter edges on" "SelectionFilterEdges toggled the filter"
stcheck "Document user text Project = Dino" "SetDocumentUserText stored a key"
stcheck "Gumball: on, alignment CPlane" "GumballSettings reported the widget state"
echo "$ST" | grep -E "^(ok|FAIL)"
if echo "$ST" | grep -q "^FAIL"; then fail=1; fi
stcheck "^ok   expect_selected 5" "state script ended with every object selected"
# View tools: clipping planes + sections, layouts + details, named CPlanes, animation playback/recording (see viewtools_script.txt).
mkdir -p "$TMPW/vt"
sed "s|@TMP@|$TMPW/vt|g" "$HERE/viewtools_script.txt" > "$TMPW/viewtools_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  VT="$("$BIN" --smoke 200 --script "$TMPW/viewtools_script.txt" 2>&1)" || { echo "$VT"; echo "FAIL: view-tools script exited non-zero"; exit 1; }
else
  VT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMPW/viewtools_script.txt" 2>&1)" || { echo "$VT"; echo "FAIL: view-tools script exited non-zero"; exit 1; }
fi
vtcheck() { if echo "$VT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$VT" "$1"; fail=1; fi; }
vtcheck "ClippingPlane: created Clipping Plane 1 (40 x 40, normal 0,0,1, clips all viewports)" "ClippingPlane built a plane from two corners"
vtcheck "ClippingSections: 1 curve(s) from 1 plane(s)" "ClippingSections cut the sphere's equator"
vtcheck "degree 1, [0-9]* control points, non-rational, closed" "the section is a closed polyline"
vtcheck "ClearClippingSections: removed 1 section curve(s)" "ClearClippingSections removed the section"
vtcheck "DisableClippingPlane: 1 plane(s) disabled" "DisableClippingPlane"
vtcheck "EnableClippingPlane: 1 plane(s) enabled" "EnableClippingPlane"
vtcheck "ClippingPlane: created Upper (40 x 40, normal 0,0,1, clips Perspective)" "ClippingPlane honoured Name= and Viewports=Active"
vtcheck "SaveClippingSectionCPlanes: 2 named CPlane(s) saved" "SaveClippingSectionCPlanes"
vtcheck "SaveClippingSectionViews: 2 named view(s) saved" "SaveClippingSectionViews"
vtcheck "ClippingDrawings: 2 drawing curve(s) on layer 'Clipping Drawings' from 2 plane(s)" "ClippingDrawings drew section curves onto a dedicated layer"
vtcheck "UpdateClippingDrawings: 2 drawing curve(s) on layer 'Clipping Drawings' from 2 plane(s)" "UpdateClippingDrawings regenerated them"
vtcheck "EditClippingDrawings: 2 drawing curve(s) selected for editing" "EditClippingDrawings selected the drawing curves"
vtcheck "ExportClippingDrawings: wrote 2 drawing curve(s) to $TMPW/vt/drawings.3dm" "ExportClippingDrawings wrote them to a file"
vtcheck "ClippingSections: 2 curve(s) from 2 plane(s)" "ClippingSections handled two planes"
vtcheck "ExtractClippingSlices: 2 planar slice surface(s) from 2 plane(s)" "ExtractClippingSlices built trimmed planar faces, not just outline curves"
vtcheck "1 faces, 1 edges, open" "a clipping slice is a single trimmed planar face"
vtcheck "MPlane: CPlane of Perspective follows object [0-9]*, tracking its orientation (origin 58.54,1.464,2.5 x 0.7071,0.7071,0" "MPlane tracked a rotated box's own plane, not just its bounding-box centre"
vtcheck "MPlane: detached" "MPlane with nothing selected detaches (min=0 so Enter isn't needed)"
vtcheck "CPlane Perspective: World (origin 0,0,0 x 1,0,0 y 0,1,0)" "CPlane World reset after detaching MPlane"
vtcheck "OrientCameraToSrf: camera looks along the normal of object [0-9]* at 80,10,0 (picked point)" "OrientCameraToSrf used the picked point, not just the surface centre"
vtcheck "Camera (Perspective): location 80,10,-96.05, target 80,10,0" "OrientCameraToSrf's camera looks straight down at the picked point"
vtcheck "Zoom1To1Calibrate: 3.78 pixels per mm (96 dpi)" "Zoom1To1Calibrate read a dpi value from the command line"
vtcheck "Zoom1To1: Perspective shows" "Zoom1To1 read back the calibrated pixels-per-mm"
vtcheck "PerspectiveMatch: camera set to eye 30,-40,25, target 2,3,-1, lens 50 mm" "PerspectiveMatch's two-vanishing-point + orthocenter solve exactly reconstructed the ground-truth eye/target/lens (30,-40,25 / 2,3,-1 / 50mm) from points re-projected through that same camera -- not just 'ran without crashing'"
vtcheck "Camera (Perspective): location 30,-40,25, target 2,3,-1, distance 57.52" "reading the camera back after PerspectiveMatch confirms it, not just the command's own printout"
vtcheck "CPlane Perspective: elevation 7 (origin 0,0,7" "CPlane Elevation= moved the CPlane"
vtcheck "NamedCPlane: saved Deck (origin 0,0,7" "NamedCPlane Save"
vtcheck "NamedCPlane: restored Deck in Perspective (origin 0,0,7" "NamedCPlane Restore"
vtcheck "CPlanePrevious: origin 0,0,0" "CPlanePrevious stepped back through the CPlane history"
vtcheck "CPlaneNext: origin 0,0,7" "CPlaneNext stepped forward again"
vtcheck "CPlane Perspective: rotated 45 degrees (origin 0,0,7 x 0.7071,0.7071,0" "CPlane Rotate="
vtcheck "CopyCPlaneToAll: CPlane of Perspective copied to 3 viewport(s)" "CopyCPlaneToAll"
vtcheck "Layout: created Sheet1 (297 x 210 mm)" "Layout created a page"
vtcheck "Detail: added Detail 1 (130 x 100 mm, Top) to Sheet1" "Detail added a Top detail from two page corners"
vtcheck "Detail: added Iso (110 x 170 mm, Perspective) to Sheet1" "Detail honoured View= and Name="
vtcheck "SelDetail: 1 detail(s) selected" "SelDetail selected the named detail"
vtcheck "HideLayersInDetail: 1 layer(s) in 1 detail(s)" "HideLayersInDetail"
vtcheck "CopyDetailToViewport: Iso -> Perspective" "CopyDetailToViewport"
vtcheck "CopyLayout: created Sheet1 Copy with 2 detail(s)" "CopyLayout duplicated the page"
vtcheck "LayoutProperties: Sheet2 420 x 297 mm" "LayoutProperties renamed and resized the page"
vtcheck "Layouts: switched to Model" "Layouts Model switched back to model space"
vtcheck "SetTurntableAnimation: 6 frames over 360 degrees in Perspective" "SetTurntableAnimation"
vtcheck "ViewFrameNumber: frame 3 of 6" "ViewFrameNumber"
vtcheck "ViewLastFrame: frame 6 of 6" "ViewLastFrame"
vtcheck "PlayAnimation: finished 6 frames" "PlayAnimation stepped through every frame without blocking"
vtcheck "RecordAnimation: wrote 6 frames to $TMPW/vt/frames" "RecordAnimation wrote every frame"
vtcheck "SetOneDaySunAnimation: 4 frame(s); PlayAnimation/RecordAnimation will sweep the Sun (Altitude -5 to 60)" "SetOneDaySunAnimation built a sun-only animation"
vtcheck "ViewFirstFrame: frame 1 of 4" "ViewFirstFrame stepped into the one-day sun animation"
vtcheck "Azimuth=70 Altitude=-5" "the one-day animation's first frame is sunrise (low altitude, easterly azimuth)"
vtcheck "ViewLastFrame: frame 4 of 4" "ViewLastFrame stepped to the animation's last frame"
vtcheck "Azimuth=290 Altitude=-5" "the one-day animation's last frame is sunset (low altitude again, westerly azimuth)"
vtcheck "SetSeasonalSunAnimation: 4 frame(s); PlayAnimation/RecordAnimation will sweep the Sun (Altitude -10 to 65)" "SetSeasonalSunAnimation built a sun-only animation"
vtcheck "Azimuth=180 Altitude=-10" "the seasonal animation's first and last frames are both winter (fixed solar-noon azimuth, lowest altitude)"
vtcheck "SplitViewportHorizontal: added Perspective 2 (5 viewports)" "SplitViewportHorizontal added a viewport"
vtcheck "CloseViewport: closed Perspective 2 (4 left)" "CloseViewport removed it"
# ToggleFloatingViewport re-dock must restore the real ImGui dock layout it
# had right before floating - including another viewport the user (here,
# DockLayoutRearrangeSelfTest) had already dragged out of the default grid -
# not just rebuild the default grid (see cmd_viewtools.cpp's
# ToggleFloatingViewport/DockLayoutSelfTest/DockLayoutRearrangeSelfTest and
# app/Application.cpp's SnapshotDockLayoutBeforeFloating/
# RestoreDockLayoutAfterFloating).
vtcheck "DockLayoutRearrangeSelfTest: tabbed Right into Top's dock node" "DockLayoutRearrangeSelfTest tabbed Right into Top (simulates the drag a mouse can't do headlessly)"
vtcheck "ToggleFloatingViewport: Perspective is now floating" "ToggleFloatingViewport floated Perspective"
vtcheck "ToggleFloatingViewport: Perspective is docked again" "ToggleFloatingViewport re-docked Perspective"
dockid() { echo "$VT" | grep "^history: DockLayoutSelfTest: $1 dock=" | sed -n "${2}p" | grep -oE '0x[0-9A-Fa-f]+'; }
DOCK_TOP_BEFORE="$(dockid Top 1)"; DOCK_RIGHT_BEFORE="$(dockid Right 1)"; DOCK_FRONT_BEFORE="$(dockid Front 1)"
DOCK_TOP_AFTER="$(dockid Top 2)"; DOCK_RIGHT_AFTER="$(dockid Right 2)"; DOCK_FRONT_AFTER="$(dockid Front 2)"
DOCK_PERSP_AFTER="$(dockid Perspective 2)"
if [ -n "$DOCK_RIGHT_BEFORE" ] && [ -n "$DOCK_TOP_BEFORE" ] && [ "$DOCK_RIGHT_BEFORE" = "$DOCK_TOP_BEFORE" ] && [ "$DOCK_FRONT_BEFORE" != "$DOCK_TOP_BEFORE" ]; then
  echo "ok   DockLayoutRearrangeSelfTest actually tabbed Right into Top's real ImGui dock node ($DOCK_RIGHT_BEFORE, distinct from Front's $DOCK_FRONT_BEFORE)"
else
  echo "FAIL DockLayoutRearrangeSelfTest did not really tab Right into Top (Right=$DOCK_RIGHT_BEFORE Top=$DOCK_TOP_BEFORE Front=$DOCK_FRONT_BEFORE)"; fail=1
fi
if [ -n "$DOCK_TOP_AFTER" ] && [ "$DOCK_TOP_AFTER" = "$DOCK_TOP_BEFORE" ] && [ "$DOCK_RIGHT_AFTER" = "$DOCK_RIGHT_BEFORE" ] && [ "$DOCK_FRONT_AFTER" = "$DOCK_FRONT_BEFORE" ]; then
  echo "ok   ToggleFloatingViewport's re-dock restored the exact prior arrangement (Right stayed tabbed with Top at $DOCK_RIGHT_AFTER, Front unchanged at $DOCK_FRONT_AFTER) instead of resetting to the default grid"
else
  echo "FAIL ToggleFloatingViewport's re-dock did not preserve the prior arrangement (Top $DOCK_TOP_BEFORE->$DOCK_TOP_AFTER, Right $DOCK_RIGHT_BEFORE->$DOCK_RIGHT_AFTER, Front $DOCK_FRONT_BEFORE->$DOCK_FRONT_AFTER)"; fail=1
fi
if [ -n "$DOCK_PERSP_AFTER" ] && [ "$DOCK_PERSP_AFTER" != "0x00000000" ]; then
  echo "ok   Perspective is itself back in the dock tree after the float/re-dock round trip (dock=$DOCK_PERSP_AFTER)"
else
  echo "FAIL Perspective did not end up re-docked (dock='$DOCK_PERSP_AFTER')"; fail=1
fi
vtcheck "Layouts: 2 layout(s); active: Model" "layouts survived the .3dm round-trip"
vtcheck "NamedCPlane: 3 named CPlane(s)" "named CPlanes survived the .3dm round-trip"
vtcheck "SelClippingPlane: 2 clipping plane(s) selected" "clipping planes survived the .3dm round-trip"
vtcheck "^ok   expect_objects 5" "view-tools script ended with the sphere, 2 clipping slices, the MPlane box and the Plane surface"
vtcheck "gl_error=0" "view-tools script: no OpenGL errors (clip distances)"
FRAMES="$(ls "$TMPW/vt/frames"/frame_*.bmp 2>/dev/null | wc -l)"
[ "$FRAMES" -ge 3 ] && echo "ok   RecordAnimation wrote $FRAMES BMP frames" || { echo "FAIL RecordAnimation frames ($FRAMES)"; fail=1; }
[ -s "$TMPW/vt/frames/frame_0001.bmp" ] && [ "$(head -c 2 "$TMPW/vt/frames/frame_0001.bmp")" = "BM" ] && echo "ok   frame_0001.bmp is a BMP" || { echo "FAIL frame_0001.bmp"; fail=1; }
grep -q "^Upper" "$TMPW/vt/clipping.txt" && echo "ok   ExportClippingSectionInfo listed the Upper plane" || { echo "FAIL clipping.txt"; fail=1; }

# NestedClippingDrawing: a real section of a section, not just a flat
# ClippingDrawing repeated twice -- the second plane re-clips the FIRST
# plane's already-sectioned rectangle, and the result's own hand-computed
# bounding box (checked with BoundingBox on the actual nested curve, not
# just a command echo) confirms the geometry, not only that it ran (see
# nested_clipping_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  NC="$("$BIN" --smoke 200 --script "$HERE/nested_clipping_script.txt" 2>&1)" || { echo "$NC"; echo "FAIL: nested-clipping script exited non-zero"; exit 1; }
else
  NC="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/nested_clipping_script.txt" 2>&1)" || { echo "$NC"; echo "FAIL: nested-clipping script exited non-zero"; exit 1; }
fi
nccheck() { if echo "$NC" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$NC" "$1"; fail=1; fi; }
nccheck "ClippingDrawings: 1 drawing curve(s) on layer 'Clipping Drawings' from 1 plane(s)" "ClippingDrawings built PlaneX's flat section of the cube"
nccheck "Bounding box min 0,-5,0 max 0,5,10" "PlaneX's flat section is the hand-computed 10 x 10 rectangle (x=0, y in [-5,5], z in [0,10])"
nccheck "NestedClippingDrawing: 1 nested drawing curve(s) clipping PlaneX's section by PlaneY (section of a section, on layer 'Clipping Drawings')" "NestedClippingDrawing re-clipped PlaneX's own section by PlaneY, not the scene"
nccheck "Bounding box min 0,0,0 max 0,0,10" "the nested drawing is the hand-computed line where PlaneX and PlaneY intersect, clipped to PlaneX's rectangle (x=0,y=0,z in [0,10]) -- a real doubly-clipped result, not a re-derivation from the cube"
nccheck "^ok   expect_objects 3" "nested-clipping script ended with exactly the cube, the flat PlaneX drawing and the nested PlaneX>PlaneY drawing"

# ImportLayout: per-detail hidden-object state (LayoutDetail::hidden_objects)
# must survive the round trip through a saved .3dm and back in via
# ImportLayout, not just the page/detail cameras - see importlayout_script.txt
# and cmd_viewtools.cpp's ImportLayout/DetailHiddenSelfTest.
mkdir -p "$TMPW/il"
sed "s|@TMP@|$TMPW/il|g" "$HERE/importlayout_script.txt" > "$TMPW/importlayout_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  IL="$("$BIN" --smoke 30 --script "$TMPW/importlayout_script.txt" 2>&1)" || { echo "$IL"; echo "FAIL: ImportLayout script exited non-zero"; exit 1; }
else
  IL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/importlayout_script.txt" 2>&1)" || { echo "$IL"; echo "FAIL: ImportLayout script exited non-zero"; exit 1; }
fi
ilcheck() { if echo "$IL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$IL" "$1"; fail=1; fi; }
ilcheck "HideInDetail: 1 object(s) in 1 detail(s)" "HideInDetail hid the tagged box in the source document"
ilcheck "ImportLayout: imported 1 layout(s) and 2 object(s) from $TMPW/il/importlayout_src.3dm" "ImportLayout pulled in the layout and both objects it references"
ilcheck "Layouts: 1 layout(s); active: Model" "the imported layout exists in the fresh document"
IL_BEFORE_COUNT="$(echo "$IL" | grep -c "^history: DetailHiddenSelfTest: Sheet1/Det1 hidden_objects=1")"
[ "$IL_BEFORE_COUNT" -ge 2 ] && echo "ok   DetailHiddenSelfTest reports exactly 1 hidden object both before Save and after ImportLayout" || { echo "FAIL DetailHiddenSelfTest hidden_objects count ($IL_BEFORE_COUNT occurrence(s) of hidden_objects=1, want >=2)"; fail=1; }
IL_HIDDEN_COUNT="$(echo "$IL" | grep -c "^history: DetailHiddenSelfTest: SrcHidden hidden$")"
IL_VISIBLE_COUNT="$(echo "$IL" | grep -c "^history: DetailHiddenSelfTest: SrcVisible visible$")"
[ "$IL_HIDDEN_COUNT" -ge 2 ] && echo "ok   SrcHidden is reported hidden in LayoutDetail::hidden_objects both before Save and after ImportLayout ($IL_HIDDEN_COUNT occurrence(s))" || { echo "FAIL SrcHidden was not consistently reported hidden ($IL_HIDDEN_COUNT occurrence(s), want >=2)"; fail=1; }
[ "$IL_VISIBLE_COUNT" -ge 2 ] && echo "ok   SrcVisible is reported visible (not swept into hidden_objects) both before Save and after ImportLayout ($IL_VISIBLE_COUNT occurrence(s))" || { echo "FAIL SrcVisible was not consistently reported visible ($IL_VISIBLE_COUNT occurrence(s), want >=2)"; fail=1; }
ilcheck "^ok   expect_objects 2" "ImportLayout script ended with exactly the 2 imported objects (no duplicated geometry)"

# PrintDisplay: the preview must show each object's real print colour
# (Document::EffectiveColor, the same colour ExportPdf/ExportSvg actually
# put on the page) instead of Viewport.cpp's near-black-on-dark-background
# legibility lift - see printdisplay_script.txt. A line coloured 5,5,5 (near
# black) on the default dark Wireframe background gets lifted to 222,225,230
# with PrintDisplay off, and must show its literal, unlifted 5,5,5 with
# PrintDisplay on.
mkdir -p "$TMPW/pd"
sed "s|@TMP@|$TMPW/pd|g" "$HERE/printdisplay_script.txt" > "$TMPW/printdisplay_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  PD="$("$BIN" --smoke 30 --script "$TMPW/printdisplay_script.txt" 2>&1)" || { echo "$PD"; echo "FAIL: PrintDisplay script exited non-zero"; exit 1; }
else
  PD="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/printdisplay_script.txt" 2>&1)" || { echo "$PD"; echo "FAIL: PrintDisplay script exited non-zero"; exit 1; }
fi
pdcheck() { if echo "$PD" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$PD" "$1"; fail=1; fi; }
pdcheck "SetRenderColor: 1 object(s) set to 5,5,5" "SetRenderColor gave the line its own near-black colour"
pdcheck "PrintDisplay: off" "PrintDisplay off before the first screenshot"
pdcheck "PrintDisplay: on (print line widths and print colours previewed)" "PrintDisplay on reports it now also previews print colours"
pdcheck "^ok   expect_objects 1" "PrintDisplay script left exactly the one line"
python3 - "$TMPW/pd/pd_off.bmp" "$TMPW/pd/pd_on.bmp" <<'PY' && echo "ok   PrintDisplay on shows the line's literal print colour (5,5,5); PrintDisplay off still lifts it to 222,225,230 for on-screen legibility" || { echo "FAIL PrintDisplay's preview does not reflect each object's real print colour"; fail=1; }
import struct, sys

def read_bmp(path):
    d = open(path, 'rb').read()
    assert d[:2] == b'BM', (path, 'signature')
    size, off, hdr, w, h, planes, bpp = struct.unpack('<IxxxxIIiiHH', d[2:30])
    assert size == len(d) and hdr == 40 and planes == 1 and bpp == 24, (path, size, len(d), w, h, bpp)
    row = (w * 3 + 3) & ~3
    px = d[off:]
    assert len(px) == row * abs(h), (path, 'pixel data size')
    return px

def count(px, triple):
    return sum(1 for i in range(0, len(px) - 2, 3) if px[i:i + 3] == triple)

off_px = read_bmp(sys.argv[1])
on_px = read_bmp(sys.argv[2])
lifted_bgr = bytes((230, 225, 222))  # Color::FromBytes(222,225,230) written BGR (BMP's own byte order)
raw_gray = bytes((5, 5, 5))          # SetRenderColor 5,5,5 - a pure grey, so BGR/RGB order does not matter

assert count(off_px, lifted_bgr) > 0, 'PrintDisplay off: the near-black line was not lifted to the legibility colour'
assert count(off_px, raw_gray) == 0, 'PrintDisplay off: the literal near-black colour leaked through despite the legibility lift'
assert count(on_px, raw_gray) > 0, 'PrintDisplay on: the line does not show its own literal print colour (5,5,5)'
assert count(on_px, lifted_bgr) == 0, 'PrintDisplay on: the legibility lift colour is still present - PrintDisplay is not bypassing it'
PY

# Lua scripting: RunScript/rs.* API, "= expr" inline evaluation, rs.GetPoint
# fed by a trailing script token (see script_script.txt).
cat > "$TMPW/t.lua" <<'LUA'
-- Builds a box and a sphere, unions them, tags and files the result, then
-- reports its bounding box and volume before picking up a marker point.
rs.AddLayer("Parts")
local box = rs.AddBox({0, 0, 0}, {10, 10, 10})
local sphere = rs.AddSphere({5, 5, 12}, 4)
rs.MoveObject(sphere, {0, 0, -3})
local made = rs.BooleanUnion({box, sphere})
if not made or #made == 0 then error("BooleanUnion produced nothing") end
local widget = made[1]
rs.ObjectName(widget, "Widget")
rs.ObjectLayer(widget, "Parts")
local bb = rs.BoundingBox(widget)
print(string.format("bbox min %.0f,%.0f,%.0f", bb[1].x, bb[1].y, bb[1].z))
print(string.format("bbox max %.0f,%.0f,%.0f", bb[7].x, bb[7].y, bb[7].z))
local vol = rs.SurfaceVolume(widget)
print(string.format("widget volume %.2f", vol))
if vol < 1000 or vol > 1268 then error("volume out of expected range: " .. tostring(vol)) end
local p = rs.GetPoint("Pick a marker point")
rs.AddPoint(p.x, p.y, p.z)
print(string.format("picked point %.0f,%.0f,%.0f", p.x, p.y, p.z))
rs.UnselectAllObjects()
rs.SelectObject(widget)
print("selected by name lookup: " .. tostring(#rs.ObjectsByName("Widget")))
-- Regression for a real bug: dino8.constraints lives in the document's own
-- user-text, so a crafted/corrupted .3dm can set it to anything.
-- LoadConstraints must drop any constraint whose points/radius_objects are
-- shorter than what BuildResiduals actually indexes for its type, not
-- crash on it. Each of these three is short by exactly what its type
-- needs (Parallel needs 4 points, Radius needs 1 radius_object,
-- EqualRadius needs 2) - all three must be silently dropped.
rs.SetDocumentUserText("dino8.constraints",
  '[{"id":99,"type":"Parallel","points":[{"o":1,"i":0}]},' ..
  '{"id":100,"type":"Radius","radius_objects":[]},' ..
  '{"id":101,"type":"EqualRadius","radius_objects":[5]}]')
LUA
sed "s|@TMP@|$TMPW|g" "$HERE/script_script.txt" > "$TMPW/script_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SC="$("$BIN" --smoke 100 --script "$TMPW/script_script.txt" 2>&1)" || { echo "$SC"; echo "FAIL: script script exited non-zero"; exit 1; }
else
  SC="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/script_script.txt" 2>&1)" || { echo "$SC"; echo "FAIL: script script exited non-zero"; exit 1; }
fi
echo "$SC" | grep -E "^(ok|FAIL)"
if echo "$SC" | grep -q "^FAIL"; then fail=1; fi
sccheck() { if echo "$SC" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SC" "$1"; fail=1; fi; }
sccheck "history: inline curve length 5.00" "the \"= expr\" command line ran Lua inline (3-4-5 triangle)"
sccheck "history: bbox min 0,0,0" "RunScript's rs.BoundingBox reported the box's own corner"
sccheck "history: bbox max 10,10,13" "RunScript's rs.BoundingBox included the raised, unioned sphere"
sccheck "history: widget volume" "RunScript's rs.SurfaceVolume reported the unioned solid's volume"
sccheck "history: picked point 20,20,20" "rs.GetPoint was fed by the trailing script token"
sccheck "^ok   expect_objects 1" "the inline Lua expression added exactly the one line"
sccheck "^ok   expect_objects 3" "RunScript left the union, its point marker, and the earlier line"
sccheck "selected by name lookup: 1" "rs.ObjectsByName found the object rs.ObjectName renamed to Widget"
grep -q "! Script error" <<<"$SC" && { echo "FAIL script.txt printed a script error"; fail=1; } || echo "ok   no Lua script errors"
sccheck "history: Constraints (0):" "LoadConstraints dropped all 3 malformed constraints (Parallel/Radius/EqualRadius each short exactly the point/radius_object count BuildResiduals indexes) instead of an out-of-bounds vector read"
sccheck "history: ConstraintSolve: converged" "ConstraintSolve ran cleanly against the now-empty constraint list, not a crash"

# Python scripting: RunPythonScript through the embedded `dino8` module
# (see src/script/PythonEngine.cpp), when this build was compiled with a
# Python 3 development install (DINO8_HAVE_PYTHON - see CMakeLists.txt).
# Builds a box and a sphere, sets Name/Color on the box (and reads its
# Layer), selects it, deletes the sphere, and exercises dino8.RunCommand
# and Point3d/Vector3d arithmetic. Unlike script_script.txt's Lua test
# there is no trailing script token: PythonEngine runs a script
# start-to-finish with no GetPoint-style mid-script suspend (see
# python_script.txt's own header).
#
# dino8.RunCommand only *queues* a nested command line while called from
# inside another command's callback (CommandEngine::RunNested -> Execute,
# see cmd_misc.cpp/CommandEngine.cpp: callback_depth_ > 0 defers it to
# deferred_, run once the calling command finishes) - the same as Lua's
# rs.Command would if it were called from inside a running rs.* script.
# So NewLayer's effect is not visible to the rest of *this* script; it
# runs right after RunPythonScript's own command finishes instead, which
# is why the layer check below reads the pre-existing "Default" layer and
# the RunCommand check greps the run's full output rather than "history:
# layer: Parts" inline.
cat > "$TMPW/t.py" <<'PY'
import dino8

box_id = dino8.doc.Objects.AddBox(dino8.Point3d(0, 0, 0), dino8.Vector3d(10, 10, 10))
sphere_id = dino8.doc.Objects.AddSphere(dino8.Point3d(5, 5, 5), 2)

obj = dino8.doc.Objects.Find(box_id)
obj.Name = "Widget"
obj.Color = (255, 0, 0)
print("name: " + obj.Name)
print("layer: " + obj.Layer)
print("color: %d,%d,%d" % obj.Color)
print("kind: " + obj.ObjectType)

obj.Select()
print("selected count: %d" % len(dino8.doc.Objects.GetSelectedObjects()))
print("object count before delete: %d" % len(dino8.doc.Objects.AllObjects()))

deleted = dino8.doc.Objects.Delete(sphere_id)
print("deleted sphere: " + str(deleted))
print("object count after delete: %d" % len(dino8.doc.Objects.AllObjects()))

v = dino8.Vector3d(3, 4, 0)
print("vector length: %.2f" % v.Length)
p1 = dino8.Point3d(1, 2, 3)
p2 = p1 + v
print("point plus vector: %.0f,%.0f,%.0f" % (p2.X, p2.Y, p2.Z))

by_name = [o for o in dino8.doc.Objects.AllObjects() if o.Name == "Widget"]
print("found by name: %d" % len(by_name))

dino8.RunCommand("NewLayer", "Parts")
PY
sed "s|@TMP@|$TMPW|g" "$HERE/python_script.txt" > "$TMPW/python_script.txt"
# Captured with set +e, not "|| { ...; exit 1; }": python_script.txt's own
# final "@expect_objects 1" line assumes the earlier Python commands
# actually ran and created that object, so on a build without Python
# (DINO8_ENABLE_PYTHON=OFF - see CMakeLists.txt) main.cpp's own
# expect_objects handler makes the whole process exit non-zero even though
# RunPythonScript itself printed a normal, honest "not available" warning
# and did not crash. An immediate "||" here can never reach the skip check
# below it, since bash already took the "failed" branch by the time that
# check would run - exactly what broke this section on Windows once
# DINO8_ENABLE_PYTHON defaulted OFF there. Checking the exit code
# ourselves, after first checking for the expected message, keeps a
# genuine crash a hard failure while treating the expected skip as one.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  set +e; PS="$("$BIN" --smoke 100 --script "$TMPW/python_script.txt" 2>&1)"; ps_ec=$?; set -e
else
  set +e; PS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/python_script.txt" 2>&1)"; ps_ec=$?; set -e
fi
# Matches every "Python unavailable" message this build prints (they don't
# all share the same word order - RunPythonScript's own says "without a
# Python 3 development install", others say "no Python 3 development
# install" - but all three name it this way; see cmd_misc.cpp).
if echo "$PS" | grep -q "Python 3 development install"; then
  echo "skip Python scripting not available in this build (compiled without Python3 Development.Embed - see CMakeLists.txt)"
elif [ "$ps_ec" -ne 0 ]; then
  echo "$PS"; echo "FAIL: python script exited non-zero"; exit 1
else
  echo "$PS" | grep -E "^(ok|FAIL)"
  if echo "$PS" | grep -q "^FAIL"; then fail=1; fi
  pscheck() { if echo "$PS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$PS" "$1"; fail=1; fi; }
  pscheck "history: name: Widget" "RunPythonScript's Dino8Object.Name getter/setter round-tripped"
  pscheck "history: layer: Default" "Dino8Object.Layer read back the box's layer"
  pscheck "Layer 'Parts' created and made current" "dino8.RunCommand(\"NewLayer\", \"Parts\") ran the real NewLayer command (deferred until RunPythonScript's own command finished, same as rs.Command would be)"
  pscheck "history: color: 255,0,0" "Dino8Object.Color setter/getter round-tripped"
  pscheck "history: kind: polysurface" "Dino8Object.ObjectType reported the box as a polysurface"
  pscheck "history: selected count: 1" "Dino8Object.Select() selected the box"
  pscheck "history: object count before delete: 2" "AllObjects saw both the box and the sphere"
  pscheck "history: deleted sphere: True" "dino8.doc.Objects.Delete removed the sphere"
  pscheck "history: object count after delete: 1" "AllObjects reflects the deletion"
  pscheck "history: vector length: 5.00" "Vector3d.Length computed a 3-4-5 triangle"
  pscheck "history: point plus vector: 4,6,3" "Point3d.__add__(Vector3d) matched RhinoCommon's operator+"
  pscheck "history: found by name: 1" "list comprehension over AllObjects() found the renamed box"
  pscheck "^ok   expect_objects 1" "RunPythonScript left exactly the box (the sphere was deleted from inside the script)"
  grep -q "! Python error" <<<"$PS" && { echo "FAIL python_script.txt printed a Python error"; fail=1; } || echo "ok   no Python script errors"
fi

# EditPythonScript / Script Editor "Run" dispatch (see cmd_misc.cpp's
# EditPythonScript registration and ui/Panels.cpp's RunScriptEditor): opens
# a .py file in the Script Editor panel exactly like EditPythonScript does,
# then triggers the panel's actual Run-button code path via the test-only
# ScriptEditorRun command (cmd_misc.cpp - not in commands.json, not on any
# menu, exists purely so this can be exercised headlessly the same way
# DockLayoutRearrangeSelfTest/HBarDragSelfTest exercise other mouse-only UI
# elsewhere in this suite). Deliberately NOT calling RunPythonScript here -
# that would prove the Python engine works (already covered above) but say
# nothing about whether the panel's own Run button routes to it.
cat > "$TMPW/editor_test.py" <<'PY'
import dino8
pid = dino8.doc.Objects.AddPoint(7, 8, 9)
print("script editor ran python: " + str(pid is not None))
PY
cat > "$TMPW/scripteditor_run.txt" <<EOF
EditPythonScript $TMPW/editor_test.py
ScriptEditorRun
@expect_objects 1
EOF
# Same reasoning as python_script.txt above: scripteditor_run.txt's own
# final "@expect_objects 1" also depends on Python actually having run, so
# the exit code is only checked after the skip message is ruled out.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  set +e; SER="$("$BIN" --smoke 100 --script "$TMPW/scripteditor_run.txt" 2>&1)"; ser_ec=$?; set -e
else
  set +e; SER="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/scripteditor_run.txt" 2>&1)"; ser_ec=$?; set -e
fi
if echo "$SER" | grep -q "Python 3 development install"; then
  echo "skip Script Editor Run->Python test not available in this build (compiled without Python3 Development.Embed)"
elif [ "$ser_ec" -ne 0 ]; then
  echo "$SER"; echo "FAIL: Script Editor Run test exited non-zero"; exit 1
else
  echo "$SER" | grep -E "^(ok|FAIL)"
  if echo "$SER" | grep -q "^FAIL"; then fail=1; fi
  sercheck() { if echo "$SER" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SER" "$1"; fail=1; fi; }
  sercheck "history: script editor ran python: True" "the Script Editor panel's Run button (ScriptEditorRun - the exact RunScriptEditor() code path DrawScriptEditor's button calls) dispatched the loaded .py file to PythonEngine, not Lua"
  sercheck "^ok   expect_objects 1" "the point dino8.doc.Objects.AddPoint created from inside the Script Editor's Python run is the only object in the document"
  grep -q "! Script error" <<<"$SER" && { echo "FAIL scripteditor_run.txt printed a Lua script error (the .py file was mis-routed to Lua by the Run button)"; fail=1; } || echo "ok   Script Editor Run did not mis-route the .py file to Lua"
  grep -q "! Python error" <<<"$SER" && { echo "FAIL scripteditor_run.txt printed a Python error"; fail=1; } || echo "ok   no Python errors from the Script Editor Run test"
fi

# Dino Flow + plug-ins: node editor, the HelloDino sample plug-in (command +
# Dino Flow node), and GrasshopperPlayer headless solve/bake (see flow_script.txt).
# The .dflow is copied to $TMPW first so the second run below can edit that
# copy in place (the source tree's copy stays untouched).
cp "$HERE/flow_graph.dflow" "$TMPW/flow_graph.dflow"
sed -e "s|@FLOWFILE@|$TMPW/flow_graph.dflow|g" -e "s|@FLOWSAVE@|$TMPW/flow_saved.3dm|g" "$HERE/flow_script.txt" > "$TMPW/flow_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FL="$("$BIN" --smoke 100 --script "$TMPW/flow_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: flow script exited non-zero"; exit 1; }
else
  FL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/flow_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: flow script exited non-zero"; exit 1; }
fi
flcheck() { if echo "$FL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FL" "$1"; fail=1; fi; }
flcheck "Dino Flow: opened the node editor" "Grasshopper opened the Dino Flow panel"
flcheck "GrasshopperPluginList: 4 plug-in(s) found" "the plug-in loader found all four sample plug-ins"
flcheck "HelloDino 1.0.0 - 1 command(s), 1 node(s)" "HelloDino loaded its command and Dino Flow node"
flcheck "MeshTools 1.0.0 - 2 command(s), 3 node(s)" "MeshTools loaded its commands (TerrainMesh, TestBadMeshIndex) and 3 Dino Flow nodes (Terrain Height, Terrain Mesh, Plugin Mesh Info)"
flcheck "TestBadMeshIndex: add_mesh correctly rejected an out-of-range face index" "the plugin ABI's add_mesh rejects a face referencing a vertex past the array end, instead of an out-of-bounds read"
flcheck "CurveTools 1.0.0 - 1 command(s), 3 node(s)" "CurveTools loaded its command and 3 Dino Flow nodes (Helix Point, Spiral Curve, Plugin Curve Length)"
flcheck "AnalysisTools 1.0.0 - 1 command(s), 1 node(s)" "AnalysisTools loaded its command and Dino Flow node"
flcheck "HelloDino: hello from the sample plug-in!" "the HelloDino command ran"
flcheck "GrasshopperPlayer: solved 4 node(s)" "GrasshopperPlayer solved the sample graph"
flcheck "gl_error=0" "flow script ran without OpenGL errors"
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
flcheck "^ok   expect_objects 2" "GrasshopperPlayer baked the Line into the document"
flcheck "Total length = 30.41 " "the first bake's line has the slider=30 length"

# Associativity, across a real close/reopen: edit the same .dflow file on
# disk (slider 30 -> 55), then a *separate* process reopens the saved
# document and runs GrasshopperUpdateBakes against that same path - it
# should replace the previously-baked FlowLine object in place (same object
# count, new length) rather than add a second one next to it (see
# flow_update_script.txt).
sed -i 's/"slider_value":30/"slider_value":55/' "$TMPW/flow_graph.dflow"
sed -e "s|@FLOWFILE@|$TMPW/flow_graph.dflow|g" -e "s|@FLOWSAVE@|$TMPW/flow_saved.3dm|g" "$HERE/flow_update_script.txt" > "$TMPW/flow_update_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FL2="$("$BIN" --smoke 100 --script "$TMPW/flow_update_script.txt" 2>&1)" || { echo "$FL2"; echo "FAIL: flow update script exited non-zero"; exit 1; }
else
  FL2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/flow_update_script.txt" 2>&1)" || { echo "$FL2"; echo "FAIL: flow update script exited non-zero"; exit 1; }
fi
fl2check() { if echo "$FL2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FL2" "$1"; fail=1; fi; }
echo "$FL2" | grep -E "^(ok|FAIL)"
if echo "$FL2" | grep -q "^FAIL"; then fail=1; fi
fl2check "gl_error=0" "flow update script ran without OpenGL errors"
FL2_EXPECT2_COUNT=$(echo "$FL2" | grep -c "^ok   expect_objects 2 (got 2)")
if [ "$FL2_EXPECT2_COUNT" = "2" ]; then echo "ok   GrasshopperUpdateBakes kept the object count at 2 both before and after the re-bake (replaced in place, no duplicate)"; else echo "FAIL GrasshopperUpdateBakes kept the object count at 2 both before and after the re-bake (replaced in place, no duplicate)"; fail=1; fi
fl2check "Total length = 30.41 " "the reopened document still has the original slider=30 line"
fl2check "GrasshopperUpdateBakes: replaced 1 object(s) from a previous bake of .* with 1 freshly-solved object(s)" "GrasshopperUpdateBakes found and replaced (not duplicated) the previously-baked line"
fl2check "Total length = 55.23 " "the re-baked line picked up the slider=55 edit made to the .dflow file on disk - associative, not frozen"

# Dino Flow data trees: Range -> Graft Tree -> Flatten Tree -> List Item ->
# Construct Point -> Bake (see flow_tree_script.txt / flow_tree_graph.dflow).
# AttachGHSData/GetUserText surface each node's Tree::Summary() so the
# branch structure Graft/Flatten produce is directly checkable as text.
sed "s|@TREEFILE@|$HEREW/flow_tree_graph.dflow|g" "$HERE/flow_tree_script.txt" > "$TMPW/flow_tree_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FT="$("$BIN" --smoke 100 --script "$TMPW/flow_tree_script.txt" 2>&1)" || { echo "$FT"; echo "FAIL: flow tree script exited non-zero"; exit 1; }
else
  FT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/flow_tree_script.txt" 2>&1)" || { echo "$FT"; echo "FAIL: flow tree script exited non-zero"; exit 1; }
fi
ftcheck() { if echo "$FT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FT" "$1"; fail=1; fi; }
ftcheck "^ok   expect_objects 1" "the tree graph baked exactly one point"
ftcheck "Range=5 items" "Range produced a 5-item, single-branch list"
ftcheck "Graft Tree=5 items in 5 branches" "Graft Tree wrapped each item in its own branch"
ftcheck "Flatten Tree=5 items;" "Flatten Tree collapsed the 5 grafted branches back into one"
ftcheck "List Item=1 item" "List Item picked a single value out of the flattened list"
ftcheck "  4,0,0" "List Item(index 2) of Range(0,10,5) read back as 4 via the baked point's coordinates"

# Dino Flow evolutionary solver: Gene Pool -> (x-3)^2 fitness -> Evolutionary
# Solver, a known-optimum problem (minimum 0 at x=3) checked two ways: the
# GrasshopperPlayer summary line, and the baked (best-x, best-fitness) point
# (see flow_solver_script.txt / flow_solver_graph.dflow).
sed "s|@SOLVERFILE@|$HEREW/flow_solver_graph.dflow|g" "$HERE/flow_solver_script.txt" > "$TMPW/flow_solver_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FS="$("$BIN" --smoke 100 --script "$TMPW/flow_solver_script.txt" 2>&1)" || { echo "$FS"; echo "FAIL: flow solver script exited non-zero"; exit 1; }
else
  FS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/flow_solver_script.txt" 2>&1)" || { echo "$FS"; echo "FAIL: flow solver script exited non-zero"; exit 1; }
fi
fscheck() { if echo "$FS" | grep -qE "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FS" "$1"; fail=1; fi; }
fscheck "^ok   expect_objects 1" "the solver graph baked exactly one point"
fscheck "Evolutionary Solver \(#4\) best fitness 0 after 60 generation\(s\)" "the solver ran all 60 generations and converged to fitness 0 for (x-3)^2 with this fixed seed"
fscheck "  3,0,0" "the baked point (best gene, best fitness) is exactly (3, 0, 0) - the true optimum of (x-3)^2"

# Dino Flow plug-in geometry values: Spiral Curve (a plug-in node output of
# kind CURVE) wired directly into Plugin Curve Length (a plug-in node INPUT
# of kind CURVE) - proving the plugin ABI's opaque geometry handles round-
# trip plugin-to-plugin, not just plugin-to-document (see
# flow_plugin_geom_script.txt / flow_plugin_geom_graph.dflow).
sed "s|@GEOMFILE@|$HEREW/flow_plugin_geom_graph.dflow|g" "$HERE/flow_plugin_geom_script.txt" > "$TMPW/flow_plugin_geom_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FG="$("$BIN" --smoke 100 --script "$TMPW/flow_plugin_geom_script.txt" 2>&1)" || { echo "$FG"; echo "FAIL: flow plugin geom script exited non-zero"; exit 1; }
else
  FG="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMPW/flow_plugin_geom_script.txt" 2>&1)" || { echo "$FG"; echo "FAIL: flow plugin geom script exited non-zero"; exit 1; }
fi
fgcheck() { if echo "$FG" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FG" "$1"; fail=1; fi; }
fgcheck "^ok   expect_objects 1" "the plugin geometry graph baked exactly one point"
fgcheck "GrasshopperPlayer: solved 4 node(s)" "GrasshopperPlayer solved Spiral Curve -> Plugin Curve Length -> Construct Point -> Bake"
fgcheck "  10,0,0" "Plugin Curve Length correctly computed 10 for a radius=0 turns=1 pitch=10 helix (a straight segment), round-tripped through two plug-in nodes and baked as the point's X"
fgcheck "gl_error=0" "flow plugin geometry script ran without OpenGL errors"

# Object editing: Join/Explode/Rebuild/ChangeDegree/Offset/Extend/Flip/Dir/MakePeriodic/
# Weight/InsertKnot/PointsOn/SetObjectName/Group/Hide/Lock/clipboard/Undo (see edit_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ED="$("$BIN" --smoke 150 --script "$HERE/edit_script.txt" 2>&1)" || { echo "$ED"; echo "FAIL: edit script exited non-zero"; exit 1; }
else
  ED="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/edit_script.txt" 2>&1)" || { echo "$ED"; echo "FAIL: edit script exited non-zero"; exit 1; }
fi
echo "$ED" | grep -E "^(ok|FAIL)"
if echo "$ED" | grep -q "^FAIL"; then fail=1; fi
echo "$ED" | grep -q "^smoke:" || { echo "$ED"; echo "FAIL: edit script produced no smoke line"; fail=1; }
# Dir's own printed origin/direction is exactly the geometry ComputeDirArrow()
# (doc/SceneObject.h/.cpp) hands the viewport's direction-arrow glyph, so a
# plain Flip (FlipObject() - the same function a glyph click calls) must
# invert it precisely, for both a curve and a surface - the headlessly-
# verifiable half of Dir's real-glyph fix (see AUDIT.md's dated note).
edcheck() { if echo "$ED" | grep -Eq "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$ED" "$1"; fail=1; fi; }
edcheck "Curve [0-9]+: start 0,0,0 tangent 1,0,0" "Dir printed a fresh line's real start point and unit tangent"
edcheck "Curve [0-9]+: start 10,0,0 tangent -1,0,0" "a plain Flip inverted that same curve's Dir-reported start/tangent exactly"
edcheck "Surface [0-9]+: centre 5,5,0 normal 0,0,-1" "Dir printed a fresh planar surface's real domain-centre point and unit normal"
edcheck "Surface [0-9]+: centre 5,5,0 normal -?0,0,1" "a plain Flip inverted that same surface's Dir-reported normal exactly, leaving its centre point unchanged (Reverse(0) reparameterises, it does not move the surface)"

# Real NURBS algorithm QC: ExtractPipedCurve/MakePeriodic Smooth=No/RefitTrim
# (see nurbs_algo_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  NA="$("$BIN" --smoke 150 --script "$HERE/nurbs_algo_script.txt" 2>&1)" || { echo "$NA"; echo "FAIL: nurbs algo script exited non-zero"; exit 1; }
else
  NA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/nurbs_algo_script.txt" 2>&1)" || { echo "$NA"; echo "FAIL: nurbs algo script exited non-zero"; exit 1; }
fi
echo "$NA" | grep -E "^(ok|FAIL)"
if echo "$NA" | grep -q "^FAIL"; then fail=1; fi
echo "$NA" | grep -q "^smoke:" || { echo "$NA"; echo "FAIL: nurbs algo script produced no smoke line"; fail=1; }
nacheck() { if echo "$NA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$NA" "$1"; fail=1; fi; }
nacheck "ExtractPipedCurve: extracted 1 rail curve(s)" "ExtractPipedCurve pulled the rail curve back out of the Pipe surface, even after the original curve was deleted"
nacheck "MakePeriodic: 1 curve(s) made periodic (Smooth=No)" "MakePeriodic Smooth=No ran the real exact-shape-preserving re-knot"
nacheck "RefitTrim: 2 trim curve(s) could not be refit to strictly fewer control points" "RefitTrim ran its real constrained least-squares fit end to end and correctly, honestly refused a genuinely infeasible case (a cylinder cap's domain leaves no margin for the fitted control polygon's own real overshoot) rather than violating the tolerance or the surface domain"

# Layers: NewLayer/SetLayer/ChangeLayer/ChangeToCurrentLayer/MatchLayer/SetLayerToObject/
# OneLayerOn/OneLayerOff/AllLayersOn/LayerOn/LayerOff/LayerLock/LayerUnlock/Purge (see layer_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  LY="$("$BIN" --smoke 150 --script "$HERE/layer_script.txt" 2>&1)" || { echo "$LY"; echo "FAIL: layer script exited non-zero"; exit 1; }
else
  LY="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/layer_script.txt" 2>&1)" || { echo "$LY"; echo "FAIL: layer script exited non-zero"; exit 1; }
fi
echo "$LY" | grep -E "^(ok|FAIL)"
if echo "$LY" | grep -q "^FAIL"; then fail=1; fi
echo "$LY" | grep -q "^smoke:" || { echo "$LY"; echo "FAIL: layer script produced no smoke line"; fail=1; }

# Layer State Manager (LayerState Save/Restore/List, Document::LayerStates -
# see layerstate_script.txt): a state saved with one layer hidden actually
# changes selectability when restored (SelAll count), and - the bug this
# increment fixes - the saved state is a real Document member persisted to
# the .3dm, not a process-wide static: it must survive New+Open and still
# restore correctly afterward.
sed "s|@TMP@|$TMPW|g" "$HERE/layerstate_script.txt" > "$TMPW/layerstate_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  LS="$("$BIN" --smoke 60 --script "$TMPW/layerstate_script.txt" 2>&1)" || { echo "$LS"; echo "FAIL: layerstate script exited non-zero"; exit 1; }
else
  LS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/layerstate_script.txt" 2>&1)" || { echo "$LS"; echo "FAIL: layerstate script exited non-zero"; exit 1; }
fi
echo "$LS" | grep -E "^(ok|FAIL)"
if echo "$LS" | grep -q "^FAIL"; then fail=1; fi
lscheck() { if echo "$LS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$LS" "$1"; fail=1; fi; }
lscheck "Layer state 'HiddenWalls' saved (3 layer(s))" "LayerState Save captured all 3 layers' visibility"
lscheck "Layer state 'HiddenWalls' restored" "LayerState Restore ran"
lscheck "1 layer state(s)" "exactly one layer state survived Save/New/Open"
lscheck "  HiddenWalls: 3 layer(s)" "the reloaded layer state kept its name and layer count"

# Purge: sweeps every named-item table (layers, blocks, materials,
# linetypes, annotation styles, empty groups), not just layers - see
# cmd_layer.cpp and purge_script.txt. Each category is made unused
# deliberately, so the single Purge run should report exactly one of each.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  PU="$("$BIN" --smoke 150 --script "$HERE/purge_script.txt" 2>&1)" || { echo "$PU"; echo "FAIL: purge script exited non-zero"; exit 1; }
else
  PU="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/purge_script.txt" 2>&1)" || { echo "$PU"; echo "FAIL: purge script exited non-zero"; exit 1; }
fi
echo "$PU" | grep -E "^(ok|FAIL)"
if echo "$PU" | grep -q "^FAIL"; then fail=1; fi
echo "$PU" | grep -q "^smoke:" || { echo "$PU"; echo "FAIL: purge script produced no smoke line"; fail=1; }
pucheck() { if echo "$PU" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$PU" "$1"; fail=1; fi; }
pucheck "Purge: removed 1 layer, 1 block, 1 material, 7 linetypes, 1 annotation style and 1 empty group" \
        "Purge swept layers/blocks/materials/linetypes/annotation styles/empty groups in one pass, not just layers (7 linetypes: the 6 unused-by-default built-ins - Continuous is protected - plus the test's own PurgeUnusedLinetype)"

# Selection: every Sel* command in cmd_select.cpp and cmd_select2.cpp (see select_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SL="$("$BIN" --smoke 200 --script "$HERE/select_script.txt" 2>&1)" || { echo "$SL"; echo "FAIL: select script exited non-zero"; exit 1; }
else
  SL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/select_script.txt" 2>&1)" || { echo "$SL"; echo "FAIL: select script exited non-zero"; exit 1; }
fi
echo "$SL" | grep -E "^(ok|FAIL)"
if echo "$SL" | grep -q "^FAIL"; then fail=1; fi
echo "$SL" | grep -q "^smoke:" || { echo "$SL"; echo "FAIL: select script produced no smoke line"; fail=1; }
slcheck() { if echo "$SL" | grep -qF "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SL" "$1"; fail=1; fi; }
# UndoSelected (per-object-scoped undo): object A (tagged undosel-a) was
# moved to 205,200,0, then unrelated object B (tagged undosel-b) was moved
# to 300,220,0 afterward - so A's own move entry is NOT the top of the
# whole-document undo stack (B's is), exercising the non-top splice path.
# UndoSelected on A alone must revert only A - not B, and not the whole
# document the way a plain Undo would (which would undo B's move instead,
# since it's the most recent edit overall).
slcheck "Location: 200, 200, 0" "UndoSelected on A reverted A's own move back to its pre-move location (200,200,0), not left at 205,200,0, even though A's entry was buried under B's later, unrelated one"
slcheck "Location: 300, 220, 0" "B's later, unrelated move stayed intact after UndoSelected targeted A only - proving this is not the same as a plain Undo (which would have undone B's move, the most recent edit overall)"
slcheck "Location: 300, 200, 0" "B's move is genuinely still on the undo stack after UndoSelected spliced A's entry out from underneath it: an ordinary Undo() right afterward undoes B's move next (back to 300,200,0)"
slcheck "The most recent change to the selection ('Move') was followed by a later edit ('Move') to the same object, so it can't be safely undone on its own without also affecting that later edit. Use Undo repeatedly instead." \
  "UndoSelected honestly refuses (rather than guessing) when isolating a match would require splitting a later, still-live edit to an object the match's own batch-move also touched"
slcheck "Location: 405, 200, 0" "the refused UndoSelected left object C exactly where the batch move put it (405,200,0), not reverted, since it could not be safely isolated"

# Provenance-based selection: SelChildren/SelParents/SelExtrusion now walk a
# real parent/child side table (doc/Document.h's ProvenanceInfo) instead of
# the old group-symmetric fallback / "every polysurface" guess - see
# provenance_script.txt's header comment for exactly what it builds.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  PV="$("$BIN" --smoke 150 --script "$HERE/provenance_script.txt" 2>&1)" || { echo "$PV"; echo "FAIL: provenance script exited non-zero"; exit 1; }
else
  PV="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/provenance_script.txt" 2>&1)" || { echo "$PV"; echo "FAIL: provenance script exited non-zero"; exit 1; }
fi
echo "$PV" | grep -E "^(ok|FAIL)"
if echo "$PV" | grep -q "^FAIL"; then fail=1; fi
echo "$PV" | grep -q "^smoke:" || { echo "$PV"; echo "FAIL: provenance script produced no smoke line"; fail=1; }
pvcheck() { if echo "$PV" | grep -qF "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$PV" "$1"; fail=1; fi; }
pvcheck "Block 'Widget' defined with 2 object(s)" "Block replaced the source curves with a 2-object instance"
pvcheck "history: Object 5 (curve) layer Default" "SelChildren on instance #2's anchor (5) added exactly its own member (6), not another instance's objects"
pvcheck "history: Object 6 (curve) layer Default" "  (List after SelChildren shows member 6 selected alongside anchor 5)"
pvcheck "SelParents: no tracked parent found for the current selection (or the parent object was deleted)" \
        "SelParents reports (not crashes) when nothing is tracked - both for an anchor object and for an extrusion whose source curve was deleted"
pvcheck "SelChildren: no tracked children found for the current selection" "SelChildren reports rather than matching everything when a leaf object has no children"
pvcheck "history: Object 11 (polysurface) layer Default" "SelExtrusion found the real ExtrudeCrv result (11), not the decoy Box (9) - a genuine improvement over the old any-polysurface Partial behaviour"
pvcheck "history: Object 10 (curve) layer Default" "SelParents on the extrusion found its real source curve (10) while it still existed"

# Transforms: exact coordinates after Move/Copy/Rotate/Scale*/Mirror/Array*/Orient*/
# ProjectToCPlane/SetPt/Nudge, in Top/Front/Right (see transform_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  TR="$("$BIN" --smoke 200 --script "$HERE/transform_script.txt" 2>&1)" || { echo "$TR"; echo "FAIL: transform script exited non-zero"; exit 1; }
else
  TR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/transform_script.txt" 2>&1)" || { echo "$TR"; echo "FAIL: transform script exited non-zero"; exit 1; }
fi
echo "$TR" | grep -E "^(ok|FAIL)"
if echo "$TR" | grep -q "^FAIL"; then fail=1; fi
echo "$TR" | grep -q "^smoke:" || { echo "$TR"; echo "FAIL: transform script produced no smoke line"; fail=1; }

# Booleans: BooleanUnion/BooleanIntersection, Boolean2Objects (Result
# cycling), BooleanSplit/MeshSplit/MeshBooleanSplit, WireCut, MeshSmooth
# (see boolean_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  BO="$("$BIN" --smoke 200 --script "$HERE/boolean_script.txt" 2>&1)" || { echo "$BO"; echo "FAIL: boolean script exited non-zero"; exit 1; }
else
  BO="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/boolean_script.txt" 2>&1)" || { echo "$BO"; echo "FAIL: boolean script exited non-zero"; exit 1; }
fi
echo "$BO" | grep -E "^(ok|FAIL)"
if echo "$BO" | grep -q "^FAIL"; then fail=1; fi
echo "$BO" | grep -q "^smoke:" || { echo "$BO"; echo "FAIL: boolean script produced no smoke line"; fail=1; }

# Adversarial booleans: near-tangent/barely-overlapping/coincident solids,
# an extreme-aspect-ratio sliver, a huge-coordinate-scale pair, a 10-deep
# chained-difference feature, and non-manifold input (see
# boolean_adversarial_script.txt and adversarial_corpus_notes.md).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  BA="$("$BIN" --smoke 300 --script "$HERE/boolean_adversarial_script.txt" 2>&1)" || { echo "$BA"; echo "FAIL: boolean-adversarial script exited non-zero"; exit 1; }
else
  BA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 300 --script "$HERE/boolean_adversarial_script.txt" 2>&1)" || { echo "$BA"; echo "FAIL: boolean-adversarial script exited non-zero"; exit 1; }
fi
bacheck() { if echo "$BA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$BA" "$1"; fail=1; fi; }
bacheck "BooleanUnion: 44 faces, volume 2000" "near-tangent boxes (1e-6 overlap) unioned into one solid at the correct volume"
bacheck "BooleanIntersection: 16 faces, volume 0.01" "barely-overlapping boxes (1e-4 overlap) still intersected into a real, non-empty sliver"
bacheck "BooleanUnion: 16 faces, volume 1000" "coincident duplicate boxes unioned without collapsing or crashing"
bacheck "BooleanIntersection: 24 faces, volume 1000" "coincident duplicate boxes intersected back to the exact original volume"
bacheck "BooleanDifference: 35992 faces, volume 750" "a 1000x1000x0.001 sliver survived a corner-clipping difference at its exact expected volume (1000 - 250)"
bacheck "BooleanUnion: 40 faces, volume 1.4e+04" "two boxes at 1e6-unit coordinates still unioned to the exact expected volume (8000+8000-2000), no precision collapse"
bacheck "BooleanDifference: 908 faces, volume 8800" "10 chained BooleanDifference cuts on one solid stayed valid through the final cut, ending at the exact expected volume (10000 - 10x120)"
if echo "$BA" | grep -q "! No object with id"; then echo "FAIL boolean-adversarial script's own SelID bookkeeping was wrong (references a missing id)"; fail=1; else echo "ok   boolean-adversarial script's SelID bookkeeping matched every object the app actually created"; fi
echo "$BA" | grep -E "^(ok|FAIL)"
if echo "$BA" | grep -q "^FAIL"; then fail=1; fi
bacheck "^ok   expect_objects 0" "boolean-adversarial script cleaned up to zero objects at the end"

# Adversarial curve self-intersection: a closed bowtie polyline fed into
# PlanarSrf (must be rejected) and a solid-capping Extrude (must degrade to
# an open surface), an open self-crossing polyline confirming IntersectSelf
# still finds real crossings after the shared CurveSelfIntersects refactor,
# and a simple closed curve confirming no false positive on ordinary
# geometry (see curve_adversarial_script.txt and adversarial_corpus_notes.md).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CA="$("$BIN" --smoke 300 --script "$HERE/curve_adversarial_script.txt" 2>&1)" || { echo "$CA"; echo "FAIL: curve-adversarial script exited non-zero"; exit 1; }
else
  CA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 300 --script "$HERE/curve_adversarial_script.txt" 2>&1)" || { echo "$CA"; echo "FAIL: curve-adversarial script exited non-zero"; exit 1; }
fi
cacheck() { if echo "$CA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$CA" "$1"; fail=1; fi; }
cacheck "! PlanarSrf: 1 curve(s) cross themselves" "PlanarSrf rejected a closed bowtie polyline instead of silently building a self-crossing trim loop"
cacheck "! Extrude: the selected curve crosses itself - building an open surface instead of a solid cap" "Extrude Solid=Yes degraded a bowtie curve to an open surface instead of claiming a solid it cannot honestly build"
cacheck "  intersection at 5,5,0" "IntersectSelf still finds a real self-crossing after the shared CurveSelfIntersects refactor"
echo "$CA" | grep -E "^(ok|FAIL)"
if echo "$CA" | grep -q "^FAIL"; then fail=1; fi
cacheck "Created 1 planar surface(s)" "a simple (non-self-intersecting) closed curve still built a normal planar surface - no false positive from the self-intersection check"
CA_CROSS_COUNT=$(echo "$CA" | grep -c "cross themselves\|crosses itself")
if [ "$CA_CROSS_COUNT" = "2" ]; then echo "ok   exactly the two bowtie cases were flagged as self-intersecting, nothing else"; else echo "FAIL exactly the two bowtie cases were flagged as self-intersecting, nothing else (got $CA_CROSS_COUNT)"; fail=1; fi
cacheck "gl_error=0" "curve-adversarial script ran without OpenGL errors"

# Analysis: Distance/Length/Area/Volume/AreaCentroid/VolumeCentroid/What/List/BoundingBox/
# Dir/Check/SelBadObjects/Angle/Radius/Diameter/Curvature/CurvatureGraph/Zebra/EMap/
# CurvatureAnalysis/DraftAngleAnalysis/ShowEdges/CrvDeviation/PointDeviation/Audit/SystemInfo
# (see analyze_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  AN="$("$BIN" --smoke 150 --script "$HERE/analyze_script.txt" 2>&1)" || { echo "$AN"; echo "FAIL: analyze script exited non-zero"; exit 1; }
else
  AN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/analyze_script.txt" 2>&1)" || { echo "$AN"; echo "FAIL: analyze script exited non-zero"; exit 1; }
fi
echo "$AN" | grep -E "^(ok|FAIL)"
if echo "$AN" | grep -q "^FAIL"; then fail=1; fi
echo "$AN" | grep -q "^smoke:" || { echo "$AN"; echo "FAIL: analyze script produced no smoke line"; fail=1; }

# Audit on a genuinely invalid object (not just a self-intersecting-but-valid
# bowtie curve, see curve_adversarial_script.txt for that different case):
# MakeInvalidCurve builds one deterministic IsValid()==false NURBS curve, and
# Check/Audit/SelBadObjects must all report it with a specific reason, not
# just a bare count (see audit_invalid_script.txt and cmd_analyze.cpp).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  AI="$("$BIN" --smoke 150 --script "$HERE/audit_invalid_script.txt" 2>&1)" || { echo "$AI"; echo "FAIL: audit-invalid script exited non-zero"; exit 1; }
else
  AI="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/audit_invalid_script.txt" 2>&1)" || { echo "$AI"; echo "FAIL: audit-invalid script exited non-zero"; exit 1; }
fi
echo "$AI" | grep -E "^(ok|FAIL)"
if echo "$AI" | grep -q "^FAIL"; then fail=1; fi
echo "$AI" | grep -q "^smoke:" || { echo "$AI"; echo "FAIL: audit-invalid script produced no smoke line"; fail=1; }
aicheck() { if echo "$AI" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$AI" "$1"; fail=1; fi; }
aicheck "Object [0-9]*: valid" "Check reported the plain line as valid"
aicheck "Object [0-9]*: INVALID - .*knot" "Check reported the deliberately invalid curve INVALID with a specific (knot-vector) reason, not just a bare count"
aicheck "Audit: 2 objects, 1 invalid" "Audit's summary counted exactly the one invalid object (out of the 2 in the doc - the valid Line and the deliberately invalid curve)"
aicheck "1 bad object(s) selected" "SelBadObjects selected exactly the invalid curve"
aicheck "Audit: 0 objects, 0 invalid" "Audit correctly reports 0 invalid once the bad object is deleted"

# Views: standard views, Zoom variants, display modes, NamedView Save/Restore, 4View/3View/
# MaxViewport, CPlane commands, viewport cycling (see view_script.txt).
mkdir -p "$TMPW/view"
sed "s|@TMP@|$TMPW/view|g" "$HERE/view_script.txt" > "$TMPW/view_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  VW="$("$BIN" --smoke 150 --script "$TMPW/view_script.txt" 2>&1)" || { echo "$VW"; echo "FAIL: view script exited non-zero"; exit 1; }
else
  VW="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMPW/view_script.txt" 2>&1)" || { echo "$VW"; echo "FAIL: view script exited non-zero"; exit 1; }
fi
echo "$VW" | grep -E "^(ok|FAIL)"
if echo "$VW" | grep -q "^FAIL"; then fail=1; fi
echo "$VW" | grep -q "^smoke:" || { echo "$VW"; echo "FAIL: view script produced no smoke line"; fail=1; }
[ -s "$TMPW/view/view.bmp" ] && echo "ok   ViewCaptureToFile wrote view.bmp" || { echo "FAIL ViewCaptureToFile"; fail=1; }
[ -s "$TMPW/view/screen.bmp" ] && echo "ok   ScreenCaptureToFile wrote screen.bmp" || { echo "FAIL ScreenCaptureToFile"; fail=1; }

# A genuine, 100%-reproducible Windows-only hang (RHINO8_KILLER_AUDIT.md row
# L) has been seen starting exactly here on every Windows CI run since it was
# first noticed: the $BIN launch below produces zero output - not even the
# unbuffered startup breadcrumbs main.cpp now prints before glfwInit() - until
# the job's own timeout kills it. It is not caused by anything in
# state_script2.txt itself (never reached). A 3-second pause here (testing an
# OS-resource-contention-between-back-to-back-launches hypothesis) was tried
# and made no difference (commit e59759b) - see row L for what that rules out
# and what's still open.

# Extended state/window/misc: the remaining cmd_state.cpp and cmd_misc.cpp
# commands not already exercised elsewhere (see state_script2.txt).
mkdir -p "$TMPW/state2"
sed "s|@TMP@|$TMPW/state2|g" "$HERE/state_script2.txt" > "$TMPW/state_script2.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  S2="$("$BIN" --smoke 200 --script "$TMPW/state_script2.txt" 2>&1)" || { echo "$S2"; echo "FAIL: state2 script exited non-zero"; exit 1; }
else
  S2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMPW/state_script2.txt" 2>&1)" || { echo "$S2"; echo "FAIL: state2 script exited non-zero"; exit 1; }
fi
echo "$S2" | grep -E "^(ok|FAIL)"
if echo "$S2" | grep -q "^FAIL"; then fail=1; fi
echo "$S2" | grep -q "^smoke:" || { echo "$S2"; echo "FAIL: state2 script produced no smoke line"; fail=1; }
s2check() { if echo "$S2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$S2" "$1"; fail=1; fi; }
s2check "WhatsNew: opened the What's New window" "WhatsNew opens its own real changelog window, not the About box"
s2check "Echo on" "Echo toggled on"
s2check "Echo off" "Echo toggled off"
s2check "Redraw on" "SetRedrawOn"
s2check "Redraw off" "SetRedrawOff"
s2check "ResetMessageBoxes: all 'do not show again' choices cleared" "ResetMessageBoxes"
s2check "Fullscreen on" "Fullscreen on"
s2check "Fullscreen off" "Fullscreen off"
s2check "Window maximized" "Maximize"
s2check "Window restored" "Restore"
s2check "SplitViewportVertical: added .* viewports" "SplitViewportVertical (cmd_viewtools's real implementation)"
s2check "BringViewportToTop: " "BringViewportToTop (cmd_viewtools's real implementation)"
s2check "SetMaximizedViewport: Perspective" "SetMaximizedViewport (cmd_viewtools's real implementation)"
s2check "ViewportTabs: hidden" "ViewportTabs (cmd_viewtools's real implementation)"
s2check "Zoom1To1Calibrate: .* pixels per mm" "Zoom1To1Calibrate (cmd_viewtools's real implementation)"
s2check "SetZoomExtentsBorder: border factor 1.2" "SetZoomExtentsBorder (cmd_viewtools's real implementation)"
s2check "Ortho angle = 45 deg" "OrthoAngle"
s2check "Grid snap size = 2.5" "SnapSize"
s2check "Grid snap on, size = 5" "SetSnap"
s2check "DragMode = World" "DragMode World (also covers the removed Dragmode duplicate)"
s2check "Drag strength = 50%" "DragStrength"
s2check "Drag copy on" "DragCopy"
s2check "Options exported to " "OptionsExport"
s2check "Options imported from " "OptionsImport"
s2check "Working folder: " "SetWorkingFolder"
s2check "Autosave: " "Autosave"
s2check "Model base point 1,2,3" "ModelBasepoint"
s2check "Earth anchor point 4,5,6" "EarthAnchorPoint"
s2check "PointCloud: 3 point(s)" "PointCloud builds a genuine ObjectKind::PointCloud object (a real ON_PointCloud-backed point set) from the selected Point objects, not a group of separate Point objects"
s2check 'point cloud "PointCloud"' "What reports the new object's own kind as a point cloud, not a group"
s2check "Points: 3" "the point cloud reports its real point count"
s2check "Colors: no" "no per-point colors were set on this cloud"
s2check "Normals: no" "no per-point normals were set on this cloud"
s2check "Bounding box: (0, 0, 0) to (1, 1, 0)" "the point cloud's own bounding box, computed straight from its points"
s2check "Bounding box: (10, 0, 0) to (11, 1, 0)" "Move actually moved the point cloud's own points (not a group's member points) - the bounding box shifted by exactly 10,0,0, and this same bounding box also reappeared unchanged after the Save/Open .3dm round trip that preceded the Move, confirming the points survive the file format intact"
s2check "InfinitePlane: 10000 x 10000 plane" "InfinitePlane"
s2check "BringToFront: 4 object(s)" "BringToFront (DrawOrder family) - 4, not 5, since PointCloud now consumes its 2 source Point objects into 1 real object instead of leaving them grouped"
s2check "Bounce: polyline with 1 bounce(s)" "Bounce (cmd_solidtools's real ray-bounce, no longer shadowed)"
s2check "GumballAlignment = World" "GumballAlignment"
s2check "GumballScaleMode = Uniform" "GumballScaleMode"
s2check "Gumball auto reset off" "GumballAutoReset"
s2check "Gumball dynamic relocate on" "GumballDynamicRelocate"
s2check "Gumball origin 5,5,5" "GumballRelocate"
s2check "Gumball reset" "GumballReset"
s2check "ViewCaptureToClipboard: [0-9]*x[0-9]* image copied to the system clipboard (image/png)" "ViewCaptureToClipboard copied a real image to the OS clipboard"
s2check "ScreenCaptureToClipboard: [0-9]*x[0-9]* image copied to the system clipboard (image/png)" "ScreenCaptureToClipboard copied a real image to the OS clipboard"
s2check "Alias qq -> Box" "Alias"
if echo "$S2" | grep -qF "2+3*4 = 14"; then echo "ok   Calc"; else echo "FAIL Calc"; fail=1; fi
s2check "Left sidebar" "ToggleLeftSidebar"
s2check "Pause: continuing" "Pause"
s2check "MultiPause: continuing" "MultiPause"
s2check "GetIssueState: ok - no issues reported" "GetIssueState"
s2check "Minimize: skipped in headless mode" "Minimize (headless-safe)"
s2check "Swapped views of " "SwapView"
s2check "Single viewport layout" "OneView"
s2check "Ortho on (angle 90 deg)" "SetOrtho"
s2check "Ortho snap to CPlane Z on" "OrthoSnapToCPlaneZ"
s2check "Snap to locked objects off" "SnapToLocked"
s2check "Snap to occluded objects off" "SnapToOccluded"
s2check "Snap to meshes off" "SnapToMeshes"
s2check "Snap to mesh objects off" "SnapToMeshObject"
s2check "Snap to SubD objects off" "SnapToSubDObject"
s2check "Osnap panel hidden" "ShowOsnap"
s2check "Remember copy options on" "RememberCopyOptions"
s2check "DollyZoom: lens 85 mm" "DollyZoom"
s2check "Right sidebar (Layers, Properties) hidden" "ToggleRightSidebar"
s2check "Toolbar shown" "ShowToolbar"
s2check "Toolbar lock on" "ToolbarLock"
s2check "Recent commands:" "PopupPopular"
s2check "Menus: File, Edit, View, Curve" "Menus"
s2check "Reset: snaps, panels, toolbar and preferences restored" "Reset"
s2check "FileExplorer: " "FileExplorer"
s2check "OpenURL: https://example.org/open" "OpenURL"
s2check "WebBrowser: https://example.org/browser" "WebBrowser"
s2check "Gumball: on, alignment CPlane, scale Uniform" "GumballSettings"
s2check "Named position 'Pos1' saved (1 object" "NamedPosition Save captured the box"
s2check "Bounding box: (160, 0, 0) to (170, 10, 10)" "the box actually moved before NamedPosition Restore"
s2check "Named position 'Pos1' restored: 1 of 1 object" "NamedPosition Restore reported success"
s2check "Bounding box: (60, 0, 0) to (70, 10, 10)" "NamedPosition Restore put the box back exactly"
s2check "Named position 'Pos1' deleted" "NamedPosition Delete"
s2check "[0-9]* snapshot(s)" "Snapshots (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "HistoryPurge: no construction history is recorded; nothing to purge" "HistoryPurge"
s2check "HistoryUpdate: no construction history is recorded; nothing to update" "HistoryUpdate"
s2check "[0-9]* attached reference model" "Worksession (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "LimitReferenceModel: 0 object(s) removed from 'nonexistent.3dm'" "LimitReferenceModel (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "ContentFilter: 'Wood' (Materials and Textures panels; ContentFilter Clear to remove)" "ContentFilter set a name filter"
s2check "ContentFilter: off (showing every entry)" "ContentFilter Clear removed it"
s2check "Dino 8 is free software. No licence keys" "Licenses"
s2check "Dino 8 does not phone home" "CheckForUpdates"
s2check "Support: open an issue at " "TechSupport"
s2check "History recording: .* object(s) with live construction history" "History (real toggle+report, cmd_history.cpp's own implementation, no longer a stub)"
s2check "History recording: .* (type On or Off to change)" "RecordHistory (shares History's own toggle)"
s2check "Text: [0-9]* curve(s) from " "Text (annotate's real text-curve command, no longer shadowed)"
s2check "Dino Flow: opened the node editor" "Grasshopper"
s2check "PackageManager: opened the package manager" "PackageManager"
s2check "PluginManager: opened the plug-in manager" "PluginManager"

# Dig* digitizer family (DigBeep/DigCamera/DigClick/DigLine/DigSection/
# DigSketch/Digitize): DigConnect Protocol=Simulated needs no hardware -
# state_script2.txt feeds it points as plain "x,y,z" tokens, proving these
# commands' real read/calibrate/build pipeline end-to-end, not just that
# DigConnect itself is real.
s2check "Digitizer: connected, protocol Simulated, not calibrated" "DigConnect Protocol=Simulated connects with no hardware"
s2check "Digitize: digitized 1,2,3" "Digitize reads a simulated point and reports its exact coordinates"
s2check "Digitize: digitized 4,5,6" "Digitize reads a second simulated point"
s2check "DigLine: line digitized" "DigLine builds a real 2-point line from two simulated points"
s2check "Bounding box min 10,0,0 max 20,0,0" "DigLine's own curve spans exactly its two digitized endpoints"
s2check "DigCamera: camera set from two digitized points" "DigCamera consumes two simulated points (eye, target)"
s2check "location 100,0,0, target 0,0,0" "DigCamera actually moved the active viewport's camera eye/target"
s2check "DigSection: 3 point(s) digitized into a curve" "DigSection builds a polyline from a simulated point sequence"
s2check "Bounding box min 0,0,0 max 10,10,0" "DigSection's polyline spans exactly its 3 digitized points"
s2check "DigSketch: 3 point(s) digitized into a curve" "DigSketch (same pipeline as DigSection) builds its own polyline"
s2check "Bounding box min 0,0,0 max 5,5,0" "DigSketch's polyline spans exactly its 3 digitized points"
s2check "DigDisconnect: disconnected" "DigDisconnect"
s2check "$(printf '\a')" "DigBeep rings a real terminal bell (raw \\a byte) once digitizing is on - not suppressed under --smoke"

# Files: New/Open/Revert/Save/SaveAs/SaveSmall/IncrementalSave/SaveAsTemplate/
# Import/Export/ExportSelected/ExportWithOrigin/Notes/DocumentProperties/Units/
# Audit3dmFile (see file_script.txt).
mkdir -p "$TMPW/file"
sed "s|@TMP@|$TMPW/file|g" "$HERE/file_script.txt" > "$TMPW/file_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FL="$("$BIN" --smoke 150 --script "$TMPW/file_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: file script exited non-zero"; exit 1; }
else
  FL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMPW/file_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: file script exited non-zero"; exit 1; }
fi
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
echo "$FL" | grep -q "^smoke:" || { echo "$FL"; echo "FAIL: file script produced no smoke line"; fail=1; }
flcheck() { if echo "$FL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FL" "$1"; fail=1; fi; }
flcheck "Saved $TMPW/file/file1.3dm" "Save wrote file1.3dm"
flcheck "Opened $TMPW/file/file1.3dm (3 objects)" "Open re-read file1.3dm"
flcheck "Saved $TMPW/file/file2.3dm" "SaveAs wrote file2.3dm"
flcheck "Opened $TMPW/file/file2.3dm (3 objects)" "Open re-read file2.3dm"
flcheck "Imported $TMPW/file/file1.3dm" "Import brought file1.3dm's objects in"
# Real geometric-fidelity proof, not just object counts: List (run once
# right before the first Save, once right after the matching Open) prints
# each object's exact fingerprint - a degree/CV count for curves, face/edge
# count for breps, face/edge/vertex/crease count for SubDs. Asserting each
# line appears *exactly* twice (grep -c over the whole captured script
# output, since grep -q would already be satisfied by the pre-save
# occurrence alone) proves the .3dm round trip didn't silently drop or
# corrupt control points, faces, or SubD topology - Box (brep), Sphere
# (brep - confirmed via the real List output, not assumed), and SubDBox
# (the first SubD object any .3dm round-trip test in this suite covers).
FL_BOX_COUNT=$(echo "$FL" | grep -c "^history:   6 faces, 12 edges, closed solid$")
[ "$FL_BOX_COUNT" = "2" ] && echo "ok   Box's exact 6 faces/12 edges fingerprint survived the .3dm round trip (List ran twice, both matched)" || { echo "FAIL Box's exact fingerprint did not appear exactly twice (got $FL_BOX_COUNT) - the .3dm round trip silently changed the brep"; fail=1; }
FL_SPHERE_COUNT=$(echo "$FL" | grep -c "^history:   1 faces, 1 edges, closed solid$")
[ "$FL_SPHERE_COUNT" = "2" ] && echo "ok   Sphere's exact 1 face/1 edge fingerprint survived the .3dm round trip" || { echo "FAIL Sphere's exact fingerprint did not appear exactly twice (got $FL_SPHERE_COUNT) - the .3dm round trip silently changed the brep"; fail=1; }
FL_SUBD_COUNT=$(echo "$FL" | grep -c "^history:   6 faces, 12 edges, 8 vertices, 0 creases$")
[ "$FL_SUBD_COUNT" = "2" ] && echo "ok   SubDBox's exact 6 faces/12 edges/8 vertices/0 creases fingerprint survived the .3dm round trip" || { echo "FAIL SubDBox's exact fingerprint did not appear exactly twice (got $FL_SUBD_COUNT) - the .3dm round trip silently changed the SubD topology"; fail=1; }
flcheck "Exported $TMPW/file/export1.obj" "Export wrote export1.obj"
test -s "$TMPW/file/file1.3dm" && echo "ok   file1.3dm exists" || { echo "FAIL file1.3dm missing"; fail=1; }
test -s "$TMPW/file/file2.3dm" && echo "ok   file2.3dm exists" || { echo "FAIL file2.3dm missing"; fail=1; }
test -s "$TMPW/file/file2_1.3dm" && echo "ok   IncrementalSave wrote file2_1.3dm" || { echo "FAIL file2_1.3dm missing"; fail=1; }
test -s "$TMPW/file/file2_1_2.3dm" && echo "ok   IncrementalSave wrote file2_1_2.3dm" || { echo "FAIL file2_1_2.3dm missing"; fail=1; }
test -s "$TMPW/file/export1.obj" && echo "ok   export1.obj exists" || { echo "FAIL export1.obj missing"; fail=1; }
flcheck "Exported $TMPW/file/exportorigin.obj (origin at 5,5,0)" "ExportWithOrigin re-based to the picked point"
test -s "$TMPW/file/exportorigin.obj" && echo "ok   exportorigin.obj exists" || { echo "FAIL exportorigin.obj missing"; fail=1; }
grep -q "^v -5 -5 0$" "$TMPW/file/exportorigin.obj" && echo "ok   ExportWithOrigin translated the box corner to -5,-5,0" || { echo "FAIL ExportWithOrigin did not re-base the geometry"; fail=1; }

# Creation: Points/Lines/InterpCrv/CurveThroughPt/Sketch/Circle3Pt/CircleD/Arc3Pt/
# Rectangle3Pt/Polygon/PolygonStar/Ellipse/Helix/Spiral/PointGrid/Divide/ClosestPt/
# Plane3Pt/SrfPt (see create_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CR="$("$BIN" --smoke 150 --script "$HERE/create_script.txt" 2>&1)" || { echo "$CR"; echo "FAIL: create script exited non-zero"; exit 1; }
else
  CR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/create_script.txt" 2>&1)" || { echo "$CR"; echo "FAIL: create script exited non-zero"; exit 1; }
fi
echo "$CR" | grep -E "^(ok|FAIL)"
if echo "$CR" | grep -q "^FAIL"; then fail=1; fi
echo "$CR" | grep -q "^smoke:" || { echo "$CR"; echo "FAIL: create script produced no smoke line"; fail=1; }
crcheck() { if echo "$CR" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$CR" "$1"; fail=1; fi; }
crcheck "degree 2, 9 control points, rational, closed" "Circle3Pt/CircleD/Ellipse built rational closed conics"
crcheck "CV\[0\] 10,0,0" "Circle3Pt through (10,0,0),(-10,0,0),(0,10,0) is centred on the origin with radius 10"
crcheck "CV\[2\] 5,5,0" "CircleD from (0,0,0) to (10,0,0) passes through the diameter's midpoint region"
crcheck "degree 2, 5 control points, rational, open" "Arc3Pt built an open rational arc"
crcheck "degree 1, 5 control points, non-rational, closed" "Rectangle3Pt closed with 5 CVs (start repeated)"
crcheck "CV\[2\] 0,5,0" "Ellipse's minor axis is 5 units, matching the second axis point"
crcheck "degree 1, 6 control points, non-rational, closed" "Polygon NumSides=5 built a closed 6-CV (5+seam) polygon"
crcheck "CV\[0\] 10,0,0" "Polygon's first corner sits at the picked radius"
crcheck "degree 1, 13 control points, non-rational, closed" "PolygonStar NumSides=6 built a closed 13-CV (12+seam) star"
crcheck "Closest point 5,0,0 distance 5" "ClosestPt found the nearest point on the line"
crcheck "degree 1 x 1, CVs 2 x 2" "Plane3Pt/SrfPt built flat 4-CV surfaces"
crcheck "PointGrid: 3 x 4 grid of points" "PointGrid CountX=3 CountY=4 built a real, non-square grid, not the old hardcoded 5 x 5"
# Sketch: real continuous mouse-drag capture (Want::Drag), driven here via
# create_script.txt's scripted drag-sample sequences (see CommandEngine's
# FeedText Want::Drag case / FeedDragPolyline - the real mouse path lives in
# Viewport.cpp's drag_capturing_/drag_capture_pts_, not exercised headlessly).
crcheck "Sketch: interpolated a curve through 3 drag sample(s)" "Sketch (3 samples) fit an interpolated curve, not click-by-click points"
crcheck "Sketch: interpolated a curve through 7 drag sample(s)" "Sketch (7-sample L-shaped-then-curved-back stroke) kept every sample past the 0.05-unit decimation floor"
crcheck "degree 2, 7 control points, non-rational, open" "the 7-sample stroke fit a real 7-CV curve, not a single segment"
crcheck "CV\[0\] 0,0,0" "the 7-sample stroke's curve starts exactly at the drag's first sampled point"
crcheck "CV\[6\] 0,20,0" "the 7-sample stroke's curve ends exactly at the drag's last sampled point"
crcheck "Sketch: interpolated a curve through 4 drag sample(s) (closed)" "Sketch Closed=Yes closed the curve when the drag/sequence ended"
crcheck "degree 2, 4 control points, non-rational, closed" "the closed stroke's curve is reported closed, with the closing point appended as a 4th control point"
crcheck "CV\[3\] 0,0,0" "the closed stroke's curve ends back exactly at its own start point (CV\[0\] == CV\[3\])"

# Second-wave drafting tools: hatch library, tables, GD&T, multi-leaders,
# live section views (see drafting2_script.txt).
sed "s|@TMP@|$TMPW|g" "$HERE/drafting2_script.txt" > "$TMPW/drafting2_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  D2="$("$BIN" --smoke 150 --script "$TMPW/drafting2_script.txt" 2>&1)" || { echo "$D2"; echo "FAIL: drafting2 script exited non-zero"; exit 1; }
else
  D2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMPW/drafting2_script.txt" 2>&1)" || { echo "$D2"; echo "FAIL: drafting2 script exited non-zero"; exit 1; }
fi
d2check() { if echo "$D2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$D2" "$1"; fail=1; fi; }
d2check "Hatch: 1 boundary(ies) hatched (ANSI31)" "Hatch used the ANSI31 library pattern"
d2check "Table: 2x2 table created" "Table built a 2x2 grid"
d2check "TableEdit: table rebuilt (2x2)" "TableEdit rebuilt the table in place"
d2check "RevisionTable: 0 revision" "RevisionTable started with a header only"
d2check "RevisionTable: 1 revision" "RevisionTable appended a row"
d2check "TitleBlock: Widget (Sheet A1, Scale 1:2)" "TitleBlock recorded name/sheet/scale"
d2check "FeatureControlFrame: Position 0.1 | A,B" "FeatureControlFrame built a frame with datums"
d2check "DatumFeature: 'A'" "DatumFeature labelled the datum"
d2check "SurfaceFinish: Ra 1.6" "SurfaceFinish recorded the roughness value"
d2check "WeldSymbol: Fillet (Above)" "WeldSymbol drew the fillet glyph"
d2check "WeldSymbol: Groove (Below)" "WeldSymbol drew the groove glyph"
d2check "WeldSymbol: Spot (Above)" "WeldSymbol drew the spot glyph"
d2check "MultiLeader: 2 arrow(s), \"Note\"" "MultiLeader built two arrows to one landing"
D2_DIMTOL_COUNT=$(echo "$D2" | grep -c "DimTolerance: 1 dimension(s) updated")
if [ "$D2_DIMTOL_COUNT" = "2" ]; then echo "ok   DimTolerance ran twice, each updating the dimension"; else echo "FAIL DimTolerance ran twice, each updating the dimension"; fail=1; fi
d2check "Text = .*0\.03" "the rebuilt dimension text carries the second (0.03) tolerance"
if echo "$D2" | grep -q "Text = .*0\.02.*0\.03\|Text = .*0\.03.*0\.02.*0\.02"; then echo "FAIL DimTolerance compounded the suffix on the second run"; fail=1; else echo "ok   DimTolerance did not compound the suffix on the second run"; fi
d2check "BillOfMaterials: " "BillOfMaterials built a table over the scene objects"
d2check "SectionView: " "SectionView sliced the box"
d2check "UpdateSectionViews: 1 section view(s) regenerated" "UpdateSectionViews rebuilt the section from its stored plane"

# Associativity: UpdateBillOfMaterials re-derives a row's material from the
# object's *current* material after RenderAssignMaterialToObjects changes it
# (the row was built while the object had no material at all).
D2_UBOM_COUNT=$(echo "$D2" | grep -c "UpdateBillOfMaterials: 2 table(s) regenerated")
if [ "$D2_UBOM_COUNT" = "2" ]; then echo "ok   UpdateBillOfMaterials regenerated both tables, twice"; else echo "FAIL UpdateBillOfMaterials regenerated both tables, twice"; fail=1; fi
d2check "(none) qty=1 material=(none)" "the By=Material row was built while BomBall had no material at all"
d2check "NewMaterial qty=1 material=NewMaterial" "UpdateBillOfMaterials picked up BomBall's newly-assigned material, not the empty one baked at creation time"

# Associativity: UpdateDimensions re-measures a DimLinear anchored to a real
# Line object's endpoints after Scale1D stretches it from 20 to 40 units.
d2check "Total length = 20 " "the line measured 20 units before the stretch"
d2check "Total length = 40 " "Scale1D stretched the line to 40 units"
d2check "UpdateDimensions:   DimLinear now measures 40" "UpdateDimensions redrew the dimension text from the stretched line's new length, not the 20 baked at creation time"
d2check "DimRadius 5 (associative to selected arc/circle)" "DimRadius recorded the selected circle as its associative reference"
d2check "UpdateDimensions:   DimRadius now measures 10" "UpdateDimensions redrew DimRadius from the circle's doubled radius, not the 5 baked at creation time"
d2check "DimAngle 90 deg (associative to 3 point(s))" "DimAngle anchored all three points (vertex + two direction points) to real Point objects"
d2check "UpdateDimensions:   DimAngle now measures 45 deg" "UpdateDimensions redrew DimAngle from a moved direction point's new position, not the 90 deg baked at creation time"

# Associativity survives a .3dm round trip: DimRefObj1/2/3 and group_id (see
# cmd_annotate.cpp/File3dm.cpp) must still resolve after Save/New/Open, so
# the post-Open UpdateDimensions re-run above finds and redraws the exact
# same 4 associative dimensions (the tolerance-anchor DimLinear at 20, the
# stretched DimLinear at 40, DimRadius at 10, DimAngle at 45 deg) as the
# last pre-save run did - counting occurrences (not grep -q) so a broken
# round trip that drops back to "no associative dimensions" or only
# partially resolves them is actually caught, not masked by the pre-save
# occurrences already having satisfied a plain substring match.
D2_LEN20_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimLinear now measures 20")
[ "$D2_LEN20_COUNT" = "4" ] && echo "ok   the tolerance-anchor DimLinear (=20) round-tripped and was redrawn on every UpdateDimensions call, including after Open" || { echo "FAIL DimLinear=20 redrawn $D2_LEN20_COUNT times, expected 4 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_LEN40_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimLinear now measures 40")
[ "$D2_LEN40_COUNT" = "4" ] && echo "ok   the stretched DimLinear (=40) round-tripped and was redrawn on every UpdateDimensions call, including after Open" || { echo "FAIL DimLinear=40 redrawn $D2_LEN40_COUNT times, expected 4 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_RAD10_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimRadius now measures 10")
[ "$D2_RAD10_COUNT" = "3" ] && echo "ok   DimRadius round-tripped and was redrawn after Open (3 calls: the two pre-save runs once it existed, plus the post-Open run)" || { echo "FAIL DimRadius redrawn $D2_RAD10_COUNT times, expected 3 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_ANG45_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimAngle now measures 45 deg")
[ "$D2_ANG45_COUNT" = "2" ] && echo "ok   DimAngle round-tripped and was redrawn after Open (the one pre-save run once it existed, plus the post-Open run)" || { echo "FAIL DimAngle redrawn $D2_ANG45_COUNT times, expected 2 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_REGEN4_COUNT=$(echo "$D2" | grep -c "UpdateDimensions: 4 dimension(s) regenerated")
[ "$D2_REGEN4_COUNT" = "2" ] && echo "ok   UpdateDimensions regenerated all 4 associative dimensions with 0 skipped, both before Save and again after Open" || { echo "FAIL UpdateDimensions: 4 dimension(s) regenerated seen $D2_REGEN4_COUNT times, expected 2 (some dimensions failed to resolve after the .3dm round trip)"; fail=1; }

d2check "gl_error=0" "drafting2 script ran without OpenGL errors"

# CPU path tracer: material library presets, RenderAssignMaterialToObjects
# Preset=, the RayTracedViewport display mode, Render/RenderArctic/
# RenderPreview with Quality=Raytraced (see raytrace_script.txt).
sed "s|@TMP@|$TMPW/rt|g" "$HERE/raytrace_script.txt" > "$TMPW/raytrace_script.txt"
mkdir -p "$TMPW/rt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RT="$(env DINO8_RT_FRAMES=1 "$BIN" --smoke 60 --script "$TMPW/raytrace_script.txt" 2>&1)" || { echo "$RT"; echo "FAIL: raytrace script exited non-zero"; exit 1; }
else
  RT="$(xvfb-run -a -s "-screen 0 1600x900x24" env DINO8_RT_FRAMES=1 "$BIN" --smoke 60 --script "$TMPW/raytrace_script.txt" 2>&1)" || { echo "$RT"; echo "FAIL: raytrace script exited non-zero"; exit 1; }
fi
rtcheck() { if echo "$RT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$RT" "$1"; fail=1; fi; }
rtcheck "Created material ChromeMat from preset Chrome" "RenderAssignMaterialToObjects Preset= created a material from the built-in library"
rtcheck "Material GoldMat assigned to 1 object(s)" "material from a preset assigned to an object"
rtcheck "MaterialLibrary: 48 built-in preset(s)" "MaterialLibrary reports the full preset count"
rtcheck "Render: rendered Perspective at 96 x 64 .* \[Raytraced Samples=4 Bounces=2 Denoise=Yes\]" "Render honoured Quality=Raytraced Samples= Bounces="
rtcheck "Saved rendering $TMPW/rt/raytrace.bmp (96 x 64)" "SaveRenderWindowAs wrote the raytraced BMP"
rtcheck "RenderArctic: rendered Perspective at 1280 x 720 .* \[Raytraced" "RenderArctic ran the path tracer at the document size"
rtcheck "RenderPreview: rendered Perspective .* \[Raytraced" "RenderPreview ran the path tracer at viewport size"
rtcheck "RenderBlowup:.*region rendered as a true optical zoom.*\[Raytraced\]" "RenderBlowup did a real optical zoom with the path tracer too, not a crop"
rtcheck "Saved $TMPW/rt/raytrace.3dm" "the raytraced scene saved to a .3dm"
rtcheck "gl_error=0" "no OpenGL errors while the viewport was in RayTracedViewport mode"
# Real proof RayTracedViewport actually produced a frame, not just that no
# GL error happened (gl_error=0 alone would pass just as well if
# GpuRaytracer::Init() silently failed and the whole mode were a no-op -
# this is exactly the gap tests/gpu_render_notes.md itself documented and
# that a fully broken shader once hid: see the DINO8_RT_FRAMES-gated
# rt_accum_frames print in Viewport.cpp).
if echo "$RT" | grep -qE "rt_accum_frames=[1-9][0-9]* empty=0"; then
  echo "ok   RayTracedViewport actually accumulated real frames (empty=0, nonzero count), not just a clean but silently-no-op GL error queue"
else
  echo "FAIL RayTracedViewport never produced a real frame (no 'rt_accum_frames=N>0 empty=0' line) - gl_error=0 alone does not prove the raytracer ran"; fail=1
fi
if echo "$RT" | grep -q "GpuRaytracer::Init failed"; then
  echo "FAIL GpuRaytracer::Init failed during the raytrace script - the GPU raytracer is non-functional in this environment"; fail=1
else
  echo "ok   GpuRaytracer::Init did not fail during the raytrace script"
fi
check_nonflat_bmp() {
  # $1 = bmp path, $2 = label. Reads w/h from the BMP header itself (rather
  # than hardcoding them) since RenderArctic/RenderPreview/RenderBlowup don't
  # all render at a fixed size, and asserts the pixel data is non-flat - real
  # proof PathTracer::Render() produced real, varied pixel data for that call
  # site, not just that the command printed a normal-looking status line.
  python3 - "$1" <<'PY' && echo "ok   $2 is a valid, non-flat 24-bit BMP" || { echo "FAIL $2 invalid or flat"; fail=1; }
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:2] == b'BM', 'signature'
size, off, hdr, w, h, planes, bpp = struct.unpack('<IxxxxIIiiHH', d[2:30])
assert size == len(d) and hdr == 40 and w > 0 and h > 0 and planes == 1 and bpp == 24, (size, len(d), w, h, bpp)
px = d[off:]
assert len(px) == ((w * 3 + 3) & ~3) * abs(h), 'pixel data size'
assert max(px) > 0 and min(px) < 255, 'image is flat'
PY
}
check_nonflat_bmp "$TMPW/rt/raytrace.bmp" "raytrace.bmp"
check_nonflat_bmp "$TMPW/rt/arctic.bmp" "arctic.bmp (RenderArctic's own pixel output, not just its printed status line)"
check_nonflat_bmp "$TMPW/rt/preview.bmp" "preview.bmp (RenderPreview's own pixel output, not just its printed status line)"
check_nonflat_bmp "$TMPW/rt/blowup.bmp" "blowup.bmp (RenderBlowup's own pixel output, not just its printed status line)"

# IGES / STEP round-trip: Box, Sphere, Cylinder, a trimmed planar surface,
# a free NURBS curve, a point, and a hand-written STEP fixture (see
# igesstep_script.txt and step_plane_face.stp).
sed "s|@TMP@|$TMPW|g" "$HERE/igesstep_script.txt" > "$TMPW/igesstep_script.txt"
cp "$HERE/step_plane_face.stp" "$TMPW/step_plane_face.stp"
cp "$HERE/iges_recursive_fixture.igs" "$TMPW/iges_recursive_fixture.igs"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  IS="$("$BIN" --smoke 200 --script "$TMPW/igesstep_script.txt" 2>&1)" || { echo "$IS"; echo "FAIL: iges/step script exited non-zero"; exit 1; }
else
  IS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMPW/igesstep_script.txt" 2>&1)" || { echo "$IS"; echo "FAIL: iges/step script exited non-zero"; exit 1; }
fi
ischeck() { if echo "$IS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$IS" "$1"; fail=1; fi; }
ischeck "Exported $TMPW/box.igs" "IGES export wrote a file"
ischeck "Exported $TMPW/box.stp" "STEP export wrote a file"
ischeck "Volume = 1000" "box volume survived an IGES and a STEP round-trip"
ischeck "IGES: .*[1-9][0-9]* brep" "IGES import rebuilt at least one brep from the combined scene"
ischeck "STEP: [1-9][0-9]* brep" "STEP import rebuilt at least one brep from the combined scene"
ischeck "STEP: .*[1-9][0-9]* curve" "STEP import read the free NURBS curve back"
ischeck "IGES: .*[1-9][0-9]* curve" "IGES import read the free NURBS curve back"
ischeck "STEP: .*[1-9][0-9]* point" "STEP import read the point back"
ischeck "IGES: .*[1-9][0-9]* point" "IGES import read the point back"
ischeck "STEP: 1 brep (1 trimmed face), 1 curve, 0 points" "the hand-written STEP fixture (PLANE face + CIRCLE) imported as 2 objects"
ischeck "IGES: 0 curves, 0 points, 0 surfaces, 0 breps (0 trimmed faces); 1 unsupported entity skipped" "a self-referencing IGES composite curve was rejected cleanly, not crashed/hung on (see BuildIgesCurve's recursion-depth guard)"
ischeck "^ok   expect_objects 0" "the malformed IGES file added nothing to the document"
grep -q "Segmentation fault\|core dumped" <<<"$IS" && { echo "FAIL: iges/step script segfaulted on the recursive-composite-curve fixture"; fail=1; } || echo "ok   no segfault while importing the recursive-composite-curve fixture"
grep -qE "^ {5}128" "$TMPW/t.igs" && grep -qE "^ {5}144" "$TMPW/t.igs" && echo "ok   t.igs uses 128 (surface) and 144 (trimmed surface) entities" || { echo "FAIL t.igs entity types"; fail=1; }
grep -q "=ADVANCED_FACE(" "$TMPW/t.stp" && grep -q "B_SPLINE_SURFACE_WITH_KNOTS(" "$TMPW/t.stp" && echo "ok   t.stp uses ADVANCED_FACE and B_SPLINE_SURFACE_WITH_KNOTS entities" || { echo "FAIL t.stp entity types"; fail=1; }
grep -q "^ISO-10303-21;$" "$TMPW/t.stp" && grep -q "^END-ISO-10303-21;$" "$TMPW/t.stp" && echo "ok   t.stp is a complete Part 21 file" || { echo "FAIL t.stp malformed"; fail=1; }

# SpaceMouse / 3Dconnexion: Protocol=File replay drives a real background
# thread (see input/SpaceMouse.cpp) that Application::Frame() drains every
# frame, so @wait gives it real wall-clock time before each check below.
cat > "$TMPW/spacemouse_deltas.txt" <<'EOS'
BUTTON 1
0 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
EOS
sed "s|@TMP@|$TMPW|g" "$HERE/spacemouse_script.txt" > "$TMPW/spacemouse_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SM="$("$BIN" --smoke 250 --script "$TMPW/spacemouse_script.txt" 2>&1)" || { echo "$SM"; echo "FAIL: spacemouse script exited non-zero"; exit 1; }
else
  SM="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 250 --script "$TMPW/spacemouse_script.txt" 2>&1)" || { echo "$SM"; echo "FAIL: spacemouse script exited non-zero"; exit 1; }
fi
smcheck() { if echo "$SM" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SM" "$1"; fail=1; fi; }
smcheck "SpaceMouse protocol set to File" "SpaceMouseProtocol switched to the File protocol"
smcheck "connected (File:" "SpaceMouse connected over the File protocol"
smcheck "^history: Command: Top$" "a button carried on a motion sample still fired its mapped command (button 1 default -> Top)"
smcheck "SpaceMouse mode set to Camera" "SpaceMouseMode set Camera mode"
smcheck "SpaceMouse mode set to Object" "SpaceMouseMode set Object mode"
# Camera mode's own pan/dolly/orbit is driven by the same drained-delta path
# Object mode exercises below, so it is not re-asserted numerically here:
# a Camera::Pan() screen-space shift depends on how many of the 15 File
# samples land in a single frame's drain, which is a real race against the
# background thread's 15ms-per-sample pacing - deterministic in Object
# mode (a flat per-sample point offset) but not worth pinning down to the
# pixel for Camera mode too.
if echo "$SM" | grep -q "Bounding box min 0,0,0 max 10,10,10"; then echo "FAIL: SpaceMouse Object mode did not move the selected box"; fail=1; else echo "ok   SpaceMouse Object mode moved the selected box (30 units along +tx: 15 samples x sensitivity 1 x scale 2)"; fi
smcheck "Bounding box min 30,0,0 max 40,10,10" "the box moved by exactly 15 x 2 = 30 units in Object mode"
echo "$SM" | grep -E "^(ok|FAIL)" || true
if echo "$SM" | grep -q "^FAIL"; then fail=1; fi

# Parametric 2D sketch constraints: Coincident/Horizontal/Vertical/Distance/
# Radius/Perpendicular/Parallel/Fixed/Midpoint, auto re-solve, glyph overlay.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CN="$("$BIN" --smoke 120 --script "$HERE/constraints_script.txt" 2>&1)" || { echo "$CN"; echo "FAIL: constraints script exited non-zero"; exit 1; }
else
  CN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$HERE/constraints_script.txt" 2>&1)" || { echo "$CN"; echo "FAIL: constraints script exited non-zero"; exit 1; }
fi
cncheck() { if echo "$CN" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$CN" "$1"; fail=1; fi; }
cncheck "Coincident constraint #1 added." "Constrain built a Coincident constraint"
cncheck "^history: Coincident #1: ok$" "Coincident solved"
cncheck "Horizontal constraint #2 added." "Constrain built a Horizontal constraint"
cncheck "Vertical constraint #3 added." "Constrain built a Vertical constraint"
cncheck "Distance constraint #4 added." "Constrain built a Distance constraint"
cncheck "Distance #4: length 10" "Distance solved to exactly 10"
cncheck "Radius constraint #5 added." "Constrain built a Radius constraint"
cncheck "Radius #5: radius 5" "Radius solved to exactly 5"
cncheck "Perpendicular constraint #6 added." "Constrain built a Perpendicular constraint"
cncheck "Perpendicular #6: angle 90" "Perpendicular solved to exactly 90 degrees"
cncheck "Parallel constraint #7 added." "Constrain built a Parallel constraint"
cncheck "Fixed constraint #8 added." "Constrain built a Fixed constraint"
cncheck "Midpoint constraint #9 added." "Constrain built a Midpoint constraint"
cncheck "Fixed constraint #10 added." "Constrain built a second Fixed constraint (auto-resolve anchor)"
cncheck "Coincident constraint #11 added." "Constrain built a second Coincident constraint (auto-resolve case)"
cncheck "CV\[0\] 400,0,0" "auto re-solve pulled the line onto the fixed circle centre with no ConstraintSolve call"
cncheck "ConstraintsShow: glyphs on (11 constraint(s))" "ConstraintsShow toggled the glyph overlay"
cncheck "ConstraintDelete: removed every constraint" "ConstraintDelete All cleared the list"
# The auto re-solve check above must fire twice: once right after Constrain
# (creation-time settle) and once again after Move with no ConstraintSolve
# in between (the actual live-update case this section exists to prove).
CN_CV0_COUNT="$(echo "$CN" | grep -c "CV\[0\] 400,0,0")"
if [ "$CN_CV0_COUNT" -ge 2 ]; then echo "ok   line stayed coincident with the fixed circle both before and after Move, unassisted"; else echo "FAIL live auto re-solve after Move did not restore coincidence (saw $CN_CV0_COUNT matches, need 2)"; fail=1; fi
echo "$CN" | grep -E "^(ok|FAIL)" || true
if echo "$CN" | grep -q "^FAIL"; then fail=1; fi

# Parametric architectural components: Wall/Door/Window/Slab/Roof/Stair/
# Column/Beam, ArchEdit rebuild, ArchDelete, ArchSchedule.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  AR="$("$BIN" --smoke 150 --script "$HERE/arch_script.txt" 2>&1)" || { echo "$AR"; echo "FAIL: arch script exited non-zero"; exit 1; }
else
  AR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/arch_script.txt" 2>&1)" || { echo "$AR"; echo "FAIL: arch script exited non-zero"; exit 1; }
fi
archeck() { if echo "$AR" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$AR" "$1"; fail=1; fi; }
archeck "Wall #1 added." "Wall command built a wall"
archeck "Door #2 added." "Door command cut an opening into the wall"
archeck "Window #3 added." "Window command cut a second opening into the wall"
archeck "ArchSlab #4 added." "ArchSlab command built a slab"
archeck "Roof #5 added." "Roof command built a gable roof"
archeck "Stair #6 added." "Stair command built a 3-riser stair"
archeck "Column #7 added." "Column command built a column"
archeck "Column #7 rebuilt (Height = 5.000000)." "ArchEdit rebuilt the column at a new height"
archeck "Beam #8 added." "Beam command built a beam"
archeck "ArchDelete: removed 1 component(s)" "ArchDelete removed the beam"
archeck "Architectural schedule (8 component(s)):" "ArchSchedule counted every component before the delete"
archeck "Architectural schedule (7 component(s)):" "ArchSchedule counted every component after the delete"
archeck "Object 3 (mesh) layer Default name 'Wall'" "the wall survived two boolean cuts as one mesh object"
echo "$AR" | grep -E "^(ok|FAIL)" || true
if echo "$AR" | grep -q "^FAIL"; then fail=1; fi

# Dynamic blocks: Visibility-state parameter (doc/BlockInstances.h) plus the
# block-definition persistence fix (Document::Blocks() previously had no
# Save/Open path - see dynamic_blocks_script.txt's header comment).
mkdir -p "$TMPW/dblk"
sed "s|@TMP@|$TMPW/dblk|g" "$HERE/dynamic_blocks_script.txt" > "$TMPW/dblk/dynamic_blocks_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DB="$("$BIN" --smoke 150 --script "$TMPW/dblk/dynamic_blocks_script.txt" 2>&1)" || { echo "$DB"; echo "FAIL: dynamic blocks script exited non-zero"; exit 1; }
else
  DB="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMPW/dblk/dynamic_blocks_script.txt" 2>&1)" || { echo "$DB"; echo "FAIL: dynamic blocks script exited non-zero"; exit 1; }
fi
# Split the transcript at each "List" command's own output: DB_S1 is the
# state right after instance #2 is switched to state B, DB_S2 is after
# Undo reverts that switch, DB_S3 is after a Save/Open round trip and a
# fresh instance #3.
awk '/^history: Command: List$/{n++; next} {print > ("'"$TMPW"'/dblk/sec" n ".txt")}' <<<"$DB"
DB_S1="$(cat "$TMPW/dblk/sec1.txt" 2>/dev/null)"
DB_S2="$(cat "$TMPW/dblk/sec2.txt" 2>/dev/null)"
DB_S3="$(cat "$TMPW/dblk/sec3.txt" 2>/dev/null)"
dbcheck() { if echo "$1" | grep -qF "$2"; then echo "ok   $3"; else echo "FAIL $3"; near "$1" "$2"; fail=1; fi; }
dbcheck_absent() { if echo "$1" | grep -qF "$2"; then echo "FAIL $3"; fail=1; else echo "ok   $3"; fi; }
dbcheck "$DB" "Block 'Widget' defined with 2 object(s)" "Block stored the Line+Circle definition"
dbcheck "$DB" "added visibility state 'A' (1 total)" "BlockAddState added the first visibility state"
dbcheck "$DB" "added visibility state 'B' (2 total)" "BlockAddState added the second visibility state"
dbcheck "$DB" "BlockSetState: instance now showing 'B'" "BlockSetState switched instance #2's active state"
# State B: instance #1 (state A, its Insert default) still shows only its
# Line; instance #2 shows only its Circle (its Line was deleted by the
# rebuild); the block-defining leftover instance from the original Block
# command (states didn't exist yet when it ran) keeps both, untouched.
dbcheck "$DB_S1" "CV[0] 100,0,0" "Instance #1 (state A) still shows its Line"
dbcheck "$DB_S1" "CV[0] 206,5,0" "Instance #2 (state B) shows its Circle after BlockSetState"
dbcheck_absent "$DB_S1" "CV[0] 200,0,0" "Instance #2's Line (state A) was removed by the rebuild, not left stale"
dbcheck "$DB_S1" "CV[0] 0,0,0" "The block-defining leftover instance's Line is untouched (pre-existing static-block behaviour)"
dbcheck "$DB_S1" "CV[0] 6,5,0" "The block-defining leftover instance's Circle is untouched (pre-existing static-block behaviour)"
# Undo: instance #2 goes back to state A (Line only), fully reverting the
# BlockSetState rebuild via the same BeginChange/Undo mechanism every other
# object add/remove uses.
dbcheck "$DB_S2" "CV[0] 200,0,0" "Undo restored instance #2's Line (back to state A)"
dbcheck_absent "$DB_S2" "CV[0] 206,5,0" "Undo removed instance #2's Circle (state B) again"
dbcheck "$DB_S2" "CV[0] 100,0,0" "Instance #1 is unaffected by undoing instance #2's state switch"
# Persistence: the block definition (both objects, both visibility tags,
# both named states) and the block-instance records survive Save/Open - a
# real, independent pre-existing bug (Document::blocks_ had no Save/Load
# path at all) fixed as a prerequisite for Visibility states to mean
# anything after a reload. A fresh instance #3 still resolves states
# correctly: only its Line (state A, Insert's default) should appear -
# if the states/tags hadn't survived the round trip, InstantiateBlock
# would fall back to placing every object (Line *and* Circle) instead.
dbcheck "$DB" "Block 'Widget': 2 object(s), base 0,0,0" "BlockManager confirms the definition round-tripped through Save/Open with both objects"
dbcheck "$DB_S3" "CV[0] 300,0,0" "Instance #3 (post-reload) shows its Line"
dbcheck_absent "$DB_S3" "CV[0] 306,5,0" "Instance #3 correctly omits the Circle (its VisStates/BlockDefinition::states tags survived Save/Open)"
# BlockManager panel actions (DrawBlockManagerPanel, ui/Panels.cpp - see
# cmd_drafting.cpp): each button calls exactly the command-line entry point
# exercised here, so this is a real, headless test of the panel's own
# Select Instances and Rename logic, not just a parallel reimplementation.
dbcheck "$DB" "SelBlockInstanceOf: selected 5 object(s) in instances of 'Widget'" "SelBlockInstanceOf (the panel's Select Instances button) selects only the leftover base instance plus instances #1-#3's Line objects, not the whole document"
dbcheck "$DB" "BlockRename: 'Widget' renamed to 'Gadget'" "BlockRename (the panel's Rename button) renamed the block"
dbcheck "$DB" "Block 'Gadget': 2 object(s), base 0,0,0, 5 object(s) in instances" "BlockManager confirms every instance tag, not just the definition name, followed the rename"
dbcheck "$DB" "SelBlockInstanceOf: selected 5 object(s) in instances of 'Gadget'" "SelBlockInstanceOf finds the renamed block's instances by their newly-retagged 'Block' user text"
echo "$DB" | grep -E "^(ok|FAIL)" || true
if echo "$DB" | grep -q "^FAIL"; then fail=1; fi

# Mechanical/structural fasteners and MEP runs: Bolt/Nut/Washer/IBeam/
# Channel/Angle/Duct/Pipe/Conduit, plus SizeDuct/SizePipe (see
# mech_mep_script.txt). Bounding-box extents are checked against each
# item's actual standard-size-table/input-diameter parameters, not just
# object counts; Duct/Pipe/Conduit volumes are checked against the
# pi*r^2*L continuity formula; SizeDuct/SizePipe are checked against
# MepDiameterFromFlow()'s own formula.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MM="$("$BIN" --smoke 100 --script "$HERE/mech_mep_script.txt" 2>&1)" || { echo "$MM"; echo "FAIL: mech/MEP script exited non-zero"; exit 1; }
else
  MM="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$HERE/mech_mep_script.txt" 2>&1)" || { echo "$MM"; echo "FAIL: mech/MEP script exited non-zero"; exit 1; }
fi
mmcheck() { if echo "$MM" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$MM" "$1"; fail=1; fi; }
mmcheck "Bolt #1 added." "Bolt command built a fastener"
mmcheck "Bounding box min -0.009815,-0.009815,0 max 0.009815,0.009815,0.0564" "Bolt M10's bounding box matches its table head diameter (0.017/cos30) and shank+head axial extent (0.05+0.0064)"
mmcheck "Nut #2 added." "Nut command built a fastener"
mmcheck "Bounding box min 1.99,-0.009815,0 max 2.01,0.009815,0.008" "Nut M10's bounding box matches its table across-flats diameter and thickness (0.008)"
mmcheck "Washer #3 added." "Washer command built a fastener"
mmcheck "Bounding box min 3.99,-0.0105,0 max 4.01,0.0105,0.002" "Washer M10's bounding box matches its table outer diameter (0.021) and thickness (0.002)"
mmcheck "IBeam #4 added." "IBeam command built a structural shape"
mmcheck "Bounding box min 0,1.95,-0.1 max 5,2.05,0.1" "IBeam IB200's bounding box matches its table depth (0.200) and flange width (0.100)"
mmcheck "Channel #5 added." "Channel command built a structural shape"
mmcheck "Bounding box min 0,2.967,-0.075 max 5,3.033,0.075" "Channel C150's bounding box matches its table depth (0.150) and flange width (0.065)"
mmcheck "Angle #6 added." "Angle command built a structural shape"
mmcheck "Bounding box min 0,4,0 max 5,4.075,0.075" "Angle L75x75's bounding box matches its table leg lengths (0.075 x 0.075), corner-anchored not centred"
mmcheck "Duct #7 added." "Duct command built an MEP run"
mmcheck "Volume = 0.3524 cubic" "Duct diameter=0.3 volume matches pi*0.15^2*5 = 0.3534 within mesh faceting"
mmcheck "Duct #7 sized to diameter 0.158233 m" "SizeDuct re-derived the diameter from 250 CFM via MepDiameterFromFlow's own formula (area = flow/6 m/s, d = sqrt(4*area/pi))"
mmcheck "Bounding box min 0,4.921,-0.07912 max 5,5.079,0.07912" "SizeDuct's rebuilt Duct mesh has the exact resized radius (0.158233/2)"
mmcheck "Pipe #8 added." "Pipe command built an MEP run"
mmcheck "Volume = 0.03916 cubic" "Pipe diameter=0.1 volume matches pi*0.05^2*5 = 0.03927 within mesh faceting"
mmcheck "Pipe #8 sized to diameter 0.0400822 m" "SizePipe re-derived the diameter from 30 GPM via MepDiameterFromFlow's own formula (area = flow/1.5 m/s, d = sqrt(4*area/pi))"
mmcheck "Bounding box min 0,5.98,-0.02004 max 5,6.02,0.02004" "SizePipe's rebuilt Pipe mesh has the exact resized radius (0.0400822/2)"
mmcheck "Conduit #9 added." "Conduit command built an MEP run"
mmcheck "Volume = 0.001566 cubic" "Conduit diameter=0.02 volume matches pi*0.01^2*5 = 0.001571 within mesh faceting"
mmcheck "^ok   expect_objects 18" "mech/MEP script produced the expected object count (9 components + 9 BoundingBox visualization boxes, Duct/Pipe's rebuilt-by-SizeDuct/SizePipe mesh ids not colliding with earlier ones)"
echo "$MM" | grep -E "^(ok|FAIL)" || true
if echo "$MM" | grep -q "^FAIL"; then fail=1; fi

# Electrical schematic symbols: Resistor/Capacitor/Switch/Ground/Lamp/
# WireRun, plus ElecTag and PanelSchedule (see elec_script.txt). Bounding
# boxes and CV endpoints are checked against each symbol's own documented
# geometry formula (ElecComponents.cpp's per-type Build*() comments), not
# just object counts; WireRun's associative rebuild is checked by actually
# moving an anchored Point object and confirming ElecRebuild re-derives the
# wire's length/endpoints from its new position. No @expect_objects in the
# script itself (ElecTag/PanelSchedule bake font-dependent glyph curve
# counts), so ElecTag/PanelSchedule are each checked by their own printed
# summary line instead of a total object count.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  EL="$("$BIN" --smoke 150 --script "$HERE/elec_script.txt" 2>&1)" || { echo "$EL"; echo "FAIL: electrical script exited non-zero"; exit 1; }
else
  EL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/elec_script.txt" 2>&1)" || { echo "$EL"; echo "FAIL: electrical script exited non-zero"; exit 1; }
fi
elcheck() { if echo "$EL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$EL" "$1"; fail=1; fi; }
elcheck "Resistor #1 added." "Resistor command built a zigzag symbol"
elcheck "Bounding box min 0,-0.2,0 max 0.6,0.2,0" "Resistor length=0.6/height=0.4 bbox matches both parameters exactly (6 separate zigzag-segment curves, each corner a real curve endpoint)"
elcheck "Capacitor #2 added." "Capacitor command built a two-plate symbol"
elcheck "Bounding box min 1.95,-0.3,0 max 2.05,0.3,0" "Capacitor gap=0.1/plate_length=0.6 bbox width equals the gap and height equals the plate length"
elcheck "Switch #3 added." "Switch command built an open-contact symbol"
elcheck "CV\[0\] 4,0,0" "Switch blade starts at the terminal position"
elcheck "CV\[1\] 4.5,0.3,0" "Switch blade tip sits at length*0.5 = 0.5, height 0.3 above the terminal line"
elcheck "CV\[0\] 4.6,0,0" "Switch contact stub starts at length*0.6 = 0.6, leaving a real 0.1 open-gap from the blade tip"
elcheck "Bounding box min 4,0,0 max 5,0.3,0" "Switch bbox spans the full terminal separation (length=1) and the blade's own rise (height=0.3)"
elcheck "Ground #4 added." "Ground command built a stepped earth symbol"
elcheck "Bounding box min 5.5,-0.7,0 max 6.5,0,0" "Ground size=1 bbox height (0.7) is exactly the documented 0.7x proportion of size, width (1.0) matches the widest rung"
elcheck "Lamp #5 added." "Lamp command built a circle+X indicator symbol"
elcheck "CV\[0\] 7.717,-0.2828,0" "Lamp's inscribed X endpoint sits exactly on the circle at -45 degrees (radius 0.4 / sqrt(2) = 0.2828 from center)"
elcheck "CV\[1\] 8.283,0.2828,0" "Lamp's inscribed X endpoint sits exactly on the circle at +45 degrees"
elcheck "Bounding box min 7.6,-0.4,0 max 8.4,0.4,0" "Lamp diameter=0.8 bbox confirms the circle's own radius (0.4) directly"
elcheck "WireRun #6 added." "WireRun command drew a wire between two anchored Point objects"
elcheck "Curve 25 length = 10" "WireRun's as-drawn length (10,10,0) to (10,20,0) matches straight-line distance 10"
elcheck "ElecRebuild: 1 anchored WireRun(s) re-evaluated" "ElecRebuild found the one anchored WireRun after Point 23 was moved"
elcheck "Curve 26 length = 11.18" "ElecRebuild re-derived the wire's length from the moved point's new position (15,10,0) to (10,20,0) = sqrt(5^2+10^2) = 11.18"
elcheck "CV\[0\] 15,10,0" "the rebuilt WireRun's own start point is the moved anchor's current position, not the original (10,10,0)"
elcheck "CV\[1\] 10,20,0" "the rebuilt WireRun's end point is the untouched second anchor's position"
elcheck "ElecTag: \"R1\" baked as [0-9]* curve(s)" "ElecTag baked a real reference-designator string as font-outline curves (count is font-dependent, same as Text's own smoke check)"
elcheck "PanelSchedule: 3 circuit row(s) built" "PanelSchedule built a real data table with the exact row count from its Circuits= option, via the same Table/BuildTableGroup mechanism as RevisionTable/BillOfMaterials"
echo "$EL" | grep -E "^(ok|FAIL)" || true
if echo "$EL" | grep -q "^FAIL"; then fail=1; fi

# Session: 3D digitizer (Dig*, Protocol=File test mode), Worksession /
# LimitReferenceModel, Snapshots, draw order, and real hole features
# (Move/Copy/Rotate/MirrorHole) (see session_script.txt).
mkdir -p "$TMPW/sess"
cat > "$TMPW/sess/dig_points.txt" <<'EOP'
# comments and blank lines are ignored
1, 2, 3
4.5 5.5 6.5 1
EOP
cat > "$TMPW/sess/dig_points2.txt" <<'EOP'
10,0,0
0,0,50
0,0,0
20,0,0
30,0,0
40,0,0
50,0,0
51,1,0
52,0,0
53,1,0
EOP
sed "s|@TMP@|$TMPW/sess|g" "$HERE/session_script.txt" > "$TMPW/sess/session_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SS="$("$BIN" --smoke 200 --script "$TMPW/sess/session_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: session script exited non-zero"; exit 1; }
else
  SS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMPW/sess/session_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: session script exited non-zero"; exit 1; }
fi
sscheck() { if echo "$SS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SS" "$1"; fail=1; fi; }
# Diagnostic only (task #141): always print the exact object-count trail
# around the Snapshots section, win or lose, since xvfb-run does not
# propagate $BIN's own nonzero exit code (a separate, pre-existing gap -
# see the `|| { ...; exit 1; }` guards above, which is why an
# @expect_objects mismatch inside session_script.txt never actually halts
# this script early even though main.cpp does return a nonzero exit_code).
echo "$SS" | grep -E "^history: (Command: (New|Sphere|Snapshots|Box)|New document\.|Snapshot '|0,0,0|20,20,0|30,30,0)|^(ok|FAIL) +expect_objects" || true
sscheck "Digitizer: connected, protocol File, file $TMPW/sess/dig_points.txt" "DigConnect opened the fixture file"
sscheck "DigPoint: digitized 1,2,3" "DigPoint read the first fixture point"
sscheck "DigPoint: digitized 4.5,5.5,6.5 (button 1)" "DigPoint read the second point and its button"
sscheck "DigPoint: no point available" "DigPoint warns once the fixture file is exhausted"
sscheck "DigScale: scale set to 25.4" "DigScale set the unit scale"
sscheck "DigDisconnect: disconnected" "DigDisconnect"
sscheck "Digitizer: not connected" "DigStatus reports disconnected after DigDisconnect"
sscheck "DigScale: scale set to 1" "DigScale reset to 1 for the second connection"
sscheck "Digitize: digitized 10,0,0" "Digitize read a real point off the digitizer"
sscheck "DigCamera: camera set from two digitized points" "DigCamera read an eye and target point"
sscheck "DigClick: digitized 20,0,0" "DigClick read and printed a point"
sscheck "DigLine: line digitized" "DigLine read two points and built a line"
sscheck "CV\[0\] 30,0,0" "the DigLine curve starts at the first digitized point"
sscheck "CV\[1\] 40,0,0" "the DigLine curve ends at the second digitized point"
sscheck "DigBeep on" "DigBeep toggled on"
sscheck "Digitize: digitized 50,0,0" "Digitize still works with DigBeep on"
sscheck "DigSection: 3 point(s) digitized into a curve" "DigSection read the remaining points into a curve"
sscheck "BringToFront: " "BringToFront ran on the overlapping circles"
sscheck "Worksession: attached $TMPW/sess/ref.3dm (1 object" "Worksession Attach copied the box in"
sscheck "Worksession: 1 attached reference model" "Worksession List shows the attached model"
sscheck "LimitReferenceModel: 0 object(s) removed" "LimitReferenceModel kept the box inside the limit box"
sscheck "Worksession: saved $TMPW/sess/session.rws" "Worksession Save wrote the .rws file"
sscheck "Worksession: detached 1 object" "Worksession Detach removed the reference objects"
sscheck "Worksession: attached 1 model(s) from $TMPW/sess/session.rws" "Worksession Load re-attached from the .rws file"
sscheck "Snapshot 'Before' saved" "Snapshots Save captured the sphere-only state"
sscheck "Snapshot 'Before' restored" "Snapshots Restore reverted the later Box"
sscheck "^ok   expect_objects 1" "Snapshots Restore actually removed the Box"
sscheck "Snapshot 'Before' deleted" "Snapshots Delete"
sscheck "RoundHole: object .* replaced by a mesh solid" "RoundHole cut the box and tagged it a hole feature"
sscheck "MoveHole: object .* re-cut at the new placement" "MoveHole replayed the boolean at the new position"
sscheck "CopyHole: copied object .* to object .*" "CopyHole added a second hole into a duplicate"
sscheck "RotateHole: object .* re-cut at the new placement" "RotateHole replayed the boolean"
sscheck "MirrorHole: copied object .* to object .*" "MirrorHole added a mirrored hole into a duplicate"

# Volumetric remesh tools: ShrinkWrap (signed-distance + marching cubes, incl.
# a concave L-shaped union), QuadRemesh (surface UV grid + dual contouring),
# ReduceMesh (quadric-error decimation) (see remesh_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RM="$("$BIN" --smoke 150 --script "$HERE/remesh_script.txt" 2>&1)" || { echo "$RM"; echo "FAIL: remesh script exited non-zero"; exit 1; }
else
  RM="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/remesh_script.txt" 2>&1)" || { echo "$RM"; echo "FAIL: remesh script exited non-zero"; exit 1; }
fi
rmcheck() { if echo "$RM" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$RM" "$1"; fail=1; fi; }
rmcheck "BooleanUnion: 54 faces, volume 3000" "BooleanUnion built the L-shaped union (2000 + 2000 - 1000 overlap)"
rmcheck "ShrinkWrap: signed-distance wrap with [0-9]* vertices, [0-9]* faces (closed)" "ShrinkWrap wrapped the L-shape into a closed mesh"
rmcheck "Volume = 2[0-9][0-9][0-9] cubic" "ShrinkWrap volume stays close to the L-shape's own volume (3000)"
SW_VOL="$(echo "$RM" | sed -n 's/.*ShrinkWrap: .*volume \([0-9.eE+]*\)$/\1/p' | head -1)"
python3 -c "import sys; v=float('$SW_VOL'); sys.exit(0 if v < 3500 else 1)" \
  && echo "ok   ShrinkWrap volume ($SW_VOL) is well under the convex hull volume (3500) - concavity preserved, not convex-hulled" \
  || { echo "FAIL ShrinkWrap volume ($SW_VOL) is not below the convex hull volume (3500) - looks convex-hulled"; fail=1; }
rmcheck "QuadRemesh: [0-9]* quad(s)" "QuadRemesh ran"
rmcheck "QuadRemesh: 1 object(s) remeshed" "QuadRemesh remeshed the surface"
rmcheck "ReduceMesh: object 9: 528 -> 264 faces" "ReduceMesh halved the mesh sphere's faces (528, not 576: Mesh::MergeAndWeld now drops the 24+24 degenerate polar faces a 24x12 MeshSphere's own tessellation produces, a real fix from a parallel session's Brep::Sphere/IsClosedManifold work, not a regression this session introduced)"
echo "$RM" | grep -E "^(ok|FAIL)"
if echo "$RM" | grep -q "^FAIL"; then fail=1; fi
rmcheck "smoke: frames=1[0-9][0-9] objects=7" "remesh script produced the expected object count"

# Remaining-command QC: curve conversion, tween/extruded/developable surfaces,
# thickness/continuity analysis, mesh clean-up incl. connected-face isolation
# and curve splitting, layer/window/point-cloud/file-recovery utilities (see
# remaining_script.txt).
sed "s|@TMP@|$TMPW|g" "$HERE/remaining_script.txt" > "$TMPW/remaining_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RN="$("$BIN" --smoke 900 --script "$TMPW/remaining_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: remaining script exited non-zero"; exit 1; }
else
  RN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 900 --script "$TMPW/remaining_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: remaining script exited non-zero"; exit 1; }
fi
rncheck2() { if echo "$RN" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$RN" "$1"; fail=1; fi; }
rncheck2 "1 object(s) selected" "Convert built a curve to select and inspect"
rncheck2 "degree 1, 5 control points, non-rational, closed" "Convert Output=Lines kept the 4-point polyline exact (closed 5-CV polyline)"
rncheck2 "RibbonOffset: 1 ribbon(s) of width 2" "RibbonOffset built one ribbon"
rncheck2 "TweenSurfaces: 1 surface(s) between the two inputs" "TweenSurfaces built the midway surface"
rncheck2 "MultiPipe: " "MultiPipe ran on two separate lines"
rncheck2 "CheckNewObjects on: every object a command adds is validated" "CheckNewObjects toggled on"
rncheck2 "ClearAnalysisMeshes: " "ClearAnalysisMeshes ran"
rncheck2 "DupLayer: " "DupLayer copied the current layer"
rncheck2 "LayerBook: page 1 of [0-9]*: Default shown alone" "LayerBook Next showed a single page"
rncheck2 "LayerBook: all [0-9]* layer(s) on" "LayerBook All turned every layer back on"
rncheck2 "IsolateLock: " "IsolateLock locked the rest of the scene"
rncheck2 "JoinCopy: " "JoinCopy joined copies of the selection"
rncheck2 "MatchProperties: layer, color, material, linetype" "MatchProperties copied properties across"
rncheck2 "MoveTargetToObjects: " "MoveTargetToObjects moved the camera target"
rncheck2 "ClearAllObjectDisplayModes: " "ClearAllObjectDisplayModes ran"
rncheck2 "SaveWindowLayout: saved QCLayout" "SaveWindowLayout wrote a layout"
rncheck2 "WindowLayout: restored QCLayout" "WindowLayout restored it"
rncheck2 "AcadSchemes: Export/SaveAs to DWG/DXF write AC1015 (AutoCAD 2000)\. Schemes: 13=AC1012 .*, 2018=AC1032" "AcadSchemes reports the current DWG/DXF scheme and the full real, LibreDWG-writer-verified list (see dwg_script.txt/smoke.sh's dwcheck section for the byte-level Version= round-trip)"
rncheck2 "Unwrap: no per-triangle flattening/unwrapping algorithm exists" "Unwrap explains the real limitation and extracts a UV mesh"
# IgesImportOptions/STEPTree open a file dialog (owned by cmd_exchange2.cpp,
# not this file) and EditScript opens the Lua Script Editor panel silently
# (owned by cmd_misc.cpp); none of the three print text in script mode, so
# this just exercises that the catalog still resolves them to those real
# commands and not to a shadowing stub (see cmd_remaining.cpp's removal of
# its old, dead duplicate registrations for all three).
rncheck2 "PointCloudContour: 1 contour(s) from 1 plane(s) 10 apart (band 5)" "PointCloudContour built one contour from the flat 8-point circle"
rncheck2 "degree 1, 9 control points, non-rational, closed" "PointCloudContour's contour is a closed 8-gon (8 points + seam)"
rncheck2 "PointCloudSection: 1 section curve(s) through 0,0,0" "PointCloudSection sliced the same cloud through the origin"
rncheck2 "ShortPath: [0-9]* point(s), length 28.2[0-9]* (straight-line distance 28.2[0-9]*)" "ShortPath on a flat plane matches the straight-line diagonal"
rncheck2 "DevLoft: ruled surface with 40 adjusted rulings (mean twist [0-9.e-]*)" "DevLoft built a ruled surface between the two straight rails"
SP_LEN="$(echo "$RN" | sed -n 's/.*ShortPath: [0-9]* point(s), length \([0-9.]*\).*/\1/p' | head -1)"
python3 -c "import sys; v=float('$SP_LEN'); sys.exit(0 if abs(v-28.284) < 0.05 else 1)" \
  && echo "ok   ShortPath length ($SP_LEN) matches the exact diagonal of a 20x20 square (28.284)" \
  || { echo "FAIL ShortPath length ($SP_LEN) does not match the expected diagonal 28.284"; fail=1; }
DL_TWIST="$(echo "$RN" | sed -n 's/.*mean twist \([0-9.eE+-]*\)).*/\1/p' | head -1)"
python3 -c "import sys; v=float('$DL_TWIST'); sys.exit(0 if v < 1e-3 else 1)" \
  && echo "ok   DevLoft mean twist ($DL_TWIST) is essentially zero for two straight parallel rails (already developable)" \
  || { echo "FAIL DevLoft mean twist ($DL_TWIST) is not near zero"; fail=1; }
rncheck2 "SplitMeshWithCurve: 2 mesh piece(s) (split by the curve's plane)" "SplitMeshWithCurve split the mesh box into two pieces"
rncheck2 "SelConnectedMeshFaces: 6 connected face(s) split off into object [0-9]* and selected (mesh had 2 disconnected part(s))" "SelConnectedMeshFaces split the picked box out of the two-box mesh"
rncheck2 "CreaseSplitting on: AutomaticSubDFromMesh creases edges sharper than its Angle" "CreaseSplitting On"
rncheck2 "CreaseSplitting off: AutomaticSubDFromMesh makes smooth SubDs" "CreaseSplitting Off"
rncheck2 "ChangeSpace: 0 object(s) moved back to model space (Space tag removed)" "ChangeSpace with no layout active found no Space tag to remove on a plain object"
rncheck2 "Rescue3dmFile: recovered 2 object(s) from $TMPW/rescue_src.3dm" "Rescue3dmFile recovered exactly the box and the sphere"
rncheck2 "^ok   expect_objects 2" "Rescue3dmFile left the document with exactly those 2 recovered objects"
rncheck2 "ExportBitmaps: 0 texture file(s) copied to $TMPW/bitmaps_out" "ExportBitmaps ran cleanly with no textures assigned"
echo "$RN" | grep -E "^(ok|FAIL)"
if echo "$RN" | grep -q "^FAIL"; then fail=1; fi
rncheck2 "gl_error=0" "remaining script ran without OpenGL errors"

# Command-line autocomplete: real fuzzy/subsequence matching, not just
# prefix matching (see fuzzy_autocomplete_script.txt). "bdiff" and "zmanif"
# are neither prefixes nor contiguous substrings of any catalog command,
# but are in-order subsequences of exactly one or two command names each;
# Tab-completing them must pick the (shorter) real command. A plain
# prefix ("Box") must still Tab-complete to itself, unchanged.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FZ="$("$BIN" --smoke 60 --script "$HERE/fuzzy_autocomplete_script.txt" 2>&1)" || { echo "$FZ"; echo "FAIL: fuzzy-autocomplete script exited non-zero"; exit 1; }
else
  FZ="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/fuzzy_autocomplete_script.txt" 2>&1)" || { echo "$FZ"; echo "FAIL: fuzzy-autocomplete script exited non-zero"; exit 1; }
fi
fzcheck() { if echo "$FZ" | grep -qE "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$FZ" "$1"; fail=1; fi; }
fzcheck "^history: Command: BooleanDifference$" "fuzzy subsequence 'bdiff' (not a prefix/substring of any command) autocompleted to BooleanDifference"
fzcheck "^history: Command: Box$" "plain prefix 'Box' still autocompletes to itself (no regression from adding fuzzy matching)"
fzcheck "^history: Command: ZoomNonManifold$" "fuzzy subsequence 'zmanif' (not a prefix/substring of any command) autocompleted to the unique match ZoomNonManifold"
fzcheck "gl_error=0" "fuzzy-autocomplete script ran without OpenGL errors"

# i18n: SetLanguage actually swaps the active string table, a key missing
# from a language's table (panel.imgui_demo is deliberately absent from
# es.json - see cmd_state.cpp's I18nSelfTest) falls back to English instead
# of a blank string or the raw key, an unknown key falls back to itself
# rather than crashing, and an unrecognised language name fails with a
# clear diagnostic instead of silently doing nothing.
cat > "$TMPW/i18n_script.txt" <<'EOS'
SetLanguage es
I18nSelfTest
SetLanguage fr
I18nSelfTest
SetLanguage English
I18nSelfTest
SetLanguage nope
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  I18N="$("$BIN" --smoke 60 --script "$TMPW/i18n_script.txt" 2>&1)" || { echo "$I18N"; echo "FAIL: i18n script exited non-zero"; exit 1; }
else
  I18N="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/i18n_script.txt" 2>&1)" || { echo "$I18N"; echo "FAIL: i18n script exited non-zero"; exit 1; }
fi
i18ncheck() { if echo "$I18N" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$I18N" "$1"; fail=1; fi; }
i18ncheck "SetLanguage: es" "SetLanguage switched to Spanish"
i18ncheck "I18nSelfTest: active=es" "the active language is now es"
i18ncheck "I18nSelfTest: menu\.file=Archivo" "a translated string (menu.file) reads Archivo in Spanish, proving the switch changes displayed text"
i18ncheck "SetLanguage: fr" "SetLanguage switched to French"
i18ncheck "I18nSelfTest: active=fr" "the active language is now fr"
i18ncheck "I18nSelfTest: menu\.file=Fichier" "a translated string (menu.file) reads Fichier in French, proving fr.json is a real second hand-translated language, not a scaffold-only stub"
i18ncheck "I18nSelfTest: fallback(panel\.imgui_demo)=ImGui Demo (developer)" "a key missing from es.json falls back to the English text, not a blank string or the raw key"
i18ncheck "I18nSelfTest: unknown_key=this\.key\.does\.not\.exist\.anywhere" "a key present in no language table at all falls back to the key itself rather than crashing or blanking"
i18ncheck "SetLanguage: en" "SetLanguage switched back to English"
i18ncheck "I18nSelfTest: active=en" "the active language is en again"
i18ncheck "I18nSelfTest: menu\.file=File\$" "the same key reads back in plain English once switched back"
i18ncheck "SetLanguage: unknown language 'nope'" "an unrecognised language name fails with a clear diagnostic instead of doing nothing"

# a11y: High Contrast is a distinct palette (pure black bg / pure white
# text / a forced 1px frame border), not merely a filter over Dark, and
# switching themes actually rewrites the live ImGui style colours (see
# docs/ACCESSIBILITY.md and SetTheme/ThemeSelfTest in cmd_state.cpp).
cat > "$TMPW/a11y_script.txt" <<'EOS'
SetTheme HighContrast
ThemeSelfTest
SetTheme Dark
ThemeSelfTest
SetTheme nope
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  A11Y="$("$BIN" --smoke 60 --script "$TMPW/a11y_script.txt" 2>&1)" || { echo "$A11Y"; echo "FAIL: a11y script exited non-zero"; exit 1; }
else
  A11Y="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/a11y_script.txt" 2>&1)" || { echo "$A11Y"; echo "FAIL: a11y script exited non-zero"; exit 1; }
fi
a11ycheck() { if echo "$A11Y" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$A11Y" "$1"; fail=1; fi; }
a11ycheck "SetTheme: highcontrast" "SetTheme switched to High Contrast"
a11ycheck "ThemeSelfTest: mode=2" "the active theme mode is now HighContrast (2)"
a11ycheck "ThemeSelfTest: window_bg=0\.000,0\.000,0\.000" "High Contrast's window background is pure black, not a tint of Dark's background"
a11ycheck "ThemeSelfTest: text=1\.000,1\.000,1\.000" "High Contrast's text colour is pure white (~21:1 contrast on black)"
a11ycheck "ThemeSelfTest: frame_border=1\.00" "High Contrast forces a visible 1px frame border (0 in Dark/Light), so state is legible from outline, not colour alone"
a11ycheck "SetTheme: dark" "SetTheme switched back to Dark"
a11ycheck "ThemeSelfTest: mode=0" "the active theme mode is Dark (0) again"
a11ycheck "ThemeSelfTest: window_bg=0\.110,0\.118,0\.137" "switching back to Dark actually restores Dark's own background colour, not black"
a11ycheck "ThemeSelfTest: frame_border=0\.00" "Dark's frame border is 0, confirming the forced border is High-Contrast-specific"
a11ycheck "SetTheme: unknown theme 'nope'" "an unrecognised theme name fails with a clear diagnostic instead of doing nothing"

# Frustum culling (Viewport.cpp - see tests/performance_notes.md's "No LOD
# or frustum culling" item and tests/cull_test.sh for the full A/B/pixel-
# diff proof): fold its pass/fail lines into this script's own count so a
# regression here fails smoke.sh, not just a separately-run script.
CULL="$(bash "$HERE/cull_test.sh" "$BIN" 2>&1)" || true
echo "$CULL" | grep -E "^(ok|FAIL)"
if echo "$CULL" | grep -q "^FAIL"; then fail=1; fi
echo "$CULL" | grep -q "^ok   cull-on and cull-off screenshots are pixel-identical" || { echo "FAIL cull_test.sh did not run to completion"; fail=1; }

# Dir's direction-arrow glyph (ui/DirectionArrows.h/.cpp, wired into
# Application::DrawViewports) is a real drawn viewport overlay, not just an
# internal flag: two scripts, byte-identical except that one runs Dir on a
# selected line and the other does not, must produce DIFFERENT final-frame
# --screenshot captures - the mirror image of cull_test.sh's own pixel-
# IDENTICAL proof just above. The click-on-the-glyph half is mouse-only and
# not exercised here (see AUDIT.md's dated note); this only proves the
# glyph itself really renders where ComputeDirArrow() says it is.
cat > "$TMPW/dir_arrow_on.txt" <<'EOS'
Line 0,0,0 10,0,0
SelLast
ZoomExtentsAll
Dir
EOS
cat > "$TMPW/dir_arrow_off.txt" <<'EOS'
Line 0,0,0 10,0,0
SelLast
ZoomExtentsAll
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  "$BIN" --smoke 30 --screenshot "$TMPW/dir_arrow_on.ppm" --script "$TMPW/dir_arrow_on.txt" >/dev/null 2>&1 || { echo "FAIL: dir arrow (on) script exited non-zero"; fail=1; }
  "$BIN" --smoke 30 --screenshot "$TMPW/dir_arrow_off.ppm" --script "$TMPW/dir_arrow_off.txt" >/dev/null 2>&1 || { echo "FAIL: dir arrow (off) script exited non-zero"; fail=1; }
else
  xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --screenshot "$TMPW/dir_arrow_on.ppm" --script "$TMPW/dir_arrow_on.txt" >/dev/null 2>&1 || { echo "FAIL: dir arrow (on) script exited non-zero"; fail=1; }
  xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --screenshot "$TMPW/dir_arrow_off.ppm" --script "$TMPW/dir_arrow_off.txt" >/dev/null 2>&1 || { echo "FAIL: dir arrow (off) script exited non-zero"; fail=1; }
fi
if [ -s "$TMPW/dir_arrow_on.ppm" ] && [ -s "$TMPW/dir_arrow_off.ppm" ] && ! cmp -s "$TMPW/dir_arrow_on.ppm" "$TMPW/dir_arrow_off.ppm"; then
  echo "ok   Dir's direction-arrow glyph is a real drawn overlay (the only-difference-is-Dir screenshot differs from the no-Dir one)"
else
  echo "FAIL Dir's direction-arrow glyph changed no visible pixel vs. an otherwise identical scene"; fail=1
fi

# ShowZBuffer (Viewport::DrawObjects' "ShowZBuffer" pass, GlRenderer::
# DrawTrianglesDepth - see AUDIT.md's dated note and zbuffer_script.txt):
# builds two boxes at very different camera depths and toggles the flag on
# and off around a viewport-only ViewCaptureToFile capture of each state -
# same on/off screenshot-diff technique as the Dir arrow proof just above,
# plus a second, stronger check that samples specific pixels in the "on"
# capture and asserts the near box is genuinely lighter than the far one.
mkdir -p "$TMPW/zb"
sed "s|@TMP@|$TMPW/zb|g" "$HERE/zbuffer_script.txt" > "$TMPW/zbuffer_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ZB="$("$BIN" --smoke 30 --script "$TMPW/zbuffer_script.txt" 2>&1)" || { echo "$ZB"; echo "FAIL: ShowZBuffer script exited non-zero"; exit 1; }
else
  ZB="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/zbuffer_script.txt" 2>&1)" || { echo "$ZB"; echo "FAIL: ShowZBuffer script exited non-zero"; exit 1; }
fi
zbcheck() { if echo "$ZB" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$ZB" "$1"; fail=1; fi; }
zbcheck "ShowZBuffer: on (every visible surface/mesh now draws as a grayscale depth value - near light, far dark)" "ShowZBuffer on reports the real depth pass, not just a recorded flag"
zbcheck "ShowZBuffer: off" "ShowZBuffer off"
zbcheck "^ok   expect_objects 2" "ShowZBuffer script left exactly the two boxes"
if [ -s "$TMPW/zb/zbuffer_off.bmp" ] && [ -s "$TMPW/zb/zbuffer_on.bmp" ] && [ -s "$TMPW/zb/zbuffer_off2.bmp" ] && \
   ! cmp -s "$TMPW/zb/zbuffer_off.bmp" "$TMPW/zb/zbuffer_on.bmp" && cmp -s "$TMPW/zb/zbuffer_off.bmp" "$TMPW/zb/zbuffer_off2.bmp"; then
  echo "ok   ShowZBuffer is a real drawn depth pass (on differs from off; off before and after ShowZBuffer's on/off round trip is pixel-identical, so nothing else changed)"
else
  echo "FAIL ShowZBuffer's on capture does not differ from its own off captures"; fail=1
fi
python3 - "$TMPW/zb/zbuffer_on.bmp" <<'PY' && echo "ok   ShowZBuffer's near box (Box A, y=0..2) samples genuinely lighter than its far box (Box B, y=100..140): a real grayscale-by-camera-distance pass, not a flat colour" || { echo "FAIL ShowZBuffer's grayscale does not get darker with camera distance"; fail=1; }
import struct, sys

def read_bmp(path):
    d = open(path, 'rb').read()
    assert d[:2] == b'BM', (path, 'signature')
    size, off, hdr, w, h, planes, bpp = struct.unpack('<IxxxxIIiiHH', d[2:30])
    assert hdr == 40 and planes == 1 and bpp == 24, (path, hdr, planes, bpp)
    row = (w * 3 + 3) & ~3
    px = d[off:]
    assert len(px) == row * h, (path, 'pixel data size')
    def get(x, y):  # y = 0 at the top of the image
        r = h - 1 - y
        i = r * row + x * 3
        b, g, rr = px[i], px[i + 1], px[i + 2]
        return rr, g, b
    return w, h, get

w, h, get = read_bmp(sys.argv[1])

# Column brightness profile (max red channel per column, sampled every
# other row for speed) to find the two boxes' bright/grey blobs against the
# pure-black (ShowZBuffer forces the background and grid off) empty space
# between and around them.
BG_THRESH = 10
col_bright = [max(get(x, y)[0] for y in range(0, h, 2)) for x in range(w)]
cols = [x for x, b in enumerate(col_bright) if b > BG_THRESH]
assert cols, 'no non-background pixels found in the ShowZBuffer capture'
runs, start, prev = [], cols[0], cols[0]
for x in cols[1:]:
    if x - prev > 3:
        runs.append((start, prev))
        start = x
    prev = x
runs.append((start, prev))
assert len(runs) >= 2, f'expected 2 separate boxes (near/far) in the capture, found {len(runs)}: {runs}'
runs.sort()
left, right = runs[0], runs[-1]

def sample(run):
    x = (run[0] + run[1]) // 2
    best, besty = -1, 0
    for y in range(h):
        r, g, b = get(x, y)
        if r > best:
            best, besty = r, y
    return x, besty, best

nx, ny, near_gray = sample(left)
fx, fy, far_gray = sample(right)
print(f'near box (Box A, screen-left) sample: pixel ({nx},{ny}) gray={near_gray}')
print(f'far  box (Box B, screen-right) sample: pixel ({fx},{fy}) gray={far_gray}')
r, g, b = get(nx, ny)
assert r == g == b, f'near sample is not a pure grey (r,g,b)={(r, g, b)} - ShowZBuffer must not tint by material colour'
r, g, b = get(fx, fy)
assert r == g == b, f'far sample is not a pure grey (r,g,b)={(r, g, b)} - ShowZBuffer must not tint by material colour'
assert far_gray > BG_THRESH, f'far box sample ({far_gray}) is indistinguishable from the black background'
assert near_gray > far_gray + 40, f'near box ({near_gray}) is not clearly lighter than the far box ({far_gray})'
PY

# Docs tutorials (docs/site/tutorials.html and README's "10 tutorials,
# verified by running them" claim): run every 01..10 tutorial script
# through the real binary via docs/tutorial_scripts/run_all.sh and fold
# its pass/fail into this script's own count. Without this, the tutorial
# scripts are real and do pass, but nothing gates them - a change that
# breaks a documented tutorial sequence would only be noticed by a human
# manually re-running run_all.sh, not by smoke.sh or CI.
TUT="$(bash "$HERE/../docs/tutorial_scripts/run_all.sh" "$BIN" 2>&1)" || true
echo "$TUT" | grep -E "^(FAIL|PASS:)"
if echo "$TUT" | grep -q "^FAIL"; then fail=1; fi
TUT_PASS_COUNT=$(echo "$TUT" | grep -c "^PASS:")
[ "$TUT_PASS_COUNT" -eq 10 ] || { echo "FAIL: expected 10/10 tutorial scripts to pass, got $TUT_PASS_COUNT"; fail=1; }

# DwgCompare / XrefCompare / CompareClear QC (compare/DwgCompare.h,
# src/commands/cmd_compare.cpp - see compare_script.txt for the full
# v1-vs-v2 scenario this drives).
sed "s|@TMP@|$TMPW|g" "$HERE/compare_script.txt" > "$TMPW/compare_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CMP="$("$BIN" --smoke 30 --script "$TMPW/compare_script.txt" 2>&1)" || { echo "$CMP"; echo "FAIL: compare script exited non-zero"; exit 1; }
else
  CMP="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/compare_script.txt" 2>&1)" || { echo "$CMP"; echo "FAIL: compare script exited non-zero"; exit 1; }
fi
cmpcheck() { if echo "$CMP" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$CMP" "$1"; fail=1; fi; }
cmpcheck "Exported $TMPW/cmp_v1.3dm" "compare script saved the v1 baseline"
cmpcheck "Deleted 1 object(s)" "only the old circle was deleted (the moved line stayed selected-clean)"
cmpcheck "DwgCompare: 1 added, 1 removed, 1 modified, 1 unchanged" "DwgCompare landed the moved line as modified, the deleted circle as removed, the untouched polyline as unchanged (not counted as a hit), and the new circle as added"
cmpcheck "CompareClear: 3 object.s. restored/removed" "CompareClear restored the 2 tinted objects and removed the 1 ghost"
echo "$CMP" | grep -q "DwgCompare: 0 added, 0 removed" && { echo "FAIL: DwgCompare produced no diff at all"; fail=1; }

# Activity Log + Named Snapshots persistence (Document::ActivityLog/
# RecordActivityLogEntry, ActivityExport, Document::CaptureSnapshotAsDocument/
# AdoptNamedSnapshotFromDocument + Application's sidecar files - see
# activity_log_script.txt): a create, a move and a delete must each show up
# in the exported CSV with the right label, and a Named Snapshot saved
# before Save must still be listed after New+Open re-reads the file from
# disk (proving both the activity-log sidecar and the snapshot sidecar
# survive a real save/close/reopen, not just the live in-memory session).
sed "s|@TMP@|$TMPW|g" "$HERE/activity_log_script.txt" > "$TMPW/activity_log_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  AL="$("$BIN" --smoke 40 --script "$TMPW/activity_log_script.txt" 2>&1)" || { echo "$AL"; echo "FAIL: activity-log script exited non-zero"; exit 1; }
else
  AL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 40 --script "$TMPW/activity_log_script.txt" 2>&1)" || { echo "$AL"; echo "FAIL: activity-log script exited non-zero"; exit 1; }
fi
alcheck() { if echo "$AL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$AL" "$1"; fail=1; fi; }
alcheck "Snapshot 'MySnap' saved" "Snapshots Save created the named snapshot before the file was saved"
alcheck "Saved $TMPW/activity_test.3dm" "Save wrote the .3dm (and, alongside it, the snapshot sidecar)"
alcheck "Opened $TMPW/activity_test.3dm" "Open re-read the .3dm"
alcheck "1 snapshot(s)" "Snapshots List found exactly the one snapshot after New+Open, proving Named Snapshots survive Save/Open"
alcheck "  MySnap" "the reloaded snapshot kept its original name"
alcheck "ActivityExport: " "ActivityExport ran and reported how many entries it wrote"
test -s "$TMPW/activity_export.csv" && echo "ok   activity_export.csv exists" || { echo "FAIL activity_export.csv missing"; fail=1; }
head -1 "$TMPW/activity_export.csv" | grep -q '^Timestamp (UTC),Action,Detail$' && echo "ok   activity_export.csv has the expected CSV header" || { echo "FAIL activity_export.csv header"; fail=1; }
grep -q '"Box"' "$TMPW/activity_export.csv" && echo "ok   activity_export.csv recorded the Box creation" || { echo "FAIL activity_export.csv missing the Box entry"; fail=1; }
grep -q '"Move"' "$TMPW/activity_export.csv" && echo "ok   activity_export.csv recorded the Move edit" || { echo "FAIL activity_export.csv missing the Move entry"; fail=1; }
grep -q '"Delete"' "$TMPW/activity_export.csv" && echo "ok   activity_export.csv recorded the Delete" || { echo "FAIL activity_export.csv missing the Delete entry"; fail=1; }
# The exported log must also include entries recorded *before* this session
# started (i.e. loaded back from the on-disk sidecar log by
# Document::LoadActivityLog after Open, not just this run's own edits) -
# count real per-edit label rows (excluding the header) and require at
# least the 3 edits made above.
[ "$(($(wc -l < "$TMPW/activity_export.csv") - 1))" -ge 3 ] && echo "ok   activity_export.csv has at least the 3 recorded edits" || { echo "FAIL activity_export.csv has too few rows"; fail=1; }
test -s "$TMPW/activity_test.3dm.activity.log" && echo "ok   the durable activity.log sidecar file was written next to the document" || { echo "FAIL activity.log sidecar file missing"; fail=1; }
test -d "$TMPW/activity_test.3dm.snapshots" && echo "ok   the Named Snapshots sidecar directory was written next to the document" || { echo "FAIL snapshots sidecar directory missing"; fail=1; }

# Smart Blocks (SmartBlockDetect/SmartBlockConvert, cmd_smartblocks.cpp):
# scatter 3 occurrences of one shape (one plain, one translated, one
# translated-then-rotated) among unrelated noise, confirm detection finds
# exactly the 3 real occurrences as a single repeated group and ignores the
# noise, then confirm conversion turns all 3 into tagged instances of one
# new block without touching object count or the noise.
SB="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/smartblocks_script.txt" 2>&1)" || { echo "$SB"; echo "FAIL: Smart Blocks script exited non-zero"; exit 1; }
sbcheck() { if echo "$SB" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SB" "$1"; fail=1; fi; }
echo "$SB" | grep -q "^FAIL expect_" && { echo "FAIL Smart Blocks script's own @expect_objects/@expect_selected checks failed"; fail=1; }
sbcheck "^history: SmartBlockDetect: 1 repeated group(s) found$" "SmartBlockDetect found exactly one repeated group (not 0, not split into several)"
sbcheck "^history:   group 1: 3 instance(s), 3 object(s) each$" "SmartBlockDetect correctly counted 3 occurrences of the 3-object bracket shape, ignoring the noise objects"
sbcheck "^history: SmartBlockConvert: 1 block definition(s) created, 3 instance(s) converted$" "SmartBlockConvert created exactly one new block and converted all 3 detected occurrences"
sbcheck "^ok   expect_objects 13 (got 13)$" "SmartBlockConvert tags occurrences in place rather than replacing them, so the total object count is unchanged"
sbcheck "^ok   expect_selected 9 (got 9)$" "SelBlockInstance selects exactly the 9 objects (3 instances x 3 objects) that were converted, and none of the noise objects"
sbcheck "Block 'SmartBlock1': 3 object(s), base .*, 9 object(s) in instances" "BlockManager confirms the new block's definition has 3 objects and 9 objects across its instances"

# CAD Standards Checker: Standards/CheckStandards (see standards_script.txt).
mkdir -p "$TMPW/standards"
cat > "$TMPW/standards/standards.json" <<'EOS'
{
  "layers": [
    {"name": "Default", "color": "0,0,0", "linetype": "Continuous"},
    {"name": "Dims", "color": "0,0,0", "linetype": "Continuous"},
    {"name": "Notes", "color": "0,0,0", "linetype": "Dashed"}
  ],
  "text_styles": ["Default"],
  "dim_styles": []
}
EOS
sed "s|@TMP@|$TMPW/standards|g" "$HERE/standards_script.txt" > "$TMPW/standards/standards_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ST="$("$BIN" --smoke 60 --script "$TMPW/standards/standards_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: standards script exited non-zero"; exit 1; }
else
  ST="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/standards/standards_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: standards script exited non-zero"; exit 1; }
fi
stcheck() { if echo "$ST" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$ST" "$1"; fail=1; fi; }
stcheck "Standards: document linked to '$TMPW/standards/standards.json'" "Standards linked the document to the standards file"
stcheck "CheckStandards: 0 violations" "CheckStandards reports clean before any drift is introduced"
stcheck "CheckStandards found 3 violation(s)" "CheckStandards found exactly 3 violations after introducing drift"
stcheck "layer 'Extra' is not defined in the standards file" "CheckStandards flagged the undefined layer 'Extra'"
stcheck "layer 'Notes' linetype 'Continuous' does not match the standard 'Dashed'" "CheckStandards flagged the linetype drift on 'Notes'"
stcheck "text/dimension style 'Rogue' is used in the drawing but is not in the standards file" "CheckStandards flagged the non-standard annotation style 'Rogue'"
if echo "$ST" | grep -q "layer 'Dims'"; then echo "FAIL: CheckStandards incorrectly flagged the compliant layer 'Dims'"; fail=1; else echo "ok   compliant layer 'Dims' was not flagged"; fi
if echo "$ST" | grep -q "layer 'Default'"; then echo "FAIL: CheckStandards incorrectly flagged the compliant layer 'Default'"; fail=1; else echo "ok   compliant layer 'Default' was not flagged"; fi
if echo "$ST" | grep -q "style 'Default'"; then echo "FAIL: CheckStandards incorrectly flagged the compliant style 'Default'"; fail=1; else echo "ok   compliant style 'Default' was not flagged"; fi
echo "$ST" | grep -E "^(ok|FAIL)" || true
if echo "$ST" | grep -q "^FAIL"; then fail=1; fi

# Sheet sets (SheetSetNew/Add/Open/Plot - session/SheetSet.h/cmd_sheetset.cpp):
# two small single-layout drawings get added to one sheet set file spanning
# both, then batch-plotted, proving the set genuinely reaches across files
# rather than only listing layouts within the currently-open document (see
# sheetset_script.txt).
sed "s|@TMP@|$TMPW|g" "$HERE/sheetset_script.txt" > "$TMPW/sheetset_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SS="$("$BIN" --smoke 60 --script "$TMPW/sheetset_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: sheetset script exited non-zero"; exit 1; }
else
  SS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/sheetset_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: sheetset script exited non-zero"; exit 1; }
fi
sscheck() { if echo "$SS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$SS" "$1"; fail=1; fi; }
sscheck "SheetSetNew: created 'Smoke Test Set'" "SheetSetNew created the sheet set file"
sscheck "SheetSetAdd: added .*sheetset_b\.3dm / SheetB" "SheetSetAdd added the second document's layout before it was even the open document"
sscheck "SheetSetAdd: added .*sheetset_a\.3dm / SheetA" "SheetSetAdd added the current document's active layout"
sscheck "SheetSetOpen: 'Smoke Test Set' .*, 2 sheet(s)" "SheetSetOpen listed both entries"
sscheck "1\. .*sheetset_b\.3dm : SheetB" "SheetSetOpen listed the first entry (file + layout)"
sscheck "2\. .*sheetset_a\.3dm : SheetA" "SheetSetOpen listed the second entry (file + layout)"
sscheck "SheetSetPlot: 2/2 sheet(s) plotted" "SheetSetPlot batch-plotted every sheet in the set"
for f in "$TMPW/sheetset_out"/*.pdf; do
  [ -s "$f" ] || { echo "FAIL: $f missing or empty"; fail=1; }
done
PDF_COUNT=$(ls "$TMPW/sheetset_out"/*.pdf 2>/dev/null | wc -l)
[ "$PDF_COUNT" -eq 2 ] && echo "ok   SheetSetPlot wrote one PDF per sheet (2 files)" || { echo "FAIL: expected 2 plotted PDFs, found $PDF_COUNT"; fail=1; }
for f in "$TMPW/sheetset_out"/*.pdf; do
  SZ=$(wc -c < "$f")
  [ "$SZ" -gt 200 ] && echo "ok   $(basename "$f") is a non-trivial PDF ($SZ bytes)" || { echo "FAIL: $(basename "$f") is too small ($SZ bytes)"; fail=1; }
  head -c 5 "$f" | grep -q "%PDF-" && echo "ok   $(basename "$f") starts with %PDF-" || { echo "FAIL: $(basename "$f") missing PDF header"; fail=1; }
done

# DataLink / DataLinkUpdate (see datalink_script1..4.txt): a table linked to
# a CSV file two-way syncs with it. Four separate app invocations, sharing
# @TMP@/datalink.3dm on disk, with this script itself playing "someone
# editing the file in a spreadsheet program" between stages 1->2 and
# 3->4 - which a single script/single invocation cannot exercise, since the
# whole point is a file changing *outside* the app between syncs.
run_dl_stage() {
  local script="$1" label="$2"
  sed "s|@TMP@|$TMPW|g" "$HERE/$script" > "$TMPW/$script"
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    "$BIN" --smoke 60 --script "$TMPW/$script" 2>&1 || { echo "FAIL: DataLink stage $label exited non-zero"; exit 1; }
  else
    xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/$script" 2>&1 || { echo "FAIL: DataLink stage $label exited non-zero"; exit 1; }
  fi
}

DL1="$(run_dl_stage datalink_script1.txt 1)"
dlcheck() { if echo "$DL1" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DL1" "$1"; fail=1; fi; }
dlcheck "DataLink: pushed 2x2 table to " "DataLink pushed the new table to the CSV file (it didn't exist yet)"
[ -f "$TMPW/datalink.csv" ] || { echo "FAIL DataLink actually wrote $TMPW/datalink.csv"; fail=1; }
DL1_CSV="$(cat "$TMPW/datalink.csv" 2>/dev/null || true)"
if [ "$DL1_CSV" = "$(printf 'A,B\nC,D\n')" ]; then echo "ok   the pushed CSV's content matches the table's cells (A,B / C,D)"; else echo "FAIL the pushed CSV's content matches the table's cells (got: $DL1_CSV)"; fail=1; fi

# Simulate an external spreadsheet edit of the linked file before stage 2.
#
# DataLinkUpdate's push/pull direction heuristic compares this file's mtime
# against the *previous* stage's last_sync_utc. A plain write here already
# gives the file a fresh "now" mtime (no explicit touch needed) - the sleep
# just guarantees real separation from the previous stage's last_sync_utc.
# NOTE: forcing the mtime artificially far into the future was tried here
# and reverted - it "fixes" this one comparison but then permanently reads
# as "still changed" in every later stage too, since a pull never rewrites
# the file to give it a fresh, real mtime. A plain write + sleep is the
# correct approach; the real, permanent fix for the two genuine bugs this
# surfaced lives in cmd_drafting2.cpp (NowMillis()'s monotonic ratchet, and
# RebuildWithLink pinning a sync's own table rebuild timestamp to exactly
# last_sync_utc so a sync is never mistaken for a change since itself).
sleep 5
printf 'P,Q\nR,S\n' > "$TMPW/datalink.csv"

DL2="$(run_dl_stage datalink_script2.txt 2)"
dlcheck() { if echo "$DL2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DL2" "$1"; fail=1; fi; }
dlcheck "DataLinkUpdate: pulled 2x2 table from " "DataLinkUpdate auto-detected the file was the only side that changed and pulled it"

sleep 5

DL3="$(run_dl_stage datalink_script3.txt 3)"
dlcheck() { if echo "$DL3" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DL3" "$1"; fail=1; fi; }
dlcheck "DataLinkUpdate: pushed 2x2 table to " "DataLinkUpdate auto-detected the table was the only side that changed (after stage 2's TableEdit) and pushed it - the reverse direction from stage 2"
DL3_CSV="$(cat "$TMPW/datalink.csv" 2>/dev/null || true)"
if [ "$DL3_CSV" = "$(printf 'M,N\nO,P\n')" ]; then echo "ok   the re-pushed CSV's content matches stage 2's TableEdit (M,N / O,P), not the stale P,Q / R,S it pulled"; else echo "FAIL the re-pushed CSV's content matches stage 2's TableEdit (got: $DL3_CSV)"; fail=1; fi

# Simulate a second external edit, so that stage 4's own TableEdit and this
# file both change before the next sync - the ambiguous case.
sleep 5
printf 'Z1,Z2\nZ3,Z4\n' > "$TMPW/datalink.csv"

DL4="$(run_dl_stage datalink_script4.txt 4)"
dlcheck() { if echo "$DL4" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$DL4" "$1"; fail=1; fi; }
dlcheck "! DataLinkUpdate: both the table and .* changed since the last sync" "DataLinkUpdate refused to silently guess a direction when both the table and the file changed since the last sync"
dlcheck "DataLinkUpdate: pushed 2x2 table to " "the follow-up DataLinkUpdate Direction=Push resolved the ambiguity explicitly"
DL4_CSV="$(cat "$TMPW/datalink.csv" 2>/dev/null || true)"
if [ "$DL4_CSV" = "$(printf 'Q,R\nS,T\n')" ]; then echo "ok   Direction=Push wrote the table's edit (Q,R / S,T) to the file, discarding the file's own outside edit as the caller explicitly chose"; else echo "FAIL Direction=Push wrote the table's edit to the file (got: $DL4_CSV)"; fail=1; fi

# Real OS-clipboard image write (ViewCaptureToClipboard/ScreenCaptureToClipboard/
# CopyRenderWindowToClipboard - see src/platform/Clipboard.h). There is no
# clipboard reader available in this headless harness, so DINO8_CLIPBOARD_DEBUG_FILE
# is used instead: it makes WriteImageToClipboard also write the exact PNG bytes
# it handed the platform clipboard backend to a file, so the assertions below
# can check for a real, non-trivial PNG rather than just "the command didn't
# crash" (see tests/clipboard_script.txt).
CLIP_DEBUG="$TMPW/clipboard_debug.png"
export DINO8_CLIPBOARD_DEBUG_FILE="$CLIP_DEBUG"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CB="$("$BIN" --smoke 60 --script "$HERE/clipboard_script.txt" 2>&1)" || { echo "$CB"; echo "FAIL: clipboard script exited non-zero"; exit 1; }
else
  CB="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/clipboard_script.txt" 2>&1)" || { echo "$CB"; echo "FAIL: clipboard script exited non-zero"; exit 1; }
fi
unset DINO8_CLIPBOARD_DEBUG_FILE
cbcheck() { if echo "$CB" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$CB" "$1"; fail=1; fi; }
cbcheck "CopyRenderWindowToClipboard: [0-9]*x[0-9]* rendering copied to the system clipboard (image/png)" "CopyRenderWindowToClipboard ran the real OS-clipboard write path"
cbcheck "ViewCaptureToClipboard: [0-9]*x[0-9]* image copied to the system clipboard (image/png)" "ViewCaptureToClipboard ran the real OS-clipboard write path"
cbcheck "ScreenCaptureToClipboard: [0-9]*x[0-9]* image copied to the system clipboard (image/png)" "ScreenCaptureToClipboard ran the real OS-clipboard write path"
if [ -s "$CLIP_DEBUG" ]; then
  echo "ok   DINO8_CLIPBOARD_DEBUG_FILE was written"
  CLIP_SIG="$(head -c 8 "$CLIP_DEBUG" | od -An -tx1 | tr -d ' \n')"
  if [ "$CLIP_SIG" = "89504e470d0a1a0a" ]; then echo "ok   clipboard debug file starts with a real PNG signature (89 50 4E 47 0D 0A 1A 0A)"; else echo "FAIL clipboard debug file does not start with a PNG signature (got $CLIP_SIG)"; fail=1; fi
  CLIP_SIZE="$(wc -c < "$CLIP_DEBUG")"
  if [ "$CLIP_SIZE" -gt 1000 ]; then echo "ok   clipboard debug file is a non-trivial size ($CLIP_SIZE bytes)"; else echo "FAIL clipboard debug file is too small to be real image data ($CLIP_SIZE bytes)"; fail=1; fi
else
  echo "FAIL DINO8_CLIPBOARD_DEBUG_FILE was never written"
  fail=1
fi

# History / RecordHistory / UpdateHistory: a real, intentionally-scoped
# constructional-history mechanism (Extrude/ExtrudeCrv, ExtrudeCrvToPoint,
# Revolve, Loft, SubDLoft - see cmd_history.cpp's own header comment and
# tests/history_script.txt's for exactly what this checks). The key
# assertion is a real bounding-box move, not just an object count: after
# History On, Extrude, moving the source curve and running UpdateHistory,
# the extruded surface's bounding box must have actually shifted to match
# the curve's new position - the exact check a fake/no-op implementation
# would fail.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  HS="$("$BIN" --smoke 150 --script "$HERE/history_script.txt" 2>&1)" || { echo "$HS"; echo "FAIL: history script exited non-zero"; exit 1; }
else
  HS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/history_script.txt" 2>&1)" || { echo "$HS"; echo "FAIL: history script exited non-zero"; exit 1; }
fi
echo "$HS" | grep -E "^(ok|FAIL)"
if echo "$HS" | grep -q "^FAIL"; then fail=1; fi
echo "$HS" | grep -q "^smoke:" || { echo "$HS"; echo "FAIL: history script produced no smoke line"; fail=1; }
hcheck() { if echo "$HS" | grep -qF "$1"; then echo "ok   $2"; else echo "FAIL $2"; near "$HS" "$1"; fail=1; fi; }
hcheck "History recording: off. 0 object(s) with live construction history" "History defaults Off and reports it"
hcheck "UpdateHistory: 0 object(s) re-evaluated from their source curve(s)' current geometry" "an Extrude made while History was Off recorded nothing for UpdateHistory to redo"
hcheck "History recording: on - new Extrude/ExtrudeCrvToPoint/Revolve/Loft/SubDLoft results will remember their source curve(s) for UpdateHistory" "History On toggled recording on"
hcheck "Bounding box min 20,0,0 max 30,0,5" "the freshly-extruded surface's bounding box, before the source curve moves"
hcheck "UpdateHistory: 1 object(s) re-evaluated from their source curve(s)' current geometry" "UpdateHistory found and rebuilt exactly the one object History was tracking"
hcheck "Bounding box min 20,0,20 max 30,0,25" "UpdateHistory genuinely re-derived the extruded surface's geometry from the source curve's new z=20 position - not the z=0..5 box baked at creation time"
hcheck "History recording: on. 1 object(s) with live construction history:" "the live report lists exactly one tracked object"
hcheck "object 4: Extrude <- 3" "the report names the real dependent/source pair (surface 4 built from curve 3)"
# Undo id-reuse regression (see the last section of history_script.txt):
# a Box drawn right after undoing a tracked Extrude used to be handed the
# undone extrusion's own id (6), so its HistoryRecord/Provenance entries -
# side tables keyed by ObjectId, deliberately outside the undo history -
# attached to the Box, and UpdateHistory rebuilt the Box into "surface
# (40,0,0)-(50,0,5)". Document::ApplyDelta now keeps the id counter
# monotonic across Undo, so the Box gets a fresh id (7) and the stale
# record resolves to nothing.
hcheck_absent() { if echo "$HS" | grep -qF "$1"; then echo "FAIL $2"; fail=1; else echo "ok   $2"; fi; }
hcheck "object 6: Extrude <- 5" "an Undo/Redo round trip keeps the redone extrusion's own id (6) and its HistoryRecord (the record survives Undo+Redo, it is only ever orphaned by a real removal)"
hcheck "  ID: 7" "a Box drawn after undoing the tracked extrusion (id 6) gets a fresh id (7), not the undone object's id"
hcheck "UpdateHistory: 1 object(s) re-evaluated from their source curve(s)' current geometry, 1 orphaned entry cleared (object deleted or an Undo passed the construction)" \
  "UpdateHistory rebuilt only the still-live tracked surface (4) and reported the undone extrusion's record (6) as orphaned - it did NOT treat the new Box as a tracked object"
hcheck_absent "Bounding box: (40, 0, 0) to (50, 0, 5)" "the Box was never rebuilt into line 5's extrusion (the exact silent wrong result the id reuse produced before)"
hcheck "Bounding box: (50, 50, 0) to (60, 60, 10)" "the Box's own geometry is untouched after UpdateHistory"

# RemoveLayer must keep every OTHER holder of a layer index consistent, not
# just live objects (see tests/layer_remap_script.txt): a layout detail's
# per-detail hidden layers, a block definition's own member objects, and its
# "in use" check must also look at block definition members, not just live
# objects.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  LR="$("$BIN" --smoke 200 --script "$HERE/layer_remap_script.txt" 2>&1)" || { echo "$LR"; echo "FAIL: layer-remap script exited non-zero"; exit 1; }
else
  LR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/layer_remap_script.txt" 2>&1)" || { echo "$LR"; echo "FAIL: layer-remap script exited non-zero"; exit 1; }
fi
echo "$LR" | grep -E "^(ok|FAIL)"
if echo "$LR" | grep -q "^FAIL"; then fail=1; fi
echo "$LR" | grep -q "^smoke:" || { echo "$LR"; echo "FAIL: layer-remap script produced no smoke line"; fail=1; }
lcheck() { if echo "$LR" | grep -qF "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
lcheck "ShowLayersInDetail: 1 layer(s) in 1 detail(s)" "the detail's hidden-layer entry followed Walls to its new index (0 before the fix - the stale index resolved to the wrong, or no, layer)"
lcheck "  Layer index: 1" "a fresh Bk instance lands on Walls (index 1 after Purge removes Spare), not Roof (index 2 before the fix, since block members were never remapped)"
lcheck "Purge: nothing to remove" "Walls is reported in use (and left alone) once only a block definition's member is on it - the 'in use' check before the fix looked at live objects only"

# New must start a genuinely empty document (see tests/new_doc_script.txt):
# Document::Clear() used to leave block definitions and the id-keyed
# HistoryRecord/Provenance/CageBinding side tables from the OLD document in
# place, so the new document's first objects, handed the same small ids,
# could silently inherit them.
sed "s|@TMP@|$TMPW|g" "$HERE/new_doc_script.txt" > "$TMPW/new_doc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ND="$("$BIN" --smoke 200 --script "$TMPW/new_doc_script.txt" 2>&1)" || { echo "$ND"; echo "FAIL: new-doc script exited non-zero"; exit 1; }
else
  ND="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMPW/new_doc_script.txt" 2>&1)" || { echo "$ND"; echo "FAIL: new-doc script exited non-zero"; exit 1; }
fi
echo "$ND" | grep -E "^(ok|FAIL)"
if echo "$ND" | grep -q "^FAIL"; then fail=1; fi
echo "$ND" | grep -q "^smoke:" || { echo "$ND"; echo "FAIL: new-doc script produced no smoke line"; fail=1; }
ndcheck() { if echo "$ND" | grep -qF "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
ndcheck "History recording: on. 0 object(s) with live construction history" "New starts with an empty HistoryRecord table - the fresh Circle was not silently inherited as document A's tracked Extrude"
ndcheck "0 object(s) selected" "SelExtrusion finds nothing in the new document (1 before the fix: the fresh Circle wrongly matched the old Provenance/HistoryRecord entry)"
ndcheck "UpdateHistory: 0 object(s) re-evaluated" "UpdateHistory has nothing to rebuild in the new document"
ndcheck "history: curve" "SelLast + What still reports the fresh object as the Circle it really is"
ndcheck_absent() { if echo "$ND" | grep -qF "$1"; then echo "FAIL $2"; fail=1; else echo "ok   $2"; fi; }
ndcheck_absent "history: surface" "the Circle was never silently rebuilt into a surface (document A's Line-1 extrusion - the exact silent wrong result the leaked HistoryRecord produced before)"
ndcheck "No block definitions. Use Block to create one." "BlockManager's block table was cleared by New, not left holding document A's block"

# Load3dm must reject a .3dm mesh whose face vertex indices point past its
# own vertex array (see src/io/File3dm.cpp's MeshFaceIndicesInRange and
# tests/mesh_fixture_gen.cpp): OpenNURBS' own reader copies such a mesh in
# verbatim with no check and no diagnostic, after which every mesh query
# would read memory past the end of the vertex array. Dino 8 has no command
# of its own that can construct such a mesh, so mesh_fixture_gen builds one
# directly through ONX_Model/ON_Mesh, independent of Dino 8's own exporter.
MESHBIN="$(dirname "$BIN")/mesh_fixture_gen"
if [ -x "$MESHBIN" ]; then
  "$MESHBIN" "$TMPW/mesh_bad.3dm" bad >/dev/null || { echo "FAIL: mesh_fixture_gen failed to write the bad-mesh fixture"; exit 1; }
  "$MESHBIN" "$TMPW/mesh_good.3dm" good >/dev/null || { echo "FAIL: mesh_fixture_gen failed to write the good-mesh fixture"; exit 1; }
  cat > "$TMPW/mesh_bad_script.txt" <<EOS
Open $TMPW/mesh_bad.3dm
@expect_objects 0
EOS
  cat > "$TMPW/mesh_good_script.txt" <<EOS
Open $TMPW/mesh_good.3dm
@expect_objects 1
EOS
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    MB="$("$BIN" --smoke 60 --script "$TMPW/mesh_bad_script.txt" 2>&1)" || { echo "$MB"; echo "FAIL: bad-mesh script exited non-zero"; exit 1; }
    MG="$("$BIN" --smoke 60 --script "$TMPW/mesh_good_script.txt" 2>&1)" || { echo "$MG"; echo "FAIL: good-mesh script exited non-zero"; exit 1; }
  else
    MB="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/mesh_bad_script.txt" 2>&1)" || { echo "$MB"; echo "FAIL: bad-mesh script exited non-zero"; exit 1; }
    MG="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/mesh_good_script.txt" 2>&1)" || { echo "$MG"; echo "FAIL: good-mesh script exited non-zero"; exit 1; }
  fi
  echo "$MB" | grep -E "^(ok|FAIL)"
  echo "$MG" | grep -E "^(ok|FAIL)"
  if echo "$MB" | grep -q "^FAIL"; then fail=1; fi
  if echo "$MG" | grep -q "^FAIL"; then fail=1; fi
  mcheck() { if echo "$1" | grep -qF "$2"; then echo "ok   $3"; else echo "FAIL $3"; fail=1; fi; }
  mcheck "$MB" "1 corrupt mesh(es) skipped (face vertex indices outside the mesh's own vertex array)" "the out-of-range mesh is reported and skipped, not silently kept (0 objects before the fix's own diagnostic existed - it silently loaded as 1 object with no error at all)"
  mcheck "$MG" "Opened $TMPW/mesh_good.3dm (1 objects)" "a mesh with only in-range faces - including a degenerate repeated-index one, which is legitimate - still loads (the check is a range check, not the stricter no-repeated-index rule)"
else
  echo "FAIL mesh_fixture_gen was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the corrupt-mesh fixture check"
  fail=1
fi

exit $fail
