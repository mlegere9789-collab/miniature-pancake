# ---------------------------------------------------------------------------
# RW2SCR_STR: wcmatch, snvalid and vl-filename-* of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0374, -0277, -0330, -0331, -0332.
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic (their own printf, integer and floating point), never with the
# interpreter under test. One real app process runs rw2scr_str_script.txt; each result is read from the "Lisp: <value>" line
# printed after the typed expression. Self-contained (own helpers) so that `run_sections.sh RW2SCR_STR` works alone.
# ---------------------------------------------------------------------------

r2t_exec() {  # r2t_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r2t_ok() { echo "ok   $1"; }
r2t_bad() { echo "FAIL $1"; fail=1; }
r2t_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | R2T_CMD="Command: $2" R2T_NTH="$3" awk 'BEGIN { n = ENVIRON["R2T_NTH"] + 0; cmd = ENVIRON["R2T_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r2t_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r2t_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r2t_ok "$5 ($2 -> $got)"; else r2t_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r2t_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r2t_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r2t_ok "$5"; else r2t_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r2t_r() {  # a real the way the app prints it, from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}
r2t_f() {  # <printf format> <awk expression>: a formatted number
  awk -v PI=3.14159265358979323846 -v fmt="$1" "BEGIN { printf fmt, $2 }"
}
r2t_case() {  # a file name as the app prints it: backslashes doubled, quoted, upper case on Windows only
  local s="$1"
  [ "$IS_WINDOWS" = 1 ] && s="$(tr '[:lower:]' '[:upper:]' <<<"$s")"
  s="${s//\\/\\\\}"
  echo "\"$s\""
}

if [ "$IS_WINDOWS" = 1 ]; then R2T_SUB="Dino8"; else R2T_SUB="dino8"; fi
R2T_CFG="$TMP/r2t_cfg"
mkdir -p "$R2T_CFG/$R2T_SUB"
R2T="$(r2t_exec "$R2T_CFG" "$HERE/rw2scr_str_script.txt")" || { echo "$R2T" | tail -20; r2t_bad "RW2SCR_STR script exited non-zero"; }
grep -q "^smoke:" <<<"$R2T" || { echo "$R2T" | tail -20; r2t_bad "RW2SCR_STR: no smoke line (the app did not reach its normal end)"; }

# ---- wcmatch (D8-AUTOCAD-LISP-0374): case-sensitive wildcard patterns ------------------------------------
r2t_is "$R2T" '(wcmatch "ASHADE" "A*")' 1 T 'R2T_wcmatch: * matches the rest (documented)'
r2t_is "$R2T" '(wcmatch "ASHADE" "~A*")' 1 nil 'R2T_wcmatch: a leading ~ negates (documented)'
r2t_is "$R2T" '(wcmatch "ab1" "@@#")' 1 T 'R2T_wcmatch: @ is a letter and # a digit'
r2t_is "$R2T" '(wcmatch "ashade" "A*")' 1 nil 'R2T_wcmatch: the match is case-sensitive'
r2t_is "$R2T" '(wcmatch "Wall-12" "W???-##")' 1 T 'R2T_wcmatch: ? is any one character'
r2t_is "$R2T" '(wcmatch "Wall-1" "W???-##")' 1 nil 'R2T_wcmatch: ## needs two digits'
r2t_is "$R2T" '(wcmatch "a-b" "a.b")' 1 T 'R2T_wcmatch: . is a non-alphanumeric character'
r2t_is "$R2T" '(wcmatch "a1b" "a.b")' 1 nil 'R2T_wcmatch: . is not a digit'
r2t_is "$R2T" '(wcmatch "cat" "[bcr]at")' 1 T 'R2T_wcmatch: a set'
r2t_is "$R2T" '(wcmatch "mat" "[bcr]at")' 1 nil 'R2T_wcmatch: not in the set'
r2t_is "$R2T" '(wcmatch "mat" "[~bcr]at")' 1 T 'R2T_wcmatch: a negated set'
r2t_is "$R2T" '(wcmatch "cat" "[~bcr]at")' 1 nil 'R2T_wcmatch: a negated set rejects its members'
r2t_is "$R2T" '(wcmatch "bog" "[a-c]og")' 1 T 'R2T_wcmatch: a range in a set'
r2t_is "$R2T" '(wcmatch "dog" "[a-c]og")' 1 nil 'R2T_wcmatch: outside the range'
r2t_is "$R2T" '(wcmatch "A*B" "A`*B")' 1 T 'R2T_wcmatch: ` makes the next character literal'
r2t_is "$R2T" '(wcmatch "AxB" "A`*B")' 1 nil 'R2T_wcmatch: an escaped * is not a wildcard'
r2t_is "$R2T" '(wcmatch "LINE" "CIRCLE,LINE")' 1 T 'R2T_wcmatch: comma separated alternatives'
r2t_is "$R2T" '(wcmatch "ARC" "CIRCLE,LINE")' 1 nil 'R2T_wcmatch: no alternative matches'
r2t_is "$R2T" '(wcmatch "" "*")' 1 T 'R2T_wcmatch: * matches the empty string'
r2t_is "$R2T" '(wcmatch "abc" "")' 1 nil 'R2T_wcmatch: the empty pattern matches only the empty string'
r2t_is "$R2T" '(wcmatch "abc" "abc")' 1 T 'R2T_wcmatch: a literal pattern'
r2t_is "$R2T" '(wcmatch "abcd" "abc")' 1 nil 'R2T_wcmatch: the whole string must match'
r2t_err "$R2T" '(wcmatch 5 "x")' 1 'bad argument type' 'R2T_wcmatch_neg: a number is not a string'
r2t_err "$R2T" '(wcmatch "x")' 1 'too few arguments' 'R2T_wcmatch_neg: one argument'

# ---- snvalid (D8-AUTOCAD-LISP-0277): symbol-table names --------------------------------------------------
r2t_is "$R2T" '(snvalid "Wall_1")' 1 T 'R2T_snvalid: letters, digits and underscore (documented)'
r2t_is "$R2T" '(snvalid "Wall<1")' 1 nil 'R2T_snvalid: < is not allowed (documented)'
r2t_is "$R2T" '(snvalid "a|b" 1)' 1 T 'R2T_snvalid: an embedded | with the flag (documented)'
r2t_is "$R2T" '(snvalid "a|b")' 1 nil 'R2T_snvalid: | without the flag (documented)'
r2t_is "$R2T" '(snvalid "|ab" 1)' 1 nil 'R2T_snvalid: | may not be first'
r2t_is "$R2T" '(snvalid "ab|" 1)' 1 nil 'R2T_snvalid: | may not be last'
r2t_is "$R2T" '(snvalid "")' 1 nil 'R2T_snvalid: the empty name'
r2t_is "$R2T" '(snvalid "Layer 1")' 1 T 'R2T_snvalid: spaces are allowed'
r2t_is "$R2T" '(snvalid "a$b-c_d")' 1 T 'R2T_snvalid: dollar, hyphen and underscore'
r2t_is "$R2T" '(snvalid "a<b")' 1 nil 'R2T_snvalid: the character < is not allowed'
r2t_is "$R2T" '(snvalid "a>b")' 1 nil 'R2T_snvalid: the character > is not allowed'
r2t_is "$R2T" '(snvalid "a/b")' 1 nil 'R2T_snvalid: the character / is not allowed'
r2t_is "$R2T" '(snvalid "a\\b")' 1 nil 'R2T_snvalid: the character backslash is not allowed'
r2t_is "$R2T" '(snvalid "a:b")' 1 nil 'R2T_snvalid: the character : is not allowed'
r2t_is "$R2T" '(snvalid "a;b")' 1 nil 'R2T_snvalid: the character ; is not allowed'
r2t_is "$R2T" '(snvalid "a?b")' 1 nil 'R2T_snvalid: the character ? is not allowed'
r2t_is "$R2T" '(snvalid "a*b")' 1 nil 'R2T_snvalid: the character * is not allowed'
r2t_is "$R2T" '(snvalid "a,b")' 1 nil 'R2T_snvalid: the character , is not allowed'
r2t_is "$R2T" '(snvalid "a=b")' 1 nil 'R2T_snvalid: the character = is not allowed'
r2t_is "$R2T" '(snvalid "a`b")' 1 nil 'R2T_snvalid: the character ` is not allowed'
r2t_err "$R2T" '(snvalid 5)' 1 'bad argument type' 'R2T_snvalid_neg: a number is not a string'

# ---- vl-filename-base, -directory, -extension (D8-AUTOCAD-LISP-0330, -0331, -0332): no file access, either separator ---
r2t_is "$R2T" '(vl-filename-base "c:\\acadwin\\acad.exe")' 1 "$(r2t_case acad)" 'R2T_vl-filename-base: path and extension are stripped (documented)'
r2t_is "$R2T" '(vl-filename-base "c:\\acadwin")' 1 "$(r2t_case acadwin)" 'R2T_vl-filename-base: a name without extension (documented)'
r2t_is "$R2T" '(vl-filename-base "/tmp/x/Plan.v2.dwg")' 1 "$(r2t_case Plan.v2)" 'R2T_vl-filename-base: only the last extension goes'
r2t_is "$R2T" '(vl-filename-base "noext")' 1 "$(r2t_case noext)" 'R2T_vl-filename-base: no directory, no extension'
r2t_is "$R2T" '(vl-filename-directory "c:\\acadwin\\template.txt")' 1 "$(r2t_case 'c:\acadwin')" 'R2T_vl-filename-directory: the folder (documented)'
r2t_is "$R2T" '(vl-filename-directory "template.txt")' 1 '""' 'R2T_vl-filename-directory: no path is the empty string (documented)'
r2t_is "$R2T" '(vl-filename-directory "/tmp/x/a.txt")' 1 "$(r2t_case /tmp/x)" 'R2T_vl-filename-directory: a slash path'
r2t_is "$R2T" '(vl-filename-extension "c:\\acadwin\\output.txt")' 1 "$(r2t_case .txt)" 'R2T_vl-filename-extension: the extension with its period (documented)'
r2t_is "$R2T" '(vl-filename-extension "output")' 1 nil 'R2T_vl-filename-extension: no extension is nil (documented)'
r2t_is "$R2T" '(vl-filename-extension "c:\\a.b\\output")' 1 nil 'R2T_vl-filename-extension: a period in a folder name is not an extension'
r2t_is "$R2T" '(vl-filename-extension "Plan.v2.DWG")' 1 "$(r2t_case .DWG)" 'R2T_vl-filename-extension: the last period starts it'
r2t_err "$R2T" '(vl-filename-base 5)' 1 'bad argument type' 'R2T_vl-filename_neg: a number is not a string'
r2t_err "$R2T" '(vl-filename-extension)' 1 'too few arguments' 'R2T_vl-filename_neg: no argument'
