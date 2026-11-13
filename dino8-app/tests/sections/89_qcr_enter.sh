# QCRE_: every registered command survives "Enter, 3, Enter" and "Radius=1 3 / 3 / Enter / Enter" typed at its FIRST prompt, with nothing
# selected and with a box + circle + line selected (the QCR_ section found Boss, Rib, Offset, OffsetMultiple, TweenCurves and Leader dying with a
# segmentation fault on exactly this: OnText turned the number into OnNumber while the command still waited for its objects or points).
# The command names come from data/commands.json (minus the names that quit the program, open dialogs, run programs, touch the network or files).
# The names are cut into chunks of 30; every chunk is one app process under timeout. A chunk that dies is reported with the command that was
# running (the last QCREBEGIN_ marker), and the rest of the chunk is run again from the next command, so one crash hides nothing.
# The check is only "the app lives": there is no expectation on what each command prints.
QCRE_TMO="${QCRE_TMO:-240}"
QCRE_DIR="$TMPW/qcre"
mkdir -p "$QCRE_DIR"
QCRE_PLAIN=0
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then QCRE_PLAIN=1; fi
python3 - "$HERE/../data/commands.json" "$QCRE_DIR/names.txt" <<'PY'
import json, re, sys
bad = re.compile(r'(exit|quit|run|shell|script|python|lua|lisp|open|import|save|export|delete|purge|web|plugin|install|download|upload|collab|share|cloud|^ai|print|macro|record|clipboard|paste|browser|login|license|update|restart|crash|reset|new|undo|redo|repeat|insert|attach|link|mail|send|publish|capture|screenshot|render|animat|wait|sleep|test|debug|log|help|alias|toolbar|panel|command|options|setting|prefer|sandbox|session|workspace|template|load|unload|reload|watch|sync|speech|voice|dictate|bench|stress|spin|loop)', re.I)
names = [c['name'] for c in json.load(open(sys.argv[1]))]
open(sys.argv[2], 'w').write('\n'.join(n for n in names if n.isalnum() and not bad.search(n)) + '\n')
PY
qcre_script() {  # qcre_script <variant> <file> <names...>
  local variant="$1" out="$2" c; shift 2
  printf 'SetActiveViewport Top\nNew\nBox 0,0,0 10,10,0 10\nCircle 5,5,10 2\nLine 20,20,0 40,25,0\n' > "$out"
  for c in "$@"; do
    if [ "$variant" = empty ]; then printf 'SelNone\nQCREBEGIN_%s\n%s\nEnter\n3\nEnter\nCancel\nCancel\n' "$c" "$c" >> "$out"
    else printf 'SelAll\nQCREBEGIN_%s\n%s Radius=1 3\n3\nEnter\nEnter\nCancel\nCancel\n' "$c" "$c" >> "$out"; fi
  done
}
qcre_run() {  # qcre_run <script> <outfile>; returns the app's exit status
  local rc=0
  if [ "$QCRE_PLAIN" = 1 ]; then XDG_CONFIG_HOME="$QCRE_DIR/cfg" timeout "$QCRE_TMO" "$BIN" --smoke 30 --script "$1" >"$2" 2>&1 || rc=$?
  else XDG_CONFIG_HOME="$QCRE_DIR/cfg" timeout "$QCRE_TMO" xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$1" >"$2" 2>&1 || rc=$?; fi
  return $rc
}
mapfile -t QCRE_ALL < "$QCRE_DIR/names.txt"
for QCRE_VARIANT in empty presel; do
  QCRE_BAD=0
  QCRE_I=0
  while [ "$QCRE_I" -lt "${#QCRE_ALL[@]}" ]; do
    QCRE_CHUNK=("${QCRE_ALL[@]:$QCRE_I:30}")
    QCRE_I=$((QCRE_I + 30))
    QCRE_TRIES=0
    while [ "${#QCRE_CHUNK[@]}" -gt 0 ] && [ "$QCRE_TRIES" -lt 8 ]; do
      QCRE_TRIES=$((QCRE_TRIES + 1))
      qcre_script "$QCRE_VARIANT" "$QCRE_DIR/c.txt" "${QCRE_CHUNK[@]}"
      QCRE_RC=0
      qcre_run "$QCRE_DIR/c.txt" "$QCRE_DIR/c.out" || QCRE_RC=$?
      if [ "$QCRE_RC" = 0 ]; then break; fi
      QCRE_LAST="$({ grep -o 'QCREBEGIN_[A-Za-z0-9]*' "$QCRE_DIR/c.out" || true; } | tail -1 | sed 's/QCREBEGIN_//')"
      if [ "$QCRE_RC" = 124 ]; then echo "FAIL QCRE_${QCRE_VARIANT}_${QCRE_LAST:-unknown}: the app hung (no exit in ${QCRE_TMO} s) on Enter / a number at the first prompt"; else echo "FAIL QCRE_${QCRE_VARIANT}_${QCRE_LAST:-unknown}: the app exited with status $QCRE_RC on Enter / a number at the first prompt"; fi
      fail=1; QCRE_BAD=$((QCRE_BAD + 1))
      [ -n "$QCRE_LAST" ] || break
      # continue with the commands after the one that died
      QCRE_NEXT=()
      QCRE_SEEN=0
      for c in "${QCRE_CHUNK[@]}"; do
        if [ "$QCRE_SEEN" = 1 ]; then QCRE_NEXT+=("$c"); fi
        if [ "$c" = "$QCRE_LAST" ]; then QCRE_SEEN=1; fi
      done
      QCRE_CHUNK=("${QCRE_NEXT[@]}")
    done
  done
  if [ "$QCRE_BAD" = 0 ]; then echo "ok   QCRE_${QCRE_VARIANT}: ${#QCRE_ALL[@]} commands survived Enter / a number at the first prompt (nothing selected / box, circle, line selected)"; fi
done
