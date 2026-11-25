# ---------------------------------------------------------------------------
# ROWSCR_LIST: list and symbol utilities of the AutoLISP-style dialect (src/script/Lisp.cpp): acad_strlsort, vl-consp, vl-list-length, vl-member-if-not, vl-sort-i.
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0099, -0315, -0336, -0340, -0355.
# The oracle is the documented behaviour of each function (Autodesk AutoLISP reference, the URL is in each Register row), worked out
# by hand: values from the vendor pages (examples quoted in each description) and worked out by hand from the definition of each function
# One real app process runs rowscr_list_script.txt, the section reads each result from the "Lisp: <value>" line printed after the typed expression.
# Self-contained (own helpers) so that `run_sections.sh ROWSCR_LIST` works alone.
# ---------------------------------------------------------------------------
rsl_exec() {  # rsl_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
rsl_ok() { echo "ok   $1"; }
rsl_bad() { echo "FAIL $1"; fail=1; }
rsl_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | RSL_CMD="Command: $2" RSL_NTH="$3" awk 'BEGIN { n = ENVIRON["RSL_NTH"] + 0; cmd = ENVIRON["RSL_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
rsl_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(rsl_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then rsl_ok "$5 ($2 -> $got)"; else rsl_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
rsl_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(rsl_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then rsl_ok "$5"; else rsl_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}


if [ "$IS_WINDOWS" = 1 ]; then RSL_SUB="Dino8"; else RSL_SUB="dino8"; fi
RSL_CFG="$TMP/rsl_cfg"
mkdir -p "$RSL_CFG/$RSL_SUB"
RSL="$(rsl_exec "$RSL_CFG" "$HERE/rowscr_list_script.txt")" || { echo "$RSL" | tail -20; rsl_bad "ROWSCR_LIST script exited non-zero"; }
grep -q "^smoke:" <<<"$RSL" || { echo "$RSL" | tail -20; rsl_bad "ROWSCR_LIST: no smoke line (the app did not reach its normal end)"; }

# ---- acad_strlsort, row D8-AUTOCAD-LISP-0099 -------------------------------------------------------------
rsl_is "$RSL" '(acad_strlsort '\''("Jan" "Feb" "Mar"))' 1 "(\"Feb\" \"Jan\" \"Mar\")" "RSL_strlsort: the documented example sorts alphabetically"
rsl_is "$RSL" '(acad_strlsort '\''("pear" "apple" "fig"))' 1 "(\"apple\" \"fig\" \"pear\")" "RSL_strlsort: three fruit in alphabetical order"
rsl_is "$RSL" '(acad_strlsort '\''("b" "a" "b" "a"))' 1 "(\"a\" \"a\" \"b\" \"b\")" "RSL_strlsort: duplicates are kept"
rsl_is "$RSL" '(acad_strlsort '\''("only"))' 1 "(\"only\")" "RSL_strlsort: one string is returned as it is"
rsl_is "$RSL" '(acad_strlsort '\''("a1" "a10" "a2"))' 1 "(\"a1\" \"a10\" \"a2\")" "RSL_strlsort: comparison is character by character, so a10 comes before a2"
rsl_is "$RSL" '(acad_strlsort nil)' 1 "nil" "RSL_strlsort_neg: an empty list gives nil"
rsl_is "$RSL" '(acad_strlsort 5)' 1 "nil" "RSL_strlsort_neg: an argument that is not a list gives nil"
rsl_is "$RSL" '(acad_strlsort '\''(1 2))' 1 "nil" "RSL_strlsort_neg: a list of numbers gives nil"
rsl_is "$RSL" '(acad_strlsort '\''("a" 2))' 1 "nil" "RSL_strlsort_neg: a list with a non-string gives nil"

