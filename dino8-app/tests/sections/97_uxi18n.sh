# ---------------------------------------------------------------------------
# UXI18N_: the UI language can be switched to each shipped language and the string table answers in that language.
# Rows D8-ARCHICAD-UI-0005 (Platform: localised versions and language switching) and D8-ARCHICAD-UI-0015 (Platform: localised UI).
# Oracle: the word for "File" (the first menu) in each language, written here from the languages themselves, not from the tables:
# de Datei, es Archivo, fr Fichier, it File, pt Arquivo, ja ファイル, ko 파일, ru Файл, zh 文件, ar ملف, en File; Arabic is the one
# right-to-left language of the eleven. The active language is also exported with OptionsExport and read back from the JSON file.
# Not tested (untested, no libraries or templates are localised here): localised object libraries and templates, and the layout of
# right-to-left text on screen.
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXI18N (not run on Windows: needs python3 and the Linux headless launch)"
else
UXI18N_S="$TMPW/uxi18n_script.txt"
rm -f "$TMPW/uxi18n_ja.json"
{
  echo "@wait 2"
  for l in de es fr it pt ja ko ru zh ar en; do echo "UXBEGIN_$l"; echo "SetLanguage $l"; echo "I18nSelfTest"; done
  echo "UXBEGIN_byname"; echo "SetLanguage Deutsch"; echo "I18nSelfTest"
  echo "UXBEGIN_unknown"; echo "SetLanguage Klingon"
  echo "UXBEGIN_export"; echo "SetLanguage ja"; echo "OptionsExport $TMPW/uxi18n_ja.json"
  echo "UXBEGIN_end"; echo "SetLanguage en"
} > "$UXI18N_S"
if ux_launch "$UXI18N_S"; then
  uxi18n_chk() {  # code word rtl
    ux_eq UXI18N_$1 "$1" "SetLanguage $1 switches the table: the File menu reads '$2', rtl=$3" \
      "SetLanguage: $1" "I18nSelfTest: active=$1" "I18nSelfTest: menu.file=$2" "I18nSelfTest: fallback(panel.imgui_demo)=$(ux_lines "$1" | sed -n 's/^I18nSelfTest: fallback(panel.imgui_demo)=//p')" \
      "I18nSelfTest: unknown_key=this.key.does.not.exist.anywhere" "I18nSelfTest: rtl=$3"
  }
  uxi18n_chk de Datei false
  uxi18n_chk es Archivo false
  uxi18n_chk fr Fichier false
  uxi18n_chk it File false
  uxi18n_chk pt Arquivo false
  uxi18n_chk ja ファイル false
  uxi18n_chk ko 파일 false
  uxi18n_chk ru Файл false
  uxi18n_chk zh 文件 false
  uxi18n_chk ar ملف true
  uxi18n_chk en File false
  ux_has UXI18N_byname byname "a language is also found by its own name (Deutsch)" "I18nSelfTest: menu.file=Datei"
  ux_has UXI18N_unknown unknown "an unknown language is refused and the available ones are listed" "SetLanguage: unknown language 'Klingon'. Available:"
  if [ -f "$TMPW/uxi18n_ja.json" ] && python3 - "$TMPW/uxi18n_ja.json" <<'PYEOF'
import json, sys
sys.exit(0 if json.load(open(sys.argv[1])).get("language") == "ja" else 1)
PYEOF
  then ux_ok "UXI18N_persist: the exported settings file carries language \"ja\""; else ux_bad "UXI18N_persist: the exported settings file does not carry language \"ja\""; fi
fi
fi
