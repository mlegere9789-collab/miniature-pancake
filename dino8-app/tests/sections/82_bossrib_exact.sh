# ---------------------------------------------------------------------------
# BRX_: Boss / Rib through the exact kernel route (bossrib_exact_script.txt). BossRibCommand calls kernel::MakeBoss / kernel::MakeRib
# (exact B-rep fusion) and falls back to the old mesh construction only when the kernel refuses, saying why. Expected values are closed
# forms of the fixture (box 10 x 10 x 10 = 1000), independent of the app's printout:
#   boss : r = 2 circle on the top face raised 5     -> 1000 + pi 2^2 5 = 1062.83
#   rib  : straight 6-long spine, height 2, thickness 1 (default) -> 1000 + 6 x 2 x 1 = 1012
#   arc rib spine: not a straight line -> the kernel declines, the mesh construction runs and the message names the reason
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  BRXO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/bossrib_exact_script.txt" 2>&1)" || { echo "$BRXO" | tail -30; echo "FAIL BRX_run: script exited non-zero"; fail=1; BRXO=""; }
else
  BRXO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/bossrib_exact_script.txt" 2>&1)" || { echo "$BRXO" | tail -30; echo "FAIL BRX_run: script exited non-zero"; fail=1; BRXO=""; }
fi
brx_ok() { echo "ok   $1"; }
brx_bad() { echo "FAIL $1"; fail=1; }
brx_block() { awk -v name="$1" '$0 ~ ("Unknown command: BRXBEGIN_" name "$") { on = 1; next } on && /Unknown command: BRXBEGIN_/ { exit } on { print }' <<<"$BRXO"; }
brx_has() { if grep -qF -- "$2" <<<"$(brx_block "$1")"; then brx_ok "$3"; else brx_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Boss|Rib|!)" <<<"$(brx_block "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi; }
brx_hasnot() { if grep -qF -- "$2" <<<"$(brx_block "$1")"; then brx_bad "$3"; else brx_ok "$3"; fi; }
brx_vol() { { grep -oE "Volume = [0-9.eE+-]+" <<<"$(brx_block "$1")" || true; } | tail -1 | sed -E 's/Volume = //'; }
brx_num() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then brx_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else brx_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }

brx_has boss "Boss: exact kernel route (MakeBoss)" "BRX_boss: the boss went through the exact kernel route"
brx_hasnot boss "built following the base's local surface normal" "BRX_boss: no mesh construction message"
brx_num "$(brx_vol boss)" 1062.83 0.002 "BRX_boss: volume = 1000 + pi 2^2 5"
brx_has rib "Rib: exact kernel route (MakeRib)" "BRX_rib: the rib went through the exact kernel route"
brx_num "$(brx_vol rib)" 1012 0.002 "BRX_rib: volume = 1000 + 6 x 2 x 1"
brx_has ribarc "the exact kernel route declined (the rib spine is not a straight line)" "BRX_ribarc: an arc spine declines the kernel route and says why"
brx_has ribarc "so this is the mesh construction" "BRX_ribarc: the mesh construction ran as the fallback"
