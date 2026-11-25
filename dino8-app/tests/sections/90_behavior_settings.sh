# ---------------------------------------------------------------------------
# BHV_: behaviour options persist across sessions, and the suite keeps its scripts from leaking them into each other.
# Before this, DragMode, DragStrength, DragCopy, RememberCopyOptions, OrthoAngle, SetZoomExtentsBorder, Alerter, DigBeep, ToolbarLock,
# CommandPrompt, ViewportTabs, CreaseSplitting and DecimalPoint all reset on every start (only snaps, panels, theme, ... were saved).
# settings.json now carries a "behavior" group. Because tests/smoke.sh runs hundreds of scripts that share one config directory and
# several of them leave these options changed (state_script2.txt leaves DragStrength 50, ToolbarLock on, OrthoAngle 45, ...), the
# harness exports DINO8_ISOLATE_BEHAVIOR_SETTINGS=1, which keeps the group out of settings.json in both directions; this section
# runs its own launches without it, in its own config home. Expected values are the (non-default) values the script sets:
#   launch 1 (no isolation): set every option; on exit settings.json must hold exactly those values
#   launch 2 (new process, same config): OptionsExport right at start shows them again, and SetZoomExtentsBorder (no argument)
#            reports the restored 1.2 with a decimal COMMA (DecimalPoint Comma was restored too): "1,2"
#   launch 3 (DINO8_ISOLATE_BEHAVIOR_SETTINGS=1, same config that still holds the values): they are NOT read (the border is the
#            built-in 1.05) and OptionsExport / the saved settings.json carry no "behavior" group
# ---------------------------------------------------------------------------
bhv_ok() { echo "ok   $1"; }
bhv_bad() { echo "FAIL $1"; fail=1; }
if [ "$IS_WINDOWS" = 1 ]; then
  echo "skip BHV_all (not run on Windows)"
else
  BHV_DIR="$TMPW/bhv"; BHV_CFG="$BHV_DIR/config"; mkdir -p "$BHV_CFG"
  cat > "$BHV_DIR/l1.txt" <<EOS
@wait 3
DragMode World
DragStrength 55
DragCopy
RememberCopyOptions
OrthoAngle 45
SetZoomExtentsBorder 1.2
Alerter
DigBeep
ToolbarLock
CommandPrompt
ViewportTabs
CreaseSplitting Off
DecimalPoint Comma
EOS
  cat > "$BHV_DIR/l2.txt" <<EOS
@wait 3
OptionsExport $BHV_DIR/b.json
BHVBEGIN_border
SetZoomExtentsBorder
BHVBEGIN_end
EOS
  cat > "$BHV_DIR/l3.txt" <<EOS
@wait 3
OptionsExport $BHV_DIR/c.json
BHVBEGIN_border
SetZoomExtentsBorder
BHVBEGIN_end
EOS
  bhv_run() {  # bhv_run <script> <isolate 0|1>
    local iso=()
    if [ "$2" = 1 ]; then iso=(env DINO8_ISOLATE_BEHAVIOR_SETTINGS=1); else iso=(env -u DINO8_ISOLATE_BEHAVIOR_SETTINGS); fi
    if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
      XDG_CONFIG_HOME="$BHV_CFG" HOME="$BHV_CFG" "${iso[@]}" timeout 600 "$BIN" --smoke 30 --script "$1" 2>&1
    else
      XDG_CONFIG_HOME="$BHV_CFG" HOME="$BHV_CFG" "${iso[@]}" timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$1" 2>&1
    fi
  }
  BHV_L1="$(bhv_run "$BHV_DIR/l1.txt" 0)" || { echo "$BHV_L1" | tail -20; bhv_bad "BHV_run1: app exited non-zero"; BHV_L1=""; }
  BHV_SET1="$(cat "$BHV_CFG/dino8/settings.json" 2>/dev/null || true)"
  BHV_L2="$(bhv_run "$BHV_DIR/l2.txt" 0)" || { echo "$BHV_L2" | tail -20; bhv_bad "BHV_run2: app exited non-zero"; BHV_L2=""; }
  BHV_B="$(cat "$BHV_DIR/b.json" 2>/dev/null || true)"
  BHV_L3="$(bhv_run "$BHV_DIR/l3.txt" 1)" || { echo "$BHV_L3" | tail -20; bhv_bad "BHV_run3: app exited non-zero"; BHV_L3=""; }
  BHV_C="$(cat "$BHV_DIR/c.json" 2>/dev/null || true)"
  BHV_SET3="$(cat "$BHV_CFG/dino8/settings.json" 2>/dev/null || true)"

  bhv_json() { if grep -qF -- "$2" <<<"${!1}"; then bhv_ok "$3"; else bhv_bad "$3"; echo "     expected: $2"; { grep -F '"behavior"' <<<"${!1}" || true; } | head -1 | sed 's/^/     actual:   /'; fi; }
  bhv_block() { awk -v name="$1" '$0 ~ ("Unknown command: BHVBEGIN_" name "$") { on = 1; next } on && /Unknown command: BHVBEGIN_/ { exit } on { print }' <<<"$2"; }
  # the persisted values, in the order the writer emits them
  BHV_WANT=('"command_prompt": false' '"alerter": true' '"drag_mode": "World"' '"drag_strength": 55' '"drag_copy": true' '"remember_copy_options": true'
            '"ortho_angle": 45' '"zoom_extents_border": 1.2' '"dig_beep": true' '"toolbar_lock": true' '"crease_splitting": false'
            '"viewport_tabs": false' '"decimal_comma": true')
  for w in "${BHV_WANT[@]}"; do bhv_json BHV_SET1 "$w" "BHV_persist: after launch 1 settings.json holds $w"; done
  for w in "${BHV_WANT[@]}"; do bhv_json BHV_B "$w" "BHV_restart: a new process starts with $w (OptionsExport at start-up)"; done
  if grep -qF "current border factor 1,2 (" <<<"$(bhv_block border "$BHV_L2")"; then bhv_ok "BHV_restart: SetZoomExtentsBorder reports 1,2 - the border and the decimal comma were both restored"; else bhv_bad "BHV_restart: SetZoomExtentsBorder did not report 1,2"; bhv_block border "$BHV_L2" | head -3 | sed 's/^/     actual: /'; fi
  if grep -qF "current border factor 1.05 (" <<<"$(bhv_block border "$BHV_L3")"; then bhv_ok "BHV_isolate: with DINO8_ISOLATE_BEHAVIOR_SETTINGS=1 the saved 1.2 is not read (built-in 1.05, decimal point)"; else bhv_bad "BHV_isolate: isolation did not keep the saved border out"; bhv_block border "$BHV_L3" | head -3 | sed 's/^/     actual: /'; fi
  if [ -n "$BHV_C" ] && ! grep -qF '"behavior"' <<<"$BHV_C" && [ -n "$BHV_SET3" ] && ! grep -qF '"behavior"' <<<"$BHV_SET3"; then bhv_ok "BHV_isolate: an isolated run writes no behavior group (OptionsExport nor settings.json)"; else bhv_bad "BHV_isolate: an isolated run still wrote a behavior group"; fi
fi