# ---- vl-consp, row D8-AUTOCAD-LISP-0315 ------------------------------------------------------------------
rsl_is "$RSL" '(vl-consp nil)' 1 "nil" "RSL_consp: (vl-consp nil) is nil (documented)"
rsl_is "$RSL" '(vl-consp t)' 1 "nil" "RSL_consp: (vl-consp t) is nil (documented)"
rsl_is "$RSL" '(vl-consp (cons 0 "LINE"))' 1 "T" "RSL_consp: a dotted pair is a cons (documented)"
rsl_is "$RSL" '(vl-consp '\''(1 2 3))' 1 "T" "RSL_consp: a non-empty list is a cons"
rsl_is "$RSL" '(vl-consp '\''())' 1 "nil" "RSL_consp: the empty list is not"
rsl_is "$RSL" '(vl-consp "text")' 1 "nil" "RSL_consp: a string is not"
rsl_is "$RSL" '(vl-consp 5)' 1 "nil" "RSL_consp: a number is not"
rsl_is "$RSL" '(vl-consp '\''abc)' 1 "nil" "RSL_consp: a symbol is not"

# ---- vl-list-length, row D8-AUTOCAD-LISP-0336 ------------------------------------------------------------
rsl_is "$RSL" '(vl-list-length nil)' 1 "0" "RSL_listlen: nil has length 0"
rsl_is "$RSL" '(vl-list-length '\''(1 2))' 1 "2" "RSL_listlen: a two-element list"
rsl_is "$RSL" '(vl-list-length '\''(a (b c) d))' 1 "3" "RSL_listlen: a nested list counts its top-level elements"
rsl_is "$RSL" '(vl-list-length (cons 1 2))' 1 "nil" "RSL_listlen_neg: a dotted pair is not a true list, so nil"
rsl_is "$RSL" '(vl-list-length (cons 0 "LINE"))' 1 "nil" "RSL_listlen_neg: another dotted pair"
rsl_err "$RSL" '(vl-list-length 5)' 1 'bad argument type' "RSL_listlen_neg: a number is not a list"

# ---- vl-member-if-not, row D8-AUTOCAD-LISP-0340 ----------------------------------------------------------
rsl_is "$RSL" '(vl-member-if-not '\''numberp '\''(1 2 "a" 4))' 1 "(\"a\" 4)" "RSL_memnot: the tail from the first non-number"
rsl_is "$RSL" '(vl-member-if-not '\''numberp '\''(1 2 3))' 1 "nil" "RSL_memnot: every element passes, so nil"
rsl_is "$RSL" '(vl-member-if-not '\''(lambda (x) (< x 3)) '\''(1 2 3 4))' 1 "(3 4)" "RSL_memnot: with a lambda predicate, the tail from the first element that fails"
rsl_is "$RSL" '(vl-member-if-not '\''numberp '\''("x" 1))' 1 "(\"x\" 1)" "RSL_memnot: the first element already fails, the whole list comes back"
rsl_is "$RSL" '(vl-member-if-not '\''numberp nil)' 1 "nil" "RSL_memnot: nil list"

# ---- vl-sort-i, row D8-AUTOCAD-LISP-0355 -----------------------------------------------------------------
rsl_is "$RSL" '(vl-sort-i '\''("a" "d" "f" "c") '\''>)' 1 "(2 1 3 0)" "RSL_sorti: the documented example, indexes of f d c a"
rsl_is "$RSL" '(vl-sort-i '\''(3 1 2) '\''<)' 1 "(1 2 0)" "RSL_sorti: ascending numbers give the indexes of 1 2 3"
rsl_is "$RSL" '(vl-sort-i '\''(3 1 2) '\''>)' 1 "(0 2 1)" "RSL_sorti: descending numbers give the indexes of 3 2 1"
rsl_is "$RSL" '(length (vl-sort-i '\''(2 1 2 1) '\''<))' 1 "4" "RSL_sorti: duplicates are kept: four elements, four indexes"
rsl_is "$RSL" '(mapcar '\''(lambda (i) (nth i '\''(2 1 2 1))) (vl-sort-i '\''(2 1 2 1) '\''<))' 1 "(1 1 2 2)" "RSL_sorti: reading the list through the indexes gives it sorted"
rsl_is "$RSL" '(vl-sort-i '\''(7) '\''<)' 1 "(0)" "RSL_sorti: one element"
rsl_is "$RSL" '(vl-sort-i nil '\''<)' 1 "nil" "RSL_sorti: nil list"
rsl_is "$RSL" '(vl-sort-i '\''(5 6 7) '\''<)' 1 "(0 1 2)" "RSL_sorti: an already sorted list keeps its index order"
