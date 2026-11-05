# ---------------------------------------------------------------------------
# LSP3_: AutoLISP-style Lisp dialect, third pass (src/script/Lisp.cpp, commands/cmd_lisp.cpp). PARITY_MAP.md
# "AutoLISP-equivalent command scripting language": error handling (*error*, error, vl-catch-all-*), entmake / entupd,
# ssget with -4 operators and windows (W C WP CP F), loading real source files (BOM, CRLF, comments, escapes, nested
# load) and interactive prompts (getint & co. park the program until the next typed line). One real app process runs
# lisp3_script.txt; every expected value is computed here with bash / awk or follows from plane geometry worked out in
# the comments. Self-contained (own helpers) so that `run_sections.sh LSP3` works alone.
# ---------------------------------------------------------------------------
lsp3_exec() {  # lsp3_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
lsp3_ok() { echo "ok   $1"; }
lsp3_bad() { echo "FAIL $1"; fail=1; }
lsp3_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat the backslashes)
  sed "s/^history: //" <<<"$1" | LSP3_CMD="Command: $2" LSP3_NTH="$3" awk 'BEGIN { n = ENVIRON["LSP3_NTH"] + 0; cmd = ENVIRON["LSP3_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
lsp3_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(lsp3_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then lsp3_ok "$5 ($2 -> $got)"; else lsp3_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
lsp3_has() {  # <output> <typed text> <nth> <substring the block must contain> <description>
  local blk; blk="$(lsp3_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk"; then lsp3_ok "$5"; else lsp3_bad "$5: '$2' (#$3) printed no '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
lsp3_hasnot() {  # the block must NOT contain the substring
  local blk; blk="$(lsp3_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk"; then lsp3_bad "$5: '$2' (#$3) unexpectedly printed '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; else lsp3_ok "$5"; fi
}
lsp3_count() {  # <output> <typed text> <nth> <substring> <expected count in the block> <description>
  local blk n; blk="$(lsp3_blk "$1" "$2" "$3")"; n="$(grep -cF -- "$4" <<<"$blk" || true)"
  if [ "$n" = "$5" ]; then lsp3_ok "$6"; else lsp3_bad "$6: '$2' (#$3) printed '$4' $n time(s), expected $5"; echo "$blk" | head -8 | sed 's/^/     actual:   /'; fi
}
lsp3_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(lsp3_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then lsp3_ok "$5"; else lsp3_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
lsp3_refused() {  # <output> <typed text> <message> <description>: result nil and the reason printed
  local blk got
  blk="$(lsp3_blk "$1" "$2" 1)"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ "$got" = "nil" ] && grep -qF -- "$3" <<<"$blk"; then lsp3_ok "$4"; else lsp3_bad "$4: '$2' gave '$got' with no '$3'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
lsp3_all() {  # <output> <needle> <expected count> <description>: whole-output count
  local n; n="$(grep -cF -- "$2" <<<"$1" || true)"
  if [ "$n" = "$3" ]; then lsp3_ok "$4"; else lsp3_bad "$4 (found $n, expected $3)"; near "$1" "$2"; fi
}
lsp3_r() {  # a real the way the app prints it
  local s; s="$(awk -v x="$1" 'BEGIN { printf "%.6f", x }')"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}
lsp3_meas() {  # <output> <Length|Volume> <nth> <expected> <description>   (relative 1e-3: Length prints 4 significant digits)
  local blk got
  blk="$(lsp3_blk "$1" "$2" "$3")"
  got="$(grep -m1 -o "\\(Total length\\|$2\\) = [-0-9.e+]*" <<<"$blk" || true)"; got="${got##* = }"
  if [ -n "$got" ] && awk -v a="$got" -v b="$4" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 1e-3 * b) }'; then lsp3_ok "$5 ($2 $got)"; else lsp3_bad "$5: $2 read '$got', expected $4"; near "$blk" "$2 = $4"; fi
}

LSP3_PI="$(awk 'BEGIN { printf "%.12f", atan2(0, -1) }')"
if [ "$IS_WINDOWS" = 1 ]; then LSP3_SUB="Dino8"; else LSP3_SUB="dino8"; fi
LSP3_CFG="$TMP/lsp3_cfg"
mkdir -p "$LSP3_CFG/$LSP3_SUB"

# ---- the source files the script loads: a UTF-8 BOM, CRLF line ends, comments, escapes, multi-line defuns ----
{
  printf '\xEF\xBB\xBF'
  printf '%s\r\n' ';;; ------------------------------------------------------------' ';;; TOOLS.LSP  sample routines (a real-looking file)' ';;; ------------------------------------------------------------' \
    ';| block comment' '   over two lines |;' \
    '(defun poly-area (pts / n i s p q)        ; shoelace formula' \
    '  (setq n (length pts) i 0 s 0.0)' \
    '  (while (< i n)' \
    '    (setq p (nth i pts)' \
    '          q (nth (rem (1+ i) n) pts)' \
    '          s (+ s (- (* (car p) (cadr q)) (* (car q) (cadr p)))))' \
    '    (setq i (1+ i)))' \
    '  (/ (abs s) 2.0))' \
    '(defun fib (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))' \
    '(defun join (lst sep / out)' \
    '  (setq out "")' \
    '  (foreach s lst' \
    '    (setq out (if (= out "") s (strcat out sep s))))' \
    '  out)' \
    "(defun sq-sum (l) (apply '+ (mapcar '(lambda (x) (* x x)) l)))" \
    '(defun c:ring ( / r)' \
    '  (setq r (getreal "\nRing radius: "))' \
    '  (if (and r (> r 0)) (command "Circle" "0,0,0" r))' \
    '  (princ))' \
    '(setq greeting "tab:\there \"quoted\" back\\slash oct:\101\102 semi;colon")   ; the ; inside the string is text' \
    '(setq version-tag "1.0")' \
    '(princ "\nTOOLS loaded")' \
    '(princ)'
} > "$TMPW/lsp3_tools.lsp"
printf '%s\n' '(setq broken-before 1)' '(/ 1 0)' '(setq broken-after 1)' > "$TMPW/lsp3_broken.lsp"
printf '%s\n' '(setq outer-done 1)' "(load \"$TMPW/lsp3_inner\")" '(setq outer-done 2)' > "$TMPW/lsp3_outer.lsp"
printf '%s\n' '(setq inner-done 1)' > "$TMPW/lsp3_inner.lsp"
sed "s#@TMP@#$TMPW#g" "$HERE/lisp3_script.txt" > "$TMPW/lsp3_a.txt"
LSP3="$(lsp3_exec "$LSP3_CFG" "$TMPW/lsp3_a.txt")" || { echo "$LSP3" | tail -20; lsp3_bad "LSP3 script exited non-zero"; }
grep -q "^smoke:" <<<"$LSP3" || { echo "$LSP3" | tail -20; lsp3_bad "LSP3: no smoke line (the app did not reach its normal end)"; }
if grep -q "@expect_.*failed" <<<"$LSP3"; then lsp3_bad "LSP3: an @expect_ check failed"; grep "expect" <<<"$LSP3" | head -3; else lsp3_ok "LSP3: no @expect_objects check failed"; fi
for pair in "7 2" "8 1" "9 2" "10 1"; do
  read -r LSP3_EN LSP3_EC <<<"$pair"
  LSP3_N="$(grep -c "^ok   expect_objects $LSP3_EN (got" <<<"$LSP3" || true)"
  if [ "$LSP3_N" = "$LSP3_EC" ]; then lsp3_ok "LSP3: @expect_objects $LSP3_EN held $LSP3_EC time(s)"; else lsp3_bad "LSP3: @expect_objects $LSP3_EN held $LSP3_N time(s), expected $LSP3_EC"; fi
done

# ---- vl-catch-all-* ----------------------------------------------------------------------------------------
lsp3_is "$LSP3" "(vl-catch-all-apply '/ '(7 0))" 1 "#<%catch-all-apply-error%>" "LSP3_catch: a failing call gives an error object, not an error"
lsp3_is "$LSP3" "(vl-catch-all-error-p (vl-catch-all-apply '/ '(7 0)))" 1 "T" "LSP3_catch: vl-catch-all-error-p recognises it"
lsp3_is "$LSP3" "(vl-catch-all-error-message (vl-catch-all-apply '/ '(7 0)))" 1 '"divide by zero"' "LSP3_catch: and its message"
lsp3_is "$LSP3" "(vl-catch-all-error-p (vl-catch-all-apply '+ '(1 2)))" 1 "nil" "LSP3_catch_neg: a call that works is not an error"
lsp3_is "$LSP3" "(vl-catch-all-apply '+ '(1 2))" 1 "$((1+2))" "LSP3_catch: and returns its value"
lsp3_is "$LSP3" "(vl-catch-all-error-message (vl-catch-all-apply 'nosuchfn '(1)))" 1 '"no function definition: NOSUCHFN"' "LSP3_catch: an undefined function"
lsp3_is "$LSP3" "(vl-catch-all-error-message (vl-catch-all-apply 'car '(5)))" 1 '"bad argument type: listp: 5 in car"' "LSP3_catch: a type error"
lsp3_is "$LSP3" "(vl-catch-all-error-message (vl-catch-all-apply '(lambda () (error \"custom stop\")) nil))" 1 '"custom stop"' "LSP3_catch: (error \"text\") inside the call"
lsp3_all "$LSP3" 'Lisp: "command failed: NoSuchCmdXYZ"' 1 "LSP3_catch: a command that fails is an error object too"
lsp3_is "$LSP3" "(type (vl-catch-all-apply '/ '(7 0)))" 1 "VL-CATCH-ALL-APPLY-ERROR" "LSP3_catch: its type"
lsp3_is "$LSP3" "(vl-catch-all-error-p nil)" 1 "nil" "LSP3_catch_neg: nil is not an error object"
lsp3_is "$LSP3" "(vl-catch-all-error-p \"divide by zero\")" 1 "nil" "LSP3_catch_neg: neither is its message text"
lsp3_err "$LSP3" "(vl-catch-all-error-message 5)" 1 "bad argument type: vl-catch-all-apply-error" "LSP3_catch_neg: the message of a number"
lsp3_err "$LSP3" "(vl-catch-all-error-message \"text\")" 1 "bad argument type: vl-catch-all-apply-error" "LSP3_catch_neg: the message of a string"
lsp3_err "$LSP3" "(vl-catch-all-error-p)" 1 "too few arguments to vl-catch-all-error-p" "LSP3_catch_neg: no argument"
lsp3_err "$LSP3" "(vl-catch-all-apply '+ 5)" 1 "bad argument type: listp" "LSP3_catch_neg: the arguments must be a list"
lsp3_err "$LSP3" "(vl-catch-all-apply '+)" 1 "too few arguments to vl-catch-all-apply" "LSP3_catch_neg: one argument"
lsp3_err "$LSP3" "(error)" 1 "too few arguments to error" "LSP3_error_neg: (error) with nothing"
lsp3_err "$LSP3" "(error 5)" 1 "bad argument type: stringp" "LSP3_error_neg: (error 5)"

# ---- *error* ------------------------------------------------------------------------------------------------------
lsp3_has "$LSP3" "(progn (/ 1 0) (setq reached T))" 1 "handled: divide by zero" "LSP3_error: *error* is called with the message"
lsp3_hasnot "$LSP3" "(progn (/ 1 0) (setq reached T))" 1 "Lisp error" "LSP3_error: and the error is not reported a second time"
lsp3_is "$LSP3" "(list err-seen reached)" 1 '("divide by zero" nil)' "LSP3_error: the handler saw the text and the program stopped at the error (reached is nil)"
lsp3_has "$LSP3" "(error \"my own\")" 1 "handled: my own" "LSP3_error: (error \"msg\") goes to *error* too"
lsp3_is "$LSP3" "(list err-seen)" 1 '("my own")' "LSP3_error: with its text"
lsp3_has "$LSP3" "failing" 1 "handled: bad argument type: listp: 5 in car" "LSP3_error: a c: command that fails calls *error*"
lsp3_hasnot "$LSP3" "failing" 1 "Lisp error in" "LSP3_error: and prints no error line of its own"
lsp3_is "$LSP3" "(list err-seen reached2)" 1 '("bad argument type: listp: 5 in car" nil)' "LSP3_error: the command stopped at the error"
lsp3_err "$LSP3" "(error \"again\")" 1 "divide by zero" "LSP3_error_neg: an error inside *error* is reported, not looped on"
lsp3_err "$LSP3" "(error \"plain\")" 1 "plain" "LSP3_error_neg: with *error* set to nil the error is reported as before"
lsp3_is "$LSP3" "(progn (setq err-seen nil) (vl-catch-all-apply '/ '(1 0)) err-seen)" 1 "nil" "LSP3_error_neg: a caught error does not reach *error*"

# ---- entmake: the scene -----------------------------------------------------------------------------------------------
lsp3_is "$LSP3" "(mapcar '(lambda (e) (cdr (assoc 0 (entget e)))) (list 1 2 3 4 5 6 7))" 1 '("LINE" "LINE" "CIRCLE" "ARC" "POINT" "LWPOLYLINE" "LINE")' "LSP3_entmake: the seven entities have the types that were asked for"
lsp3_is "$LSP3" "(mapcar '(lambda (e) (cdr (assoc 8 (entget e)))) (list 1 2 3 4 5 6 7))" 1 '("Default" "Walls" "Walls" "Default" "Default" "Default" "Doors")' "LSP3_entmake: and the layers"
lsp3_is "$LSP3" "(mapcar '(lambda (e) (cdr (assoc 62 (entget e)))) (list 1 2 3 4 5 6 7))" 1 '(nil 1 nil 3 nil nil nil)' "LSP3_entmake: and the explicit colors (group 62)"
lsp3_is "$LSP3" "(cdr (assoc 11 (entget 1)))" 1 "($(lsp3_r 10) $(lsp3_r 0) $(lsp3_r 0))" "LSP3_entmake: line end point"
lsp3_is "$LSP3" "(cdr (assoc 10 (entget 3)))" 1 "($(lsp3_r 20) $(lsp3_r 20) $(lsp3_r 0))" "LSP3_entmake: circle centre"
lsp3_is "$LSP3" "(cdr (assoc 40 (entget 3)))" 1 "$(lsp3_r 3)" "LSP3_entmake: circle radius"
lsp3_is "$LSP3" "(cdr (assoc 40 (entget 4)))" 1 "$(lsp3_r 4)" "LSP3_entmake: arc radius"
lsp3_is "$LSP3" "(cdr (assoc 50 (entget 4)))" 1 "$(lsp3_r 0)" "LSP3_entmake: arc start angle"
lsp3_is "$LSP3" "(cdr (assoc 51 (entget 4)))" 1 "$(lsp3_r "$(awk -v p="$LSP3_PI" 'BEGIN { printf "%.12f", p / 2 }')")" "LSP3_entmake: arc end angle (pi/2)"
lsp3_is "$LSP3" "(cdr (assoc 10 (entget 5)))" 1 "($(lsp3_r 5) $(lsp3_r 2) $(lsp3_r 0))" "LSP3_entmake: point"
lsp3_is "$LSP3" "(cdr (assoc 90 (entget 6)))" 1 "4" "LSP3_entmake: the closed polyline reports 4 vertices (group 90)"
lsp3_is "$LSP3" "(cdr (assoc 70 (entget 6)))" 1 "1" "LSP3_entmake: and closed (group 70 = 1)"
lsp3_is "$LSP3" "(length (vl-remove-if-not '(lambda (g) (= (car g) 10)) (entget 6)))" 1 "4" "LSP3_entmake: with four vertex groups"
lsp3_is "$LSP3" "(entlast)" 1 "7" "LSP3_entmake: the last entity made is entity 7"
lsp3_meas "$LSP3" Length 1 10 "LSP3_entmake: the app's own Length of the line is 10 (3-4-5 style endpoints, bash 10)"
lsp3_meas "$LSP3" Length 2 "$(awk -v p="$LSP3_PI" 'BEGIN { print 2 * p * 3 }')" "LSP3_entmake: the circle's Length is 2 pi r"
lsp3_meas "$LSP3" Length 3 "$(awk -v p="$LSP3_PI" 'BEGIN { print 4 * p / 2 }')" "LSP3_entmake: the arc's Length is r * sweep"
lsp3_meas "$LSP3" Length 4 16 "LSP3_entmake: the closed 4 x 4 square's Length is its perimeter 16"

# ---- entmake refusals -------------------------------------------------------------------------------------------------
lsp3_refused "$LSP3" "(entmake '((8 . \"Default\") (10 0.0 0.0 0.0)))" "group 0 (the entity type) is required" "LSP3_entmake_neg: no entity type"
lsp3_refused "$LSP3" "(entmake '((0 . \"TEXT\") (10 0.0 0.0 0.0)))" "the entity type TEXT is not supported" "LSP3_entmake_neg: an unsupported type"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (8 . \"NoSuchLayer\") (10 0.0 0.0 0.0) (11 1.0 1.0 0.0)))" 'layer "NoSuchLayer" does not exist' "LSP3_entmake_neg: an unknown layer"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (62 . 300) (10 0.0 0.0 0.0) (11 1.0 1.0 0.0)))" "color 300 is not supported" "LSP3_entmake_neg: color 300"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (62 . 0) (10 0.0 0.0 0.0) (11 1.0 1.0 0.0)))" "color 0 is not supported" "LSP3_entmake_neg: color 0 (BYBLOCK)"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (10 0.0 0.0 0.0)))" "a LINE needs group 11" "LSP3_entmake_neg: a line without its end"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (10 1.0 1.0 0.0) (11 1.0 1.0 0.0)))" "two different points" "LSP3_entmake_neg: a zero-length line"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (10 0.0 0.0 0.0) (10 2.0 2.0 0.0) (11 1.0 1.0 0.0)))" "a LINE needs exactly one group 10" "LSP3_entmake_neg: a line with two starts"
lsp3_refused "$LSP3" "(entmake '((0 . \"POINT\") (10 0.0 0.0 0.0) (11 1.0 1.0 0.0)))" "group 11 is not used by a POINT" "LSP3_entmake_neg: a point with an end"
lsp3_refused "$LSP3" "(entmake '((0 . \"POINT\")))" "a POINT needs exactly one group 10" "LSP3_entmake_neg: a point without a location"
lsp3_refused "$LSP3" "(entmake '((0 . \"CIRCLE\") (10 0.0 0.0 0.0) (40 . -1.0)))" "needs a positive radius" "LSP3_entmake_neg: a negative radius"
lsp3_refused "$LSP3" "(entmake '((0 . \"CIRCLE\") (10 0.0 0.0 0.0)))" "needs a positive radius" "LSP3_entmake_neg: no radius"
lsp3_refused "$LSP3" "(entmake '((0 . \"CIRCLE\") (10 0.0 0.0 0.0) (40 . 1.0) (210 0.0 1.0 0.0)))" "only the normal 0,0,1 is supported" "LSP3_entmake_neg: a tilted normal"
lsp3_refused "$LSP3" "(entmake '((0 . \"ARC\") (10 0.0 0.0 0.0) (40 . 1.0) (50 . 0.0)))" "needs the start and end angles" "LSP3_entmake_neg: an arc without its end angle"
lsp3_refused "$LSP3" "(entmake '((0 . \"ARC\") (10 0.0 0.0 0.0) (40 . 1.0) (50 . 2.0) (51 . 1.0)))" "the end angle must be greater" "LSP3_entmake_neg: end before start"
lsp3_refused "$LSP3" "(entmake '((0 . \"ARC\") (10 0.0 0.0 0.0) (40 . 1.0) (50 . 0.0) (51 . 7.0)))" "sweep at most 2 pi" "LSP3_entmake_neg: a sweep over 2 pi (7 > 6.283)"
lsp3_refused "$LSP3" "(entmake '((0 . \"LWPOLYLINE\") (10 0.0 0.0 0.0) (10 1.0 0.0 0.0)))" "at least three vertices" "LSP3_entmake_neg: two vertices make a line, not a polyline"
lsp3_refused "$LSP3" "(entmake '((0 . \"LWPOLYLINE\") (90 . 5) (10 0.0 0.0 0.0) (10 1.0 0.0 0.0) (10 1.0 1.0 0.0)))" "group 90 says 5 vertices but 3 were given" "LSP3_entmake_neg: group 90 disagrees with the vertices"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (1 . \"text\") (10 0.0 0.0 0.0) (11 1.0 1.0 0.0)))" "group 1 is not supported by entmake" "LSP3_entmake_neg: an unsupported group"
lsp3_refused "$LSP3" "(entmake '((0 . \"LINE\") (40 . 1.0) (10 0.0 0.0 0.0) (11 1.0 1.0 0.0)))" "group 40 is not used by a LINE" "LSP3_entmake_neg: a radius on a line"
lsp3_err "$LSP3" "(entmake '((0 . \"LINE\") (10 a b c) (11 1.0 1.0 0.0)))" 1 "point coordinates must be numbers" "LSP3_entmake_neg: symbols as coordinates"
lsp3_refused "$LSP3" "(entmake nil)" "group 0 (the entity type) is required" "LSP3_entmake_neg: nil"
lsp3_err "$LSP3" "(entmake 5)" 1 "bad argument type: listp" "LSP3_entmake_neg: a number"
lsp3_err "$LSP3" "(entmake)" 1 "too few arguments to entmake" "LSP3_entmake_neg: no argument"

# ---- entupd -------------------------------------------------------------------------------------------------------------
lsp3_is "$LSP3" "(entupd 1)" 1 "1" "LSP3_entupd: returns the entity name"
lsp3_is "$LSP3" "(entupd 7)" 1 "7" "LSP3_entupd: for the last entity too"
lsp3_is "$LSP3" "(entupd 9999)" 1 "nil" "LSP3_entupd_neg: no such entity"
lsp3_err "$LSP3" "(entupd 'a)" 1 "bad argument type: ename" "LSP3_entupd_neg: a symbol"
lsp3_err "$LSP3" "(entupd)" 1 "too few arguments to entupd" "LSP3_entupd_neg: no argument"

# ---- ssget -4 operators ---------------------------------------------------------------------------------------------------
# The scene: 1 line y=0 (Default), 2 line y=5 (Walls, red), 3 circle r 3 (Walls), 4 arc r 4 (Default, color 3),
# 5 point (5,2), 6 closed square (30,0)-(34,4), 7 line x=8 (Doors).
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<OR\") (0 . \"LINE\") (0 . \"CIRCLE\") (-4 . \"OR>\"))))" 1 "4" "LSP3_op: <OR LINE CIRCLE: three lines and the circle"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<AND\") (0 . \"LINE\") (8 . \"Walls\") (-4 . \"AND>\"))))" 1 "1" "LSP3_op: <AND LINE Walls: line 2 only"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<NOT\") (0 . \"LINE\") (-4 . \"NOT>\"))))" 1 "4" "LSP3_op: <NOT LINE: circle, arc, point, polyline"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<XOR\") (0 . \"LINE\") (8 . \"Walls\") (-4 . \"XOR>\"))))" 1 "3" "LSP3_op: <XOR LINE Walls: lines 1 and 7, circle 3 (exactly one holds)"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<OR\") (-4 . \"<AND\") (0 . \"LINE\") (8 . \"Walls\") (-4 . \"AND>\") (0 . \"CIRCLE\") (-4 . \"OR>\"))))" 1 "2" "LSP3_op: a nested group: (LINE and Walls) or CIRCLE"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<or\") (0 . \"POINT\") (0 . \"ARC\") (-4 . \"or>\"))))" 1 "2" "LSP3_op: operator names ignore case"
lsp3_is "$LSP3" "(ssname (ssget \"X\" '((-4 . \">\") (40 . 3.5))) 0)" 1 "4" "LSP3_op: radius > 3.5 is the arc"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \">\") (40 . 3.5))))" 1 "1" "LSP3_op: and only the arc"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<=\") (40 . 3.0))))" 1 "1" "LSP3_op: radius <= 3 is the circle (3.0 itself counts)"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \">=\") (40 . 3.0))))" 1 "2" "LSP3_op: radius >= 3 is circle and arc"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"/=\") (40 . 3.0))))" 1 "1" "LSP3_op: radius /= 3 is the arc (entities with no radius are not selected)"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"=\") (40 . 4.0))))" 1 "1" "LSP3_op: radius = 4 is the arc"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<\") (62 . 4))))" 1 "2" "LSP3_op: color < 4: the red line (1) and the color-3 arc"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \">\") (62 . 100))))" 1 "5" "LSP3_op: color > 100: the five BYLAYER entities count as 256"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \">,>,*\") (10 4.0 1.0 0.0))))" 1 "3" "LSP3_op: x > 4 and y > 1 on group 10: circle, point, and the polyline (its vertex 34,4)"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<,<,*\") (10 1.0 1.0 0.0))))" 1 "1" "LSP3_op: x < 1 and y < 1: line 1 starting at 0,0"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"*,=,*\") (10 0.0 5.0 0.0))))" 1 "1" "LSP3_op: only y = 5 matters: line 2"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((0 . \"LINE\") (-4 . \">\") (62 . 100) (8 . \"Doors\"))))" 1 "1" "LSP3_op: plain pairs and a relational one together: line 7"
lsp3_is "$LSP3" "(sslength (ssget \"X\" '((-4 . \"<NOT\") (-4 . \"<OR\") (0 . \"LINE\") (0 . \"POINT\") (-4 . \"OR>\") (-4 . \"NOT>\"))))" 1 "3" "LSP3_op: NOT of an OR group: circle, arc, polyline"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \"<OR\") (0 . \"CIRCLE\")))" 1 "the group is not closed (missing OR>)" "LSP3_op_neg: an unclosed group"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \"OR>\")))" 1 "OR> without its opening <OR" "LSP3_op_neg: a closer without its opener"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \"AND>\") (0 . \"LINE\")))" 1 "AND> without its opening <AND" "LSP3_op_neg: another closer"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \">\")))" 1 "needs a group after it" "LSP3_op_neg: a relational operator with nothing after it"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \">\") (-4 . \"<OR\")))" 1 "not another operator" "LSP3_op_neg: followed by another operator"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \"??\") (40 . 1.0)))" 1 'unknown relational operator "??"' "LSP3_op_neg: an operator that does not exist"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \"<\") (0 . \"LINE\")))" 1 "compares numbers, not strings" "LSP3_op_neg: < on a string group"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \">,>\") (10 1.0 2.0 3.0)))" 1 "needs one part per coordinate" "LSP3_op_neg: two comma parts for three coordinates"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . \">,>,*\") (40 . 1.0)))" 1 "needs a point group" "LSP3_op_neg: a comma operator on a scalar group"
lsp3_err "$LSP3" "(ssget \"X\" '((-4 . 5)))" 1 "needs a string" "LSP3_op_neg: (-4 . 5)"

# ---- windowed selection (geometry worked out by hand from the scene) ---------------------------------------------------------
lsp3_is "$LSP3" "(ssget \"W\" '(-1 -1) '(11 6))" 1 "<Selection set: 3>" "LSP3_win: W (-1,-1)-(11,6) holds lines 1 and 2 and the point; line 7 (y from -2 to 12) sticks out"
lsp3_is "$LSP3" "(sslength (ssget \"W\" '(-1 -1) '(11 6)))" 1 "3" "LSP3_win: three members"
lsp3_is "$LSP3" "(sslength (ssget \"C\" '(-1 -1) '(11 6)))" 1 "4" "LSP3_win: C on the same window adds the crossing line 7"
lsp3_is "$LSP3" "(sslength (ssget \"C\" '(9 -5) '(9.5 1)))" 1 "1" "LSP3_win: C (9,-5)-(9.5,1) is crossed by line 1 only (x 9..9.5 at y 0)"
lsp3_is "$LSP3" "(ssname (ssget \"C\" '(9 -5) '(9.5 1)) 0)" 1 "1" "LSP3_win: and it is entity 1"
lsp3_is "$LSP3" "(sslength (ssget \"W\" '(-5 -5) '(40 30)))" 1 "7" "LSP3_win: a window around everything (circle to 23, square to 34) selects all 7"
lsp3_is "$LSP3" "(ssget \"C\" '(19 19) '(21 21))" 1 "nil" "LSP3_win_neg: a window inside the circle's disk touches no outline (corner 21,21 is 1.41 from the centre, r = 3)"
lsp3_is "$LSP3" "(sslength (ssget \"C\" '(16 16) '(24 24)))" 1 "1" "LSP3_win: C around the whole circle"
lsp3_is "$LSP3" "(sslength (ssget \"W\" '(16 16) '(24 24)))" 1 "1" "LSP3_win: W around the whole circle"
lsp3_is "$LSP3" "(ssget \"W\" '(16 16) '(22 24))" 1 "nil" "LSP3_win_neg: W cutting the circle (it reaches x = 23) selects nothing"
lsp3_is "$LSP3" "(sslength (ssget \"C\" '(16 16) '(22 24)))" 1 "1" "LSP3_win: C cutting the circle selects it"
lsp3_is "$LSP3" "(ssname (ssget \"C\" '(3.5 19.5) '(4.5 21)) 0)" 1 "4" "LSP3_win: C on the arc's start (4,20)"
lsp3_is "$LSP3" "(ssget \"W\" '(3.5 19.5) '(4.5 21))" 1 "nil" "LSP3_win_neg: W on the same small window: the arc leaves it"
lsp3_is "$LSP3" "(sslength (ssget \"C\" '(11 6) '(-1 -1)))" 1 "4" "LSP3_win: the corners may be given in either order"
lsp3_is "$LSP3" "(sslength (ssget \"_C\" '(-1 -1) '(11 6)))" 1 "4" "LSP3_win: a leading _ on the mode is accepted"
lsp3_is "$LSP3" "(ssget \"C\" '(100 100) '(110 110))" 1 "nil" "LSP3_win_neg: an empty region"
lsp3_is "$LSP3" "(sslength (ssget \"WP\" '((-1 -1) (12 -1) (-1 7))))" 1 "2" "LSP3_win: WP triangle (-1,-1) (12,-1) (-1,7) holds line 1 and the point (line 2's end 10,5 is above the hypotenuse y = -1 + 8/13 (12 - x) = 0.23)"
lsp3_is "$LSP3" "(sslength (ssget \"CP\" '((-1 -1) (12 -1) (-1 7))))" 1 "4" "LSP3_win: CP on the triangle adds the partly inside line 2 and line 7"
lsp3_is "$LSP3" "(sslength (ssget \"WP\" '((-5 -5) (40 -5) (40 30) (-5 30))))" 1 "7" "LSP3_win: WP around everything"
lsp3_is "$LSP3" "(sslength (ssget \"CP\" '((29 -1) (31 -1) (31 1) (29 1))))" 1 "1" "LSP3_win: CP over the polyline's corner 30,0"
lsp3_is "$LSP3" "(ssget \"WP\" '((29 -1) (31 -1) (31 1) (29 1)))" 1 "nil" "LSP3_win_neg: WP over the same corner holds nothing whole"
lsp3_is "$LSP3" "(sslength (ssget \"F\" '((0 3) (9 3))))" 1 "1" "LSP3_win: the fence (0,3)-(9,3) crosses line 7 (x = 8) only"
lsp3_is "$LSP3" "(ssname (ssget \"F\" '((0 3) (9 3))) 0)" 1 "7" "LSP3_win: and it is entity 7"
lsp3_is "$LSP3" "(sslength (ssget \"F\" '((0 -1) (10 6))))" 1 "3" "LSP3_win: the diagonal fence y = -1 + 0.7 x crosses lines 1 (x 1.43), 2 (x 8.57) and 7 (y 4.6), but not the point"
lsp3_is "$LSP3" "(ssget \"F\" '((50 50) (60 60)))" 1 "nil" "LSP3_win_neg: a fence over nothing"
lsp3_is "$LSP3" "(sslength (ssget \"C\" '(-1 -1) '(11 6) '((0 . \"LINE\") (8 . \"Walls\"))))" 1 "1" "LSP3_win: a window and a filter together: the crossing Walls line"
lsp3_is "$LSP3" "(ssname (ssget \"C\" '(-1 -1) '(11 6) '((0 . \"LINE\") (8 . \"Walls\"))) 0)" 1 "2" "LSP3_win: it is entity 2"
lsp3_is "$LSP3" "(sslength (ssget \"W\" '(-1 -1) '(11 6) '((-4 . \"<NOT\") (0 . \"POINT\") (-4 . \"NOT>\"))))" 1 "2" "LSP3_win: a window with a -4 filter (everything but the point)"
lsp3_is "$LSP3" "(ssget \"W\" '(-1 -1) '(11 6) '((0 . \"CIRCLE\")))" 1 "nil" "LSP3_win_neg: nothing in the window passes the filter"
lsp3_err "$LSP3" "(ssget \"W\" '(0 0))" 1 "needs two corner points" "LSP3_win_neg: W with one corner"
lsp3_err "$LSP3" "(ssget \"W\" '(0 0) '(0 5))" 1 "must span an area" "LSP3_win_neg: corners with equal x"
lsp3_err "$LSP3" "(ssget \"W\" '(0 0) '(5 0))" 1 "must span an area" "LSP3_win_neg: corners with equal y"
lsp3_err "$LSP3" "(ssget \"W\" 5 '(1 1))" 1 "bad argument type: listp" "LSP3_win_neg: a number as a corner"
lsp3_err "$LSP3" "(ssget \"W\" '(0) '(1 1))" 1 "bad point in ssget" "LSP3_win_neg: a one-coordinate corner"
lsp3_err "$LSP3" "(ssget \"W\" '(a b) '(1 1))" 1 "bad point in ssget" "LSP3_win_neg: symbols as coordinates"
lsp3_err "$LSP3" "(ssget \"WP\" '((0 0) (1 1)))" 1 "needs at least three points" "LSP3_win_neg: a two-point polygon"
lsp3_err "$LSP3" "(ssget \"WP\" 5)" 1 "bad argument type: listp" "LSP3_win_neg: a number as the polygon"
lsp3_err "$LSP3" "(ssget \"CP\" '((0 0) (1 1) (a b)))" 1 "bad point in ssget" "LSP3_win_neg: a bad polygon vertex"
lsp3_err "$LSP3" "(ssget \"F\" '((0 0)))" 1 "needs at least two points" "LSP3_win_neg: a one-point fence"
lsp3_err "$LSP3" "(ssget \"Z\" '(0 0))" 1 "ssget: the modes are" "LSP3_win_neg: an unknown mode"
lsp3_err "$LSP3" "(ssget \"W\" '(0 0) '(1 1) '((0 . \"LINE\")) 5)" 1 "needs two corner points" "LSP3_win_neg: too many arguments"

# ---- loading real source files ---------------------------------------------------------------------------------------------------
lsp3_has "$LSP3" "(load \"$TMPW/lsp3_tools\")" 1 "TOOLS loaded" "LSP3_load: (load \"name\") finds name.lsp, skips the BOM, the CRLF line ends and both comment forms, and runs the file"
lsp3_is "$LSP3" "(load \"$TMPW/lsp3_tools\")" 1 "nil" "LSP3_load: the value of (load) is the file's last form, (princ), nil"
lsp3_is "$LSP3" "(poly-area '((0 0) (4 0) (4 3) (0 3)))" 1 "$(lsp3_r "$(awk 'BEGIN { x[0]=0;y[0]=0;x[1]=4;y[1]=0;x[2]=4;y[2]=3;x[3]=0;y[3]=3; s=0; for(i=0;i<4;i++){j=(i+1)%4; s+=x[i]*y[j]-x[j]*y[i]} print (s<0?-s:s)/2 }')")" "LSP3_load: poly-area of the 4 x 3 rectangle (awk shoelace)"
lsp3_is "$LSP3" "(poly-area '((0 0) (6 0) (0 8)))" 1 "$(lsp3_r "$(awk 'BEGIN { print 6*8/2 }')")" "LSP3_load: poly-area of the 6-8 right triangle (base * height / 2)"
lsp3_is "$LSP3" "(poly-area '((0.5 0.5) (3.25 0.75) (2.0 4.5) (-1.0 2.0)))" 1 "$(lsp3_r "$(awk 'BEGIN { split("0.5 3.25 2.0 -1.0",x," "); split("0.5 0.75 4.5 2.0",y," "); s=0; for(i=1;i<=4;i++){j=i%4+1; s+=x[i]*y[j]-x[j]*y[i]} print (s<0?-s:s)/2 }')")" "LSP3_load: poly-area of an irregular quadrilateral"
LSP3_F0=0; LSP3_F1=1; for LSP3_K in $(seq 2 15); do LSP3_T=$((LSP3_F0+LSP3_F1)); LSP3_F0=$LSP3_F1; LSP3_F1=$LSP3_T; done
lsp3_is "$LSP3" "(fib 15)" 1 "$LSP3_F1" "LSP3_load: recursive fib 15 equals bash's iterative value"
lsp3_is "$LSP3" "(join '(\"a\" \"bc\" \"d\") \"-\")" 1 '"a-bc-d"' "LSP3_load: a foreach/strcat routine from the file"
lsp3_is "$LSP3" "(join nil \"-\")" 1 '""' "LSP3_load_neg: the same routine on an empty list"
LSP3_G=$'tab:\there "quoted" back\\slash oct:AB semi;colon'
lsp3_is "$LSP3" "(strlen greeting)" 1 "${#LSP3_G}" "LSP3_load: the string escapes \\t \\\" \\\\ \\101\\102 produced exactly the characters bash builds (length ${#LSP3_G})"
lsp3_is "$LSP3" "(substr greeting 1 4)" 1 '"tab:"' "LSP3_load: its start"
lsp3_is "$LSP3" "(vl-string->list (substr greeting 5 1))" 1 "($(printf '%d' "'"$'\t'))" "LSP3_load: \\t is character code 9"
LSP3_T="${LSP3_G%%AB*}"
lsp3_is "$LSP3" "(vl-string-search \"AB\" greeting)" 1 "${#LSP3_T}" "LSP3_load: the octal escapes \\101\\102 are the letters AB, at bash's offset"
LSP3_T="${LSP3_G%%semi;colon*}"
lsp3_is "$LSP3" "(vl-string-search \"semi;colon\" greeting)" 1 "${#LSP3_T}" "LSP3_load: a ; inside a string is text, not a comment"
LSP3_T="${LSP3_G%%\"quoted\"*}"
lsp3_is "$LSP3" "(vl-string-search \"\\\"quoted\\\"\" greeting)" 1 "${#LSP3_T}" "LSP3_load: the escaped quotes are in the string"
LSP3_T="${LSP3_G%%back\\slash*}"
lsp3_is "$LSP3" "(vl-string-search \"back\\\\slash\" greeting)" 1 "${#LSP3_T}" "LSP3_load: a doubled backslash is one backslash"
lsp3_is "$LSP3" "(list version-tag)" 1 '("1.0")' "LSP3_load: a global set by the file"
lsp3_is "$LSP3" "(sq-sum '(1 2 3))" 1 "$((1*1+2*2+3*3))" "LSP3_load: a defun with a quoted lambda inside apply/mapcar"
lsp3_is "$LSP3" "(boundp 'poly-area)" 1 "T" "LSP3_load: boundp sees the loaded function"
lsp3_is "$LSP3" "(boundp 'no-such-name)" 1 "nil" "LSP3_load_neg: boundp of an undefined symbol"
lsp3_has "$LSP3" "(load \"$TMPW/lsp3_tools.lsp\")" 1 "TOOLS loaded" "LSP3_load: the name with its extension works as well"
lsp3_is "$LSP3" "(load \"$TMPW/lsp3_missing\" \"not found\")" 1 '"not found"' "LSP3_load_neg: a missing file with an onfailure value returns it"
lsp3_err "$LSP3" "(load \"$TMPW/lsp3_missing\")" 1 "can't open file" "LSP3_load_neg: a missing file without one is an error"
lsp3_is "$LSP3" "(load \"$TMPW/lsp3_broken\" \"load failed\")" 1 '"load failed"' "LSP3_load_neg: a file that fails half way returns the onfailure value"
lsp3_err "$LSP3" "(load \"$TMPW/lsp3_broken\")" 1 "divide by zero" "LSP3_load_neg: and without one the error is reported"
lsp3_is "$LSP3" "(list broken-before)" 1 "(1)" "LSP3_load_neg: the forms before the error did run"
lsp3_is "$LSP3" "(list outer-done inner-done)" 1 "(2 1)" "LSP3_load: a file that loads another file"
lsp3_all "$LSP3" "TOOLS loaded" 4 "LSP3_load: four loads of tools.lsp (load x2, LoadLisp, RunScript) each printed their message"
lsp3_all "$LSP3" "LoadLisp: loaded" 2 "LSP3_load: LoadLisp and RunScript of a .lsp both report the load"

# ---- interactive prompts ---------------------------------------------------------------------------------------------------------------
lsp3_has "$LSP3" "(setq a (getint \"First: \"))" 1 "First: " "LSP3_ask: (getint) prints its prompt and waits"
lsp3_is "$LSP3" "(setq a (getint \"First: \"))" 1 "7" "LSP3_ask: the next typed line, 7, is the answer"
lsp3_is "$LSP3" "(list a)" 1 "(7)" "LSP3_ask: and it was assigned"
lsp3_has "$LSP3" "(setq b (getreal \"Second: \"))" 1 "Requires numeric value." "LSP3_ask: a bad answer (abc) is reported and the prompt stays"
lsp3_is "$LSP3" "(setq b (getreal \"Second: \"))" 1 "2.5" "LSP3_ask: the next line, 2.5, is taken"
lsp3_is "$LSP3" "(list b)" 1 "(2.5)" "LSP3_ask: and assigned"
lsp3_is "$LSP3" "(setq pt (getpoint \"Pick: \"))" 1 "($(lsp3_r 3) $(lsp3_r 4) $(lsp3_r 0))" "LSP3_ask: (getpoint) takes a typed point 3,4"
lsp3_has "$LSP3" "(setq pt (getpoint \"Pick: \"))" 2 "Point or option keyword required." "LSP3_ask_neg: a bad point (1,x) is reported"
lsp3_is "$LSP3" "(setq pt (getpoint \"Pick: \"))" 2 "($(lsp3_r 5) $(lsp3_r 6) $(lsp3_r 7))" "LSP3_ask_neg: and the next line, 5,6,7, is taken"
lsp3_is "$LSP3" "(setq s (getstring \"Name: \"))" 1 '"hello"' "LSP3_ask: (getstring)"
lsp3_has "$LSP3" "(progn (setq x (getint \"Num: \")) (setq y (getint \"Num2: \")) (+ x y))" 1 "Num2: " "LSP3_ask: a program asking twice shows the second prompt"
lsp3_is "$LSP3" "(progn (setq x (getint \"Num: \")) (setq y (getint \"Num2: \")) (+ x y))" 1 "$((10+32))" "LSP3_ask: and gets the two answers from the next two lines (10 + 32)"
lsp3_is "$LSP3" "(progn (setq a (getint \"A: \")) (setq b (getint \"B: \")) (list a b)) 1" 1 "(1 2)" "LSP3_ask: one answer typed after the expression, the other on the next line"
lsp3_count "$LSP3" "(progn (initget 1) (setq q (getint \"Need: \")))" 1 "Requires a value." 2 "LSP3_ask_neg: initget bit 1 refuses Enter twice"
lsp3_is "$LSP3" "(progn (initget 1) (setq q (getint \"Need: \")))" 1 "9" "LSP3_ask_neg: and finally takes the 9"
lsp3_has "$LSP3" "(progn (initget \"Yes No\") (setq k (getkword \"Go? \")))" 1 "Invalid option keyword." "LSP3_ask_neg: a non-keyword answer (maybe) is refused"
lsp3_is "$LSP3" "(progn (initget \"Yes No\") (setq k (getkword \"Go? \")))" 1 '"Yes"' "LSP3_ask: and y is Yes"
lsp3_has "$LSP3" "(progn (initget 6) (setq r (getint \"Pos: \")))" 1 "Value must be nonzero." "LSP3_ask_neg: 0 is refused by initget bit 2"
lsp3_has "$LSP3" "(progn (initget 6) (setq r (getint \"Pos: \")))" 1 "Value must be positive." "LSP3_ask_neg: -4 by bit 4"
lsp3_is "$LSP3" "(progn (initget 6) (setq r (getint \"Pos: \")))" 1 "12" "LSP3_ask: 12 is taken"
lsp3_is "$LSP3" "(setq e0 (getint \"Escape: \"))" 1 "nil" "LSP3_ask: Enter at a prompt gives nil"
lsp3_is "$LSP3" "(list e0)" 1 "(nil)" "LSP3_ask: e0 is nil"
lsp3_has "$LSP3" "(command \"Point\" (getpoint \"Where: \"))" 1 "Where: " "LSP3_ask: a prompt inside a (command ...) argument"
lsp3_is "$LSP3" "(cdr (assoc 10 (entget (entlast))))" 1 "($(lsp3_r 5) $(lsp3_r 6) $(lsp3_r 7))" "LSP3_ask: the Point command got the point typed at the prompt (5,6,7)"
lsp3_has "$LSP3" "ring" 1 "Ring radius: " "LSP3_ask: a loaded c:ring command prompts"
lsp3_meas "$LSP3" Length 5 "$(awk -v p="$LSP3_PI" 'BEGIN { print 2 * p * 2 }')" "LSP3_ask: and drew the circle of the radius typed at the prompt (2): Length 4 pi"
lsp3_has "$LSP3" "ring" 2 "Ring radius: " "LSP3_ask_neg: ring again prompts"
lsp3_meas "$LSP3" Length 6 "$(awk -v p="$LSP3_PI" 'BEGIN { print 2 * p * 1 }')" "LSP3_ask: 'ring 1' takes the radius from the line (no prompt wait) and draws radius 1 (Enter at the earlier prompt drew nothing: 9 objects then, 10 now)"
lsp3_has "$LSP3" "(progn (setq w (getint \"Cancelled: \")) (setq z 1))" 1 "Function cancelled" "LSP3_cancel: Cancel at a prompt stops the program with Function cancelled"
lsp3_is "$LSP3" "(list w z)" 1 "(nil nil)" "LSP3_cancel: nothing after the prompt ran"
lsp3_hasnot "$LSP3" "(progn (setq w (getint \"Cancelled again: \")) (setq z 2))" 1 "Lisp error" "LSP3_cancel: with *error* defined the cancel goes there, not to an error line"
lsp3_is "$LSP3" "(list cm z)" 1 '("Function cancelled" nil)' "LSP3_cancel: *error* got Function cancelled and the program stopped"
lsp3_all "$LSP3" "Lisp: (AFTER nil)" 1 "LSP3_ask_neg: a c: command calling (command \"inner\") whose getint needs input does not hang: it gets nil and the caller goes on"
lsp3_all "$LSP3" "Lisp: (INNERDONE nil)" 1 "LSP3_ask_neg: the same from (progn (command ...) ...)"
lsp3_has "$LSP3" "(getint \"left open: \")" 1 "left open: " "LSP3_exit: the last prompt is printed; the app then ran to its normal end with the prompt still open (the smoke line is present)"
