# ---------------------------------------------------------------------------
# HLP_: "Help <command>" prints the command's description on the command line, in the active UI language.
# Before this, Help / CommandHelp only opened the Help panel (nothing reached the command history, so scripts, the command line
# and screen readers got nothing), and "Help Circle" typed after any other command was shown the PREVIOUS command's help because the
# typed name was dropped. Expected strings are read straight from the data files with python (data/i18n/<lang>.json "cmddesc.<name>",
# data/commands.json), never from the app:
#   fr / ja / ar : the translated description of a command that has a cmddesc.* key (Circle, Circle, Move)
#   de + Pipe    : Pipe has no cmddesc.* key, so the English catalog description is shown even under German
#   en + Circle  : the English catalog description;   its options line is the catalog's options joined with ", "
#   unknown name : a warning naming it;   "Help" alone after `Point 1,1,1` documents Point (the last command)
# ---------------------------------------------------------------------------
hlp_ok() { echo "ok   $1"; }
hlp_bad() { echo "FAIL $1"; fail=1; }
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip HLP_all (python3 not available)"
else
  HLP_DIR="$TMPW/hlp"; HLP_CFG="$HLP_DIR/config"; mkdir -p "$HLP_CFG"
  HLP_I18N="$HERE/../data/i18n"; HLP_CAT="$HERE/../data/commands.json"
  hlp_desc() { python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["cmddesc."+sys.argv[2]])' "$HLP_I18N/$1.json" "$2"; }
  hlp_cat() { python3 -I -c 'import json,sys
d={c["name"]:c for c in json.load(open(sys.argv[1], encoding="utf-8"))}
c=d[sys.argv[2]]
print(c["description"] if sys.argv[3]=="desc" else ", ".join(c["options"]))' "$HLP_CAT" "$1" "$2"; }
  HLP_FR="$(hlp_desc fr circle)"; HLP_JA="$(hlp_desc ja circle)"; HLP_AR="$(hlp_desc ar move)"
  HLP_PIPE="$(hlp_cat Pipe desc)"; HLP_CIRCLE="$(hlp_cat Circle desc)"; HLP_COPT="$(hlp_cat Circle opts)"; HLP_POINT="$(hlp_cat Point desc)"
  cat > "$HLP_DIR/s.txt" <<EOS
@wait 3
SetLanguage fr
HLPBEGIN_fr
Help Circle
SetLanguage ja
HLPBEGIN_ja
Help Circle
SetLanguage ar
HLPBEGIN_ar
Help Move
SetLanguage de
HLPBEGIN_de
Help Pipe
SetLanguage en
HLPBEGIN_en
Help Circle
HLPBEGIN_unknown
Help NoSuchCommandXyz
Point 1,1,1
HLPBEGIN_last
Help
HLPBEGIN_end
EOS
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    HLP_O="$(XDG_CONFIG_HOME="$HLP_CFG" HOME="$HLP_CFG" timeout 600 "$BIN" --smoke 30 --script "$HLP_DIR/s.txt" 2>&1)" || { echo "$HLP_O" | tail -20; hlp_bad "HLP_run: app exited non-zero"; HLP_O=""; }
  else
    HLP_O="$(XDG_CONFIG_HOME="$HLP_CFG" HOME="$HLP_CFG" timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HLP_DIR/s.txt" 2>&1)" || { echo "$HLP_O" | tail -20; hlp_bad "HLP_run: app exited non-zero"; HLP_O=""; }
  fi
  hlp_block() { awk -v name="$1" '$0 ~ ("Unknown command: HLPBEGIN_" name "$") { on = 1; next } on && /Unknown command: HLPBEGIN_/ { exit } on { print }' <<<"$HLP_O"; }
  hlp_has() { if grep -qxF -- "history: $2" <<<"$(hlp_block "$1")"; then hlp_ok "$3"; else hlp_bad "$3"; echo "     expected line: history: $2"; hlp_block "$1" | head -3 | sed 's/^/     actual:        /'; fi; }
  hlp_hasnot() { if grep -qF -- "$2" <<<"$(hlp_block "$1")"; then hlp_bad "$3"; else hlp_ok "$3"; fi; }

  hlp_has fr "Help: Circle - $HLP_FR" "HLP_fr: 'Help Circle' under French prints the French description from fr.json"
  hlp_has ja "Help: Circle - $HLP_JA" "HLP_ja: ... Japanese (and the typed name, not the previous command, is what is documented)"
  hlp_has ar "Help: Move - $HLP_AR" "HLP_ar: ... Arabic, for Move"
  hlp_has de "Help: Pipe - $HLP_PIPE" "HLP_de: a command outside the translated subset falls back to the English catalog text under German"
  hlp_has en "Help: Circle - $HLP_CIRCLE" "HLP_en: under English the catalog description"
  hlp_has en "Help: Circle options - $HLP_COPT" "HLP_en: ... then an options line listing the catalog's command-line options"
  hlp_has unknown "! Help: unknown command 'NoSuchCommandXyz'" "HLP_unknown: an unknown name is reported by name"
  hlp_has last "Help: Point - $HLP_POINT" "HLP_last: bare 'Help' documents the last command run (Point)"
  hlp_hasnot unknown "Help: Circle" "HLP_unknown: nothing of the previous help leaks into the unknown-command answer"
fi
