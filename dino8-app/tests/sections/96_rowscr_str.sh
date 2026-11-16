# ---------------------------------------------------------------------------
# ROWSCR_STR: string and symbol functions of the AutoLISP-style dialect (src/script/Lisp.cpp): vl-string-elt, vl-string-mismatch, vl-string-translate, vl-symbol-name.
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0357, -0359, -0364, -0366.
# The oracle is the documented behaviour of each function (Autodesk AutoLISP reference, the URL is in each Register row), worked out
# by hand: values from the vendor pages (examples quoted in each description) and worked out by hand from the definition of each function
# One real app process runs rowscr_str_script.txt, the section reads each result from the "Lisp: <value>" line printed after the typed expression.
# Self-contained (own helpers) so that `run_sections.sh ROWSCR_STR` works alone.
# ---------------------------------------------------------------------------
rss_exec() {  # rss_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
rss_ok() { echo "ok   $1"; }
rss_bad() { echo "FAIL $1"; fail=1; }
rss_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | RSS_CMD="Command: $2" RSS_NTH="$3" awk 'BEGIN { n = ENVIRON["RSS_NTH"] + 0; cmd = ENVIRON["RSS_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
rss_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(rss_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then rss_ok "$5 ($2 -> $got)"; else rss_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
rss_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(rss_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then rss_ok "$5"; else rss_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}


if [ "$IS_WINDOWS" = 1 ]; then RSS_SUB="Dino8"; else RSS_SUB="dino8"; fi
RSS_CFG="$TMP/rss_cfg"
mkdir -p "$RSS_CFG/$RSS_SUB"
RSS="$(rss_exec "$RSS_CFG" "$HERE/rowscr_str_script.txt")" || { echo "$RSS" | tail -20; rss_bad "ROWSCR_STR script exited non-zero"; }
grep -q "^smoke:" <<<"$RSS" || { echo "$RSS" | tail -20; rss_bad "ROWSCR_STR: no smoke line (the app did not reach its normal end)"; }

# ---- vl-string-elt, row D8-AUTOCAD-LISP-0357 -------------------------------------------------------------
rss_is "$RSS" '(vl-string-elt "abc" 1)' 1 "98" "RSS_elt: position 1 of abc is b, code 98"
rss_is "$RSS" '(vl-string-elt "abc" 0)' 1 "97" "RSS_elt: the first character a is 97"
rss_is "$RSS" '(vl-string-elt "abc" 2)' 1 "99" "RSS_elt: the last character c is 99"
rss_is "$RSS" '(vl-string-elt "A" 0)' 1 "65" "RSS_elt: capital A is 65"
rss_err "$RSS" '(vl-string-elt "abc" 5)' 1 'outside the string' "RSS_elt_neg: position 5 of a three-character string raises an error"
rss_err "$RSS" '(vl-string-elt "abc" 3)' 1 'outside the string' "RSS_elt_neg: the position one past the end"
rss_err "$RSS" '(vl-string-elt "abc" -1)' 1 'outside the string' "RSS_elt_neg: a negative position"
rss_err "$RSS" '(vl-string-elt 5 0)' 1 'bad argument type' "RSS_elt_neg: not a string"

# ---- vl-string-mismatch, row D8-AUTOCAD-LISP-0359 --------------------------------------------------------
rss_is "$RSS" '(vl-string-mismatch "abcdef" "abcxyz")' 1 "3" "RSS_mismatch: abc is shared, d and x differ"
rss_is "$RSS" '(vl-string-mismatch "VL-FILE-DIRECTORY-P" "VL-FILE-SIZE")' 1 "8" "RSS_mismatch: VL-FILE- is eight characters"
rss_is "$RSS" '(vl-string-mismatch "ABC" "abcd" 0 0 T)' 1 "3" "RSS_mismatch: ignoring case ABC matches abc"
rss_is "$RSS" '(vl-string-mismatch "ABC" "abcd")' 1 "0" "RSS_mismatch: case sensitive by default, A differs from a"
rss_is "$RSS" '(vl-string-mismatch "xxabcd" "abcz" 2 0)' 1 "3" "RSS_mismatch: starting at position 2 of the first string, abc is shared"
rss_is "$RSS" '(vl-string-mismatch "abc" "xxabc" 0 2)' 1 "3" "RSS_mismatch: starting at position 2 of the second string"
rss_is "$RSS" '(vl-string-mismatch "abc" "abc")' 1 "3" "RSS_mismatch: identical strings share all three"
rss_is "$RSS" '(vl-string-mismatch "" "abc")' 1 "0" "RSS_mismatch: an empty string shares nothing"
rss_is "$RSS" '(vl-string-mismatch "abc" "xbc")' 1 "0" "RSS_mismatch: the first characters differ"
rss_is "$RSS" '(vl-string-mismatch "ab" "abcd")' 1 "2" "RSS_mismatch: the shorter string ends the count"
rss_err "$RSS" '(vl-string-mismatch "abc" "abc" 9 0)' 1 'bad argument value' "RSS_mismatch_neg: start position past the end"
rss_err "$RSS" '(vl-string-mismatch "abc")' 1 'too few arguments' "RSS_mismatch_neg: one string"

# ---- vl-string-translate, row D8-AUTOCAD-LISP-0364 -------------------------------------------------------
rss_is "$RSS" '(vl-string-translate "abc" "xyz" "aabbcc")' 1 "\"xxyyzz\"" "RSS_translate: the documented example"
rss_is "$RSS" '(vl-string-translate "abc" "xyz" "cabbage-d")' 1 "\"zxyyxge-d\"" "RSS_translate: other characters are left alone"
rss_is "$RSS" '(vl-string-translate "abc" "xyz" "hello")' 1 "\"hello\"" "RSS_translate: nothing in the source set, nothing changes"
rss_is "$RSS" '(vl-string-translate "xyz" "abc" "")' 1 "\"\"" "RSS_translate: an empty string stays empty"
rss_is "$RSS" '(vl-string-translate "." "_" "a.b.c")' 1 "\"a_b_c\"" "RSS_translate: dots become underscores"
rss_err "$RSS" '(vl-string-translate "abc" "xyz")' 1 'too few arguments' "RSS_translate_neg: two arguments"
rss_err "$RSS" '(vl-string-translate 1 "xyz" "abc")' 1 'bad argument type' "RSS_translate_neg: the source set is not a string"

# ---- vl-symbol-name, row D8-AUTOCAD-LISP-0366 ------------------------------------------------------------
rss_is "$RSS" '(vl-symbol-name '\''my-var)' 1 "\"MY-VAR\"" "RSS_symname: the name is upper case"
rss_is "$RSS" '(vl-symbol-name '\''a1)' 1 "\"A1\"" "RSS_symname: a symbol with a digit"
rss_is "$RSS" '(vl-symbol-name '\''t)' 1 "\"T\"" "RSS_symname: the symbol T"
rss_err "$RSS" '(vl-symbol-name 1)' 1 'bad argument type: symbolp' "RSS_symname_neg: a number is not a symbol"
rss_err "$RSS" '(vl-symbol-name "abc")' 1 'bad argument type: symbolp' "RSS_symname_neg: a string is not a symbol"
rss_err "$RSS" '(vl-symbol-name)' 1 'too few arguments' "RSS_symname_neg: no argument"
