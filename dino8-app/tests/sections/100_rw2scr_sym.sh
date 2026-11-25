# ---------------------------------------------------------------------------
# RW2SCR_SYM: symbol functions of the AutoLISP-style dialect (src/script/Lisp.cpp): vl-symbolp, vl-symbol-value, set, atoms-family, vl-bb-*, vl-list*.
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0368, -0367, -0267, -0125, -0310, -0309 (and the supported part of -0334).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic (their own printf, integer and floating point), never with the
# interpreter under test. One real app process runs rw2scr_sym_script.txt; each result is read from the "Lisp: <value>" line
# printed after the typed expression. Self-contained (own helpers) so that `run_sections.sh RW2SCR_SYM` works alone.
# ---------------------------------------------------------------------------

r2s_exec() {  # r2s_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r2s_ok() { echo "ok   $1"; }
r2s_bad() { echo "FAIL $1"; fail=1; }
r2s_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | R2S_CMD="Command: $2" R2S_NTH="$3" awk 'BEGIN { n = ENVIRON["R2S_NTH"] + 0; cmd = ENVIRON["R2S_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r2s_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r2s_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r2s_ok "$5 ($2 -> $got)"; else r2s_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r2s_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r2s_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r2s_ok "$5"; else r2s_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r2s_r() {  # a real the way the app prints it, from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}
r2s_f() {  # <printf format> <awk expression>: a formatted number
  awk -v PI=3.14159265358979323846 -v fmt="$1" "BEGIN { printf fmt, $2 }"
}

if [ "$IS_WINDOWS" = 1 ]; then R2S_SUB="Dino8"; else R2S_SUB="dino8"; fi
R2S_CFG="$TMP/r2s_cfg"
mkdir -p "$R2S_CFG/$R2S_SUB"
R2S="$(r2s_exec "$R2S_CFG" "$HERE/rw2scr_sym_script.txt")" || { echo "$R2S" | tail -20; r2s_bad "RW2SCR_SYM script exited non-zero"; }
grep -q "^smoke:" <<<"$R2S" || { echo "$R2S" | tail -20; r2s_bad "RW2SCR_SYM: no smoke line (the app did not reach its normal end)"; }

# ---- vl-symbolp (D8-AUTOCAD-LISP-0368) -------------------------------------------------------------------
r2s_is "$R2S" '(vl-symbolp '"'"'a)' 1 T 'R2S_vl-symbolp: a quoted symbol'
r2s_is "$R2S" '(vl-symbolp t)' 1 T 'R2S_vl-symbolp: T is a symbol (documented)'
r2s_is "$R2S" '(vl-symbolp nil)' 1 nil 'R2S_vl-symbolp: nil is not counted (documented)'
r2s_is "$R2S" '(vl-symbolp 1)' 1 nil 'R2S_vl-symbolp: a number'
r2s_is "$R2S" '(vl-symbolp (list 1))' 1 nil 'R2S_vl-symbolp: a list (documented)'
r2s_is "$R2S" '(vl-symbolp "a")' 1 nil 'R2S_vl-symbolp: a string'
r2s_err "$R2S" '(vl-symbolp)' 1 'too few arguments' 'R2S_vl-symbolp_neg: no argument'

# ---- vl-symbol-value (D8-AUTOCAD-LISP-0367) --------------------------------------------------------------
r2s_is "$R2S" '(setq rw2v 7)' 1 7 'R2S_vl-symbol-value: setup'
r2s_is "$R2S" '(vl-symbol-value '"'"'rw2v)' 1 7 'R2S_vl-symbol-value: the value bound to a symbol'
r2s_is "$R2S" '(vl-symbol-value '"'"'pi)' 1 "$(r2s_r "PI")" 'R2S_vl-symbol-value: PI'
r2s_is "$R2S" '(vl-symbol-value '"'"'t)' 1 T 'R2S_vl-symbol-value: T is itself'
r2s_is "$R2S" '(vl-symbol-value '"'"'rw2undefined)' 1 nil 'R2S_vl-symbol-value: an unbound symbol is nil'
r2s_is "$R2S" '(setq rw2l (list 1 2))' 1 '(1 2)' 'R2S_vl-symbol-value: setup of a list value'
r2s_is "$R2S" '(vl-symbol-value '"'"'rw2l)' 1 '(1 2)' 'R2S_vl-symbol-value: a list value'
r2s_err "$R2S" '(vl-symbol-value 5)' 1 'bad argument type' 'R2S_vl-symbol-value_neg: a number is not a symbol'
r2s_err "$R2S" '(vl-symbol-value "rw2v")' 1 'bad argument type' 'R2S_vl-symbol-value_neg: a string is not a symbol'

# ---- set (D8-AUTOCAD-LISP-0267): the symbol is chosen at run time ----------------------------------------
r2s_is "$R2S" '(setq rw2a '"'"'rw2b)' 1 RW2B 'R2S_set: setup, a holds the symbol B'
r2s_is "$R2S" '(set rw2a 640)' 1 640 'R2S_set: returns the value (documented)'
r2s_is "$R2S" '(list rw2a rw2b)' 1 '(RW2B 640)' 'R2S_set: b is 640 and a is still the symbol B (documented)'
r2s_is "$R2S" '(set '"'"'rw2c '"'"'(1 2 3))' 1 '(1 2 3)' 'R2S_set: a list value'
r2s_is "$R2S" '(length rw2c)' 1 3 'R2S_set: the symbol holds the list'
r2s_is "$R2S" '(set (read "rw2d") (+ 1 2))' 1 3 'R2S_set: a symbol read from a string'
r2s_is "$R2S" '(progn rw2d)' 1 3 'R2S_set: and it is bound'
r2s_err "$R2S" '(set '"'"'car 5)' 1 'cannot redefine' 'R2S_set_neg: a built-in function is protected'
r2s_err "$R2S" '(set 5 5)' 1 'bad argument type' 'R2S_set_neg: a number is not a symbol'
r2s_err "$R2S" '(set '"'"'rw2e)' 1 'too few arguments' 'R2S_set_neg: one argument'

# ---- atoms-family (D8-AUTOCAD-LISP-0125) -----------------------------------------------------------------
r2s_is "$R2S" '(atoms-family 1 '"'"'("CAR" "CDR" "XYZ"))' 1 '("CAR" "CDR" nil)' 'R2S_atoms-family: names, an undefined one is nil (documented)'
r2s_is "$R2S" '(atoms-family 0 '"'"'("car" "xyz"))' 1 '(CAR nil)' 'R2S_atoms-family: format 0 gives symbols'
r2s_is "$R2S" '(atoms-family 1 '"'"'("xyz"))' 1 '(nil)' 'R2S_atoms-family: asking does not create the symbol'
r2s_is "$R2S" '(atoms-family 1 '"'"'("rw2v" "setq" "defun"))' 1 '("RW2V" "SETQ" "DEFUN")' 'R2S_atoms-family: user variables and special forms are defined'
r2s_is "$R2S" '(atoms-family 1 nil)' 1 nil 'R2S_atoms-family: an empty list of names'
r2s_is "$R2S" '(if (member "CAR" (atoms-family 1)) T)' 1 T 'R2S_atoms-family: the full list has CAR'
r2s_is "$R2S" '(if (member "RW2V" (atoms-family 1)) T)' 1 T 'R2S_atoms-family: the full list has the variable defined above'
r2s_is "$R2S" '(if (member "XYZ" (atoms-family 1)) T)' 1 nil 'R2S_atoms-family: the full list has no XYZ'
r2s_is "$R2S" '(type (car (atoms-family 0)))' 1 SYM 'R2S_atoms-family: format 0 elements are symbols'
r2s_is "$R2S" '(type (car (atoms-family 1)))' 1 STR 'R2S_atoms-family: format 1 elements are strings'
r2s_err "$R2S" '(atoms-family 2)' 1 'bad argument value' 'R2S_atoms-family_neg: format 2 does not exist'
r2s_err "$R2S" '(atoms-family)' 1 'too few arguments' 'R2S_atoms-family_neg: no argument'

# ---- vl-bb-set and vl-bb-ref (D8-AUTOCAD-LISP-0310, -0309): the blackboard shared by all namespaces ------
r2s_is "$R2S" '(vl-bb-set '"'"'foobar "Root toot toot")' 1 '"Root toot toot"' 'R2S_vl-bb-set: returns the value (documented)'
r2s_is "$R2S" '(vl-bb-ref '"'"'foobar)' 1 '"Root toot toot"' 'R2S_vl-bb-ref: reads it back (documented)'
r2s_is "$R2S" '(vl-bb-set '"'"'rw2n 42)' 1 42 'R2S_vl-bb-set: a number'
r2s_is "$R2S" '(vl-bb-ref '"'"'rw2n)' 1 42 'R2S_vl-bb-ref: the number'
r2s_is "$R2S" '(vl-bb-set '"'"'rw2n '"'"'(1 2))' 1 '(1 2)' 'R2S_vl-bb-set: replaces the value'
r2s_is "$R2S" '(vl-bb-ref '"'"'rw2n)' 2 '(1 2)' 'R2S_vl-bb-ref: the list'
r2s_is "$R2S" '(vl-bb-ref '"'"'rw2never)' 1 nil 'R2S_vl-bb-ref: a symbol never set is nil'
r2s_is "$R2S" '(boundp '"'"'rw2n)' 1 nil 'R2S_vl-bb-set: the blackboard is not the ordinary namespace'
r2s_err "$R2S" '(vl-bb-set 5 1)' 1 'bad argument type' 'R2S_vl-bb-set_neg: a number is not a symbol'
r2s_err "$R2S" '(vl-bb-ref)' 1 'too few arguments' 'R2S_vl-bb-ref_neg: no argument'

# ---- vl-list* (partial: a trailing list, or one atom after one object; longer dotted lists are refused, row D8-AUTOCAD-LISP-0334 stays open) ---
r2s_is "$R2S" '(vl-list* 1)' 1 1 'R2S_vl-list*: one argument is returned as is'
r2s_is "$R2S" '(vl-list* 1 2 3 '"'"'(4 5))' 1 '(1 2 3 4 5)' 'R2S_vl-list*: a trailing list is joined on'
r2s_is "$R2S" '(vl-list* 1 '"'"'(2 3))' 1 '(1 2 3)' 'R2S_vl-list*: one object and a list'
r2s_is "$R2S" '(vl-list* 1 2 nil)' 1 '(1 2)' 'R2S_vl-list*: a nil tail'
r2s_is "$R2S" '(vl-list* 0 "text")' 1 '(0 . "text")' 'R2S_vl-list*: one atom after one object is a dotted pair'
r2s_err "$R2S" '(vl-list* 1 2 3)' 1 'not supported' 'R2S_vl-list*_neg: a dotted list of three terms is not representable'
r2s_err "$R2S" '(vl-list*)' 1 'too few arguments' 'R2S_vl-list*_neg: no argument'
