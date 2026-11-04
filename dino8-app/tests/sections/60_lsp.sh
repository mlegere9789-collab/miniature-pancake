# ---------------------------------------------------------------------------
# LSP_: AutoLISP-style Lisp dialect (src/script/Lisp.{h,cpp}, commands/
# cmd_lisp.cpp). PARITY_MAP.md "AutoLISP-equivalent command scripting language"
# ("no LISP dialect or AutoLISP compatibility anywhere; scripting is Lua,
# Python or Macro/command files"). Four real app processes:
#   A  lisp_script.txt             the language, (command ...), c: commands,
#                                  files, and every negative control
#   B  lisp_persist_script.txt     fresh config dir whose acad.lsp (written
#                                  here, by bash) defines a c: command
#   C  lisp_nopersist_script.txt   same expressions, no acad.lsp: they fail
#   D  lisp_brokenauto_script.txt  acad.lsp with a syntax error: reported,
#                                  nothing of it runs, the app still works
# Oracles: every number is recomputed here with bash/awk arithmetic; every
# geometric claim is read back from the app's own Volume / Length / object
# count (the box volumes are w*d*h computed below, not taken from Lisp).
lsp_exec() {  # lsp_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
lsp_ok() { echo "ok   $1"; }
lsp_bad() { echo "FAIL $1"; fail=1; }
# the lines a command printed: after its exact "Command: <text>" echo (the Nth one) up to the next "Command:" line
lsp_blk() { sed "s/^history: //" <<<"$1" | awk -v n="$3" -v cmd="Command: $2" '$0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'; }
# <output> <typed text> <nth> <expected text after "Lisp: "> <description>
lsp_is() {
  local blk got
  blk="$(lsp_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then lsp_ok "$5 ($2 -> $got)"; else lsp_bad "$5: '$2' gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
# same but numeric, with a tolerance
lsp_isnum() {
  local blk got
  blk="$(lsp_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && awk -v a="$got" -v b="$4" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 1e-9 * (1 + (b < 0 ? -b : b))) }' 2>/dev/null; then lsp_ok "$5 ($2 -> $got)"; else lsp_bad "$5: '$2' gave '$got', expected $4"; near "$blk" "$4"; fi
}
# <output> <typed text> <nth> <substring that block must contain> <description>
lsp_has() {
  local blk; blk="$(lsp_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk"; then lsp_ok "$5"; else lsp_bad "$5: '$2' printed no '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
lsp_hasnot() {
  local blk; blk="$(lsp_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk"; then lsp_bad "$5: '$2' unexpectedly printed '$4'"; else lsp_ok "$5"; fi
}
# whole-output count of a substring (where a nested command's own "Command:" echo splits the block)
lsp_all() {  # <output> <needle> <expected count> <description>
  local n; n="$(grep -cF -- "$2" <<<"$1" || true)"
  if [ "$n" = "$3" ]; then lsp_ok "$4"; else lsp_bad "$4 (found $n, expected $3)"; near "$1" "$2"; fi
}
lsp_vol() {  # <output> <nth Volume> <expected> <description>
  local blk got; blk="$(lsp_blk "$1" "Volume" "$2")"
  got="$(grep -m1 -o 'Volume = [-0-9.e+]*' <<<"$blk" || true)"; got="${got#Volume = }"
  if [ -n "$got" ] && awk -v a="$got" -v b="$3" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 1e-6 * b) }'; then lsp_ok "$4 (Volume $got)"; else lsp_bad "$4: Volume read '$got', expected $3"; near "$blk" "Volume = $3"; fi
}

if [ "$IS_WINDOWS" = 1 ]; then LSP_SUB="Dino8"; else LSP_SUB="dino8"; fi
LSP_CFG_A="$TMP/lsp_cfg_a"; LSP_CFG_B="$TMP/lsp_cfg_b"; LSP_CFG_D="$TMP/lsp_cfg_d"
mkdir -p "$LSP_CFG_A/$LSP_SUB" "$LSP_CFG_B/$LSP_SUB" "$LSP_CFG_D/$LSP_SUB"
cat > "$TMPW/lsp_good.lsp" <<'EOS'
; a sample AutoLISP file: comment, multi-line defun, c: command
(defun sq (x) (* x x))
(defun c:sqbox ()            ; a 9 x 9 x 9 box
  (command "Box" "0,0,0"
           (list (sq 3) (sq 3) 0)
           (sq 3)))
;| block
   comment |;
(setq loaded T)
(sq 12)
EOS
printf '%s\n' '(setq rs_loaded 42)' '(defun c:rsbox () (command "Box" "0,0,0" "7,3,0" "2"))' > "$TMPW/lsp_good2.lsp"
printf '%s\n' '(setq zz 1)' '(setq yy (+ 1' > "$TMPW/lsp_syntax_bad.lsp"
printf '%s\n' '(setq z1 1)' '(/ 5 0)' '(setq z2 2)' > "$TMPW/lsp_runtime_bad.lsp"
printf '%s\n' '(defun autobox_helper (n) (* n n))' '(defun c:autobox () (command "Box" "0,0,0" "2,3,0" "4"))' '(setq auto_loaded 7)' > "$LSP_CFG_B/$LSP_SUB/acad.lsp"
printf '%s\n' '(defun c:okbox () (command "Box" "0,0,0" "1,1,0" "1"))' '(setq broken_marker 1)' '(setq bad (+ 1' > "$LSP_CFG_D/$LSP_SUB/acad.lsp"
sed "s#@TMP@#$TMPW#g" "$HERE/lisp_script.txt" > "$TMPW/lsp_a.txt"
LSP_A="$(lsp_exec "$LSP_CFG_A" "$TMPW/lsp_a.txt")" || { echo "$LSP_A"; lsp_bad "LSP script A exited non-zero"; }
grep -q "^smoke:" <<<"$LSP_A" || { echo "$LSP_A" | tail -20; lsp_bad "LSP_A: no smoke line"; }
if grep -q "@expect_.*failed" <<<"$LSP_A"; then lsp_bad "LSP_A: an @expect_ check failed"; grep "expect" <<<"$LSP_A" | head -3; else lsp_ok "LSP_A: every @expect_objects in the script held (9 after the drawing section, 10 after the negative controls, 11 and 12 after the file section)"; fi

# ---- the language: each result against an independently computed value -------
lsp_is "$LSP_A" "(+ 1 2 3)" 1 "$((1+2+3))" "LSP_arith: + adds integers"
lsp_is "$LSP_A" "(* 2 3.5)" 1 "$(awk 'BEGIN{printf "%.1f", 2*3.5}')" "LSP_arith: * mixes integer and real into a real"
lsp_is "$LSP_A" "(/ 7 2)" 1 "$((7/2))" "LSP_arith: integer / integer truncates (AutoLISP rule)"
lsp_is "$LSP_A" "(/ 7 2.0)" 1 "$(awk 'BEGIN{printf "%.1f", 7/2.0}')" "LSP_arith: integer / real is real"
lsp_is "$LSP_A" "(rem 17 5)" 1 "$((17%5))" "LSP_arith: rem"
lsp_is "$LSP_A" "(expt 2 10)" 1 "$((2**10))" "LSP_arith: expt"
lsp_isnum "$LSP_A" "(sqrt 144)" 1 "$(awk 'BEGIN{print sqrt(144)}')" "LSP_arith: sqrt"
lsp_is "$LSP_A" "(- 5)" 1 "$((-5))" "LSP_arith: unary minus"
lsp_is "$LSP_A" "(1+ 41)" 1 "$((41+1))" "LSP_arith: 1+"
lsp_is "$LSP_A" "(max 3 9 4)" 1 "9" "LSP_arith: max"
lsp_is "$LSP_A" "(abs -7.5)" 1 "7.5" "LSP_arith: abs of a real"
lsp_is "$LSP_A" "(setq a 5 b (* a a))" 1 "25" "LSP_var: setq with two pairs, the second using the first"
lsp_is "$LSP_A" "(+ a b)" 1 "$((5+25))" "LSP_var: both variables persist between command lines"
LSP_FACT=1; for k in $(seq 1 10); do LSP_FACT=$((LSP_FACT*k)); done
lsp_is "$LSP_A" "(fact 10)" 1 "$LSP_FACT" "LSP_defun: recursive factorial 10 equals bash's own product"
LSP_FACT=1; for k in $(seq 1 20); do LSP_FACT=$((LSP_FACT*k)); done
lsp_is "$LSP_A" "(fact 20)" 1 "$LSP_FACT" "LSP_defun: factorial 20 (a 64-bit integer, 2432902008176640000) is exact"
lsp_is "$LSP_A" "(dbl 4)" 1 "8" "LSP_scope: a function with a local (/ y) computes with it"
lsp_is "$LSP_A" "(list y)" 1 "(99)" "LSP_scope: the global y is still 99 after the function used a local y"
lsp_is "$LSP_A" "(mapcar '1+ '(1 2 3))" 1 "(2 3 4)" "LSP_list: mapcar over a built-in"
lsp_is "$LSP_A" "(reverse '(1 2 3))" 1 "(3 2 1)" "LSP_list: reverse"
lsp_is "$LSP_A" "(append '(1 2) '(3 4))" 1 "(1 2 3 4)" "LSP_list: append"
lsp_is "$LSP_A" "(nth 2 '(a b c))" 1 "C" "LSP_list: nth is zero-based and symbols print upper case"
lsp_is "$LSP_A" "(length '(1 2 3 4))" 1 "4" "LSP_list: length"
lsp_is "$LSP_A" "(apply '+ '(1 2 3 4))" 1 "10" "LSP_list: apply"
lsp_is "$LSP_A" "(cons 0 '(1 2))" 1 "(0 1 2)" "LSP_list: cons"
lsp_is "$LSP_A" "(assoc 'b '((a 1) (b 2)))" 1 "(B 2)" "LSP_list: assoc"
lsp_is "$LSP_A" "(member 3 '(1 2 3 4))" 1 "(3 4)" "LSP_list: member"
lsp_is "$LSP_A" "(mapcar '(lambda (x) (* x x)) '(1 2 3 4))" 1 "(1 4 9 16)" "LSP_list: mapcar with a lambda"
lsp_has "$LSP_A" "(foreach x '(1 2 3) (princ x))" 1 "123" "LSP_loop: foreach ran its body three times (princ output 123)"
lsp_is "$LSP_A" "(strcat \"ab\" \"cd\" \"ef\")" 1 '"abcdef"' "LSP_str: strcat"
lsp_is "$LSP_A" "(substr \"hello\" 2 3)" 1 '"ell"' "LSP_str: substr is 1-based"
lsp_is "$LSP_A" "(strlen \"hello\")" 1 "5" "LSP_str: strlen"
lsp_is "$LSP_A" "(itoa 42)" 1 '"42"' "LSP_str: itoa"
lsp_is "$LSP_A" "(atoi \"123\")" 1 "123" "LSP_str: atoi"
lsp_is "$LSP_A" "(rtos 3.14159 2 2)" 1 '"3.14"' "LSP_str: rtos to 2 decimals"
lsp_is "$LSP_A" "(strcase \"Hello\")" 1 '"HELLO"' "LSP_str: strcase upper"
lsp_is "$LSP_A" "(strcase \"Hello\" T)" 1 '"hello"' "LSP_str: strcase lower"
LSP_SUM=0; for k in 0 1 2 3 4; do LSP_SUM=$((LSP_SUM+k)); done
lsp_is "$LSP_A" "(list s)" 1 "($LSP_SUM)" "LSP_loop: while accumulated 0+1+2+3+4"
lsp_is "$LSP_A" "(list s)" 2 "($((LSP_SUM+30)))" "LSP_loop: repeat 3 added 10 three times"
lsp_is "$LSP_A" "(cond ((= 1 2) \"a\") ((= 2 2) \"b\") (T \"c\"))" 1 '"b"' "LSP_cond: first true clause wins"
lsp_is "$LSP_A" "(and 1 nil)" 1 "nil" "LSP_logic: and"
lsp_is "$LSP_A" "(or nil 7)" 1 "7" "LSP_logic: or"
lsp_is "$LSP_A" "(= 3 3.0)" 1 "T" "LSP_logic: = compares numerically"
lsp_is "$LSP_A" "(< 1 2 3)" 1 "T" "LSP_logic: < chains"
lsp_is "$LSP_A" "(equal 1.0 1.0000001 0.001)" 1 "T" "LSP_logic: equal with a fuzz"
lsp_is "$LSP_A" "(eq 'a 'a)" 1 "T" "LSP_logic: eq on symbols"
lsp_is "$LSP_A" "Lisp (+ 10 20)" 1 "30" "LSP_entry: the Lisp command takes an expression as its argument"
lsp_isnum "$LSP_A" "(distance '(0 0 0) '(3 4 12))" 1 "$(awk 'BEGIN{print sqrt(3*3+4*4+12*12)}')" "LSP_geom: distance of (0,0,0)-(3,4,12) equals awk's sqrt(169)"
lsp_isnum "$LSP_A" "(car (polar '(1 1 0) 0 5))" 1 "$(awk 'BEGIN{print 1+5*cos(0)}')" "LSP_geom: polar x at angle 0, distance 5 from (1,1)"
lsp_isnum "$LSP_A" "(cadr (polar '(0 0 0) (/ (* 4 (atan 1)) 2) 5))" 1 "$(awk 'BEGIN{pi=atan2(0,-1); print 5*sin(pi/2)}')" "LSP_geom: polar y at pi/2 (pi built as 4*atan 1) is 5"
lsp_is "$LSP_A" "(last '(1 2 3))" 1 "3" "LSP_list: last"
lsp_is "$LSP_A" "(cadr '(1 2 3))" 1 "2" "LSP_list: cadr"
lsp_isnum "$LSP_A" "(atof \"2.5\")" 1 "2.5" "LSP_str: atof"
lsp_is "$LSP_A" "(fix 3.9)" 1 "3" "LSP_arith: fix truncates toward zero"
lsp_is "$LSP_A" "(float 3)" 1 "3.0" "LSP_arith: float"
lsp_is "$LSP_A" "(zerop 0)" 1 "T" "LSP_logic: zerop"
lsp_is "$LSP_A" "(minusp -2)" 1 "T" "LSP_logic: minusp"
lsp_is "$LSP_A" "(sin 0)" 1 "0.0" "LSP_arith: sin 0"
lsp_is "$LSP_A" "(cos 0)" 1 "1.0" "LSP_arith: cos 0"
# sensitivity: the comparison helper can fail (a wrong expectation is reported as FAIL, not ok)
if [ "$(lsp_is "$LSP_A" "(+ 1 2 3)" 1 "7" "sens" | grep -c '^FAIL')" = 1 ]; then lsp_ok "LSP_sens: lsp_is reports a wrong expectation (7 for (+ 1 2 3)) as FAIL"; else lsp_bad "LSP_sens: lsp_is accepted a wrong expectation"; fi

# ---- driving the app: (command ...), c: commands, entlast, ssget ---------------
lsp_vol "$LSP_A" 1 "$((2*2*3))" "LSP_cmd: (command \"Box\" ...) drew a 2x2x3 box"
lsp_is "$LSP_A" "(sslength (ssget \"X\"))" 1 "1" "LSP_cmd: ssget X sees the one box"
lsp_is "$LSP_A" "(entlast)" 1 "1" "LSP_cmd: entlast is the first object id"
lsp_vol "$LSP_A" 2 "$((4*4*4))" "LSP_c: the c:mybox command typed as 'mybox' drew a box from the variable sz=4"
lsp_vol "$LSP_A" 3 "$((5*5*5))" "LSP_c: after (setq sz 5) the same command reads the new value; (entlast) selected through (command \"SelID\" (entlast)) is that 5x5x5 box"
lsp_is "$LSP_A" "(entlast)" 2 "3" "LSP_cmd: entlast is the third object after the second mybox"
LSP_LEN="$(sed "s/^history: //" <<<"$LSP_A" | grep -m1 -o 'ength = [0-9.]*' || true)"
if [ "${LSP_LEN#ength = }" = "10" ]; then lsp_ok "LSP_cmd: Length of the Line drawn with an Enter reads 10"; else lsp_bad "LSP_cmd: line length read '$LSP_LEN'"; fi
lsp_is "$LSP_A" "(sslength (ssget \"X\"))" 2 "9" "LSP_cmd: 3 boxes + 1 line + 5 points from a while loop = 9 objects (also checked by @expect_objects 9)"

# ---- negative controls: nothing half-done, the app stays alive ------------------
lsp_has "$LSP_A" "(+ 1 2" 1 "malformed list: missing close parenthesis" "LSP_neg: an unbalanced expression is reported"
lsp_has "$LSP_A" "(+ 1 2))" 1 "extra close parenthesis" "LSP_neg: an extra close parenthesis is reported"
lsp_has "$LSP_A" "(nosuchfn 1)" 1 "no function definition: NOSUCHFN" "LSP_neg: an undefined function is reported by name"
lsp_has "$LSP_A" "(/ 1 0)" 1 "divide by zero" "LSP_neg: integer division by zero"
lsp_has "$LSP_A" "(+ 1 \"a\")" 1 "bad argument type" "LSP_neg: a string where a number is needed"
lsp_all "$LSP_A" "Lisp error: command failed: Box" 1 "LSP_neg: a command that cannot complete with the given arguments is an error"
lsp_is "$LSP_A" "(null after)" 1 "T" "LSP_neg: the statement after the failed command did not run (after is still unset)"
lsp_all "$LSP_A" "Lisp error: command failed: NoSuchCmdXYZ" 1 "LSP_neg: an unknown command name"
lsp_all "$LSP_A" "Lisp error: command failed: 5" 1 "LSP_neg: a non-command first argument"
lsp_has "$LSP_A" "(defun c:Box () 1)" 1 "would replace the built-in command Box" "LSP_neg: c:Box may not replace the real Box"
lsp_vol "$LSP_A" 4 "1" "LSP_neg: after the refused c:Box, typing Box still runs the real command (a 1x1x1 box, Volume 1)"
lsp_has "$LSP_A" "(while T 1)" 1 "evaluation step limit exceeded" "LSP_neg: an endless loop is stopped"
lsp_has "$LSP_A" "(r 0)" 1 "recursion too deep" "LSP_neg: endless recursion is stopped"
lsp_has "$LSP_A" "(defun car (x) 1)" 1 "cannot redefine the built-in function CAR" "LSP_neg: a built-in cannot be redefined by defun"
lsp_has "$LSP_A" "(setq car 1)" 1 "cannot redefine the built-in function CAR" "LSP_neg: nor by setq"
lsp_has "$LSP_A" "bad" 1 "Lisp error in c:bad: divide by zero" "LSP_neg: an error inside a c: command is reported with the command's name"
lsp_has "$LSP_A" "(ssget \"W\")" 1 "only (ssget \"X\") is supported" "LSP_neg: an unsupported ssget filter is refused, not ignored"
lsp_has "$LSP_A" "Lisp" 1 "give an expression" "LSP_neg: Lisp with no expression warns"
lsp_has "$LSP_A" "LoadLisp" 1 "give a .lsp file" "LSP_neg: LoadLisp with no file warns"
lsp_has "$LSP_A" "LoadLisp /nonexistent/none.lsp" 1 "cannot open /nonexistent/none.lsp" "LSP_neg: LoadLisp of a missing file"
lsp_has "$LSP_A" "(load \"/nonexistent/none.lsp\")" 1 "can't open file" "LSP_neg: (load) of a missing file"
lsp_has "$LSP_A" "(car 5)" 1 "bad argument type: listp" "LSP_neg: car of a number"
lsp_is "$LSP_A" "(list (+ 1 1))" 1 "(2)" "LSP_neg: after every error above the interpreter still evaluates (the last control)"

# ---- files ---------------------------------------------------------------------
lsp_has "$LSP_A" "(load \"$TMPW/lsp_good.lsp\")" 1 "Lisp: 144" "LSP_file: (load) evaluates a multi-line file with line and block comments and returns its last value (sq 12 = 144)"
lsp_is "$LSP_A" "(sq 12)" 1 "$((12*12))" "LSP_file: a function defined in the loaded file is callable afterwards"
lsp_vol "$LSP_A" 5 "$((9*9*9))" "LSP_file: the c:sqbox command defined in the file (typed as 'sqbox') drew a 9x9x9 box"
lsp_has "$LSP_A" "LoadLisp $TMPW/lsp_syntax_bad.lsp" 1 "malformed list" "LSP_file: LoadLisp reports a syntax error in the file"
lsp_is "$LSP_A" "(list zz)" 1 "(nil)" "LSP_file: ...and the form before the syntax error did NOT run (the file is parsed first)"
lsp_has "$LSP_A" "LoadLisp $TMPW/lsp_runtime_bad.lsp" 1 "divide by zero" "LSP_file: LoadLisp reports a run-time error"
lsp_is "$LSP_A" "(list z1 z2)" 1 "(1 nil)" "LSP_file: ...forms before the failing one ran (z1 = 1), forms after it did not (z2 unset)"
lsp_has "$LSP_A" "RunScript $TMPW/lsp_good2.lsp" 1 "LoadLisp: loaded $TMPW/lsp_good2.lsp" "LSP_file: RunScript accepts a .lsp file"
lsp_is "$LSP_A" "(list rs_loaded)" 1 "(42)" "LSP_file: RunScript of the .lsp defined rs_loaded = 42"
lsp_vol "$LSP_A" 6 "$((7*3*2))" "LSP_file: the c:rsbox from the RunScript'd file drew a 7x3x2 box"

# ---- restart: acad.lsp autoload ---------------------------------------------------
LSP_B="$(lsp_exec "$LSP_CFG_B" "$HERE/lisp_persist_script.txt")" || { echo "$LSP_B"; lsp_bad "LSP script B exited non-zero"; }
grep -q "^smoke:" <<<"$LSP_B" || lsp_bad "LSP_B: no smoke line"
if grep -q "Lisp: autoloaded acad.lsp" <<<"$LSP_B"; then lsp_ok "LSP_auto: the app announces it autoloaded acad.lsp from the config directory"; else lsp_bad "LSP_auto: no autoload line"; fi
lsp_vol "$LSP_B" 1 "$((2*3*4))" "LSP_auto: 'autobox', a c: command that exists only because the NEW process autoloaded acad.lsp, drew a 2x3x4 box"
lsp_is "$LSP_B" "(list auto_loaded)" 1 "(7)" "LSP_auto: a variable set in acad.lsp is present in the new process"
lsp_is "$LSP_B" "(autobox_helper 5)" 1 "25" "LSP_auto: a function defined in acad.lsp is callable in the new process"
LSP_C="$(lsp_exec "$LSP_CFG_A" "$HERE/lisp_nopersist_script.txt")" || { echo "$LSP_C"; lsp_bad "LSP script C exited non-zero"; }
if grep -q "Lisp: autoloaded" <<<"$LSP_C"; then lsp_bad "LSP_auto_neg: a process with no acad.lsp claims an autoload"; else lsp_ok "LSP_auto_neg: no acad.lsp, no autoload line"; fi
lsp_has "$LSP_C" "autobox" 1 "Unknown command: autobox" "LSP_auto_neg: without acad.lsp 'autobox' is an unknown command"
lsp_is "$LSP_C" "(list auto_loaded)" 1 "(nil)" "LSP_auto_neg: without acad.lsp the variable is unset"
lsp_has "$LSP_C" "(autobox_helper 5)" 1 "no function definition: AUTOBOX_HELPER" "LSP_auto_neg: without acad.lsp the helper is undefined"
lsp_has "$LSP_C" "mybox" 1 "Unknown command: mybox" "LSP_auto_neg: a c: command typed in the first process (mybox) did not survive into a new process"
lsp_has "$LSP_C" "(fact 3)" 1 "no function definition: FACT" "LSP_auto_neg: nor did a defun from the first process"
lsp_vol "$LSP_C" 1 "$((1*1*2))" "LSP_auto_neg: the normal Box command still works in that process (1x1x2)"
LSP_D="$(lsp_exec "$LSP_CFG_D" "$HERE/lisp_brokenauto_script.txt")" || { echo "$LSP_D"; lsp_bad "LSP script D exited non-zero"; }
if grep -q "acad.lsp error: malformed list" <<<"$LSP_D"; then lsp_ok "LSP_auto_neg: a syntax error in acad.lsp is reported"; else lsp_bad "LSP_auto_neg: broken acad.lsp not reported"; fi
lsp_has "$LSP_D" "okbox" 1 "Unknown command: okbox" "LSP_auto_neg: nothing of the broken acad.lsp ran (its c:okbox, written before the error, does not exist)"
lsp_is "$LSP_D" "(list broken_marker)" 1 "(nil)" "LSP_auto_neg: nor did its setq"
lsp_vol "$LSP_D" 1 "$((1*1*2))" "LSP_auto_neg: the app still works after a broken acad.lsp (Box 1x1x2)"
