# ---------------------------------------------------------------------------
# COL_: the collaboration commands take a token, and the LAN bind is opt-in and needs one (the protocol itself is proved by the
# standalone ctest target dino8_test_collab_auth: tests/test_collab_auth.cpp - HMAC challenge/response against RFC 4231 vectors, wrong
# token / no token / raw unauthenticated frame rejected and never relayed, idle connection dropped, LAN bind).
# Before this, CollabHost always bound 127.0.0.1 only (so "CollabJoin Host=<this machine's IP>" in its own message could never work),
# had no authentication, and CollabStatus said nothing about either. Now:
#   CollabHost Token=<>= 8 chars> [Lan=Yes]   token required; Lan=Yes listens on every interface and is refused without a token
#   CollabJoin ... Token=<same>              authenticates by HMAC-SHA256 over a per-connection nonce (the token is never sent)
#   CollabStatus                              adds ", token required, N connection(s) rejected" when a token is in force
# One process (the commands' own output); two-process behaviour is covered over real sockets by the ctest target above.
# ---------------------------------------------------------------------------
col_ok() { echo "ok   $1"; }
col_bad() { echo "FAIL $1"; fail=1; }
COL_DIR="$TMPW/col"; COL_CFG="$COL_DIR/config"; mkdir -p "$COL_CFG"
cat > "$COL_DIR/s.txt" <<EOS
@wait 3
COLBEGIN_lan
CollabHost Lan=Yes
COLBEGIN_short
CollabHost Token=abc
COLBEGIN_host
CollabHost Token=correct-horse-9
CollabStatus
CollabLeave
COLBEGIN_plain
CollabHost
CollabStatus
CollabLeave
COLBEGIN_lanok
CollabHost Lan=Yes Token=correct-horse-9
CollabStatus
CollabLeave
COLBEGIN_joinshort
CollabJoin Port=1 Token=short
COLBEGIN_end
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  COL_O="$(XDG_CONFIG_HOME="$COL_CFG" HOME="$COL_CFG" timeout 600 "$BIN" --smoke 30 --script "$COL_DIR/s.txt" 2>&1)" || { echo "$COL_O" | tail -20; col_bad "COL_run: app exited non-zero"; COL_O=""; }
else
  COL_O="$(XDG_CONFIG_HOME="$COL_CFG" HOME="$COL_CFG" timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$COL_DIR/s.txt" 2>&1)" || { echo "$COL_O" | tail -20; col_bad "COL_run: app exited non-zero"; COL_O=""; }
fi
col_block() { awk -v name="$1" '$0 ~ ("Unknown command: COLBEGIN_" name "$") { on = 1; next } on && /Unknown command: COLBEGIN_/ { exit } on { print }' <<<"$COL_O"; }
col_line() { if grep -qxF -- "history: $2" <<<"$(col_block "$1")"; then col_ok "$3"; else col_bad "$3"; echo "     expected line: history: $2"; col_block "$1" | head -4 | sed 's/^/     actual:        /'; fi; }
col_has() { if grep -qF -- "$2" <<<"$(col_block "$1")"; then col_ok "$3"; else col_bad "$3"; echo "     expected text: $2"; col_block "$1" | head -3 | sed 's/^/     actual:        /'; fi; }
col_hasnot() { if grep -qF -- "$2" <<<"$(col_block "$1")"; then col_bad "$3"; else col_ok "$3"; fi; }

col_line lan "! CollabHost: Lan=Yes needs a Token (at least 8 characters): without one the relay would accept any machine that can reach the port" "COL_lan: Lan=Yes without a token is refused"
col_hasnot lan "hosting on" "COL_lan: ... and nothing was started"
col_line short "! CollabHost: the Token must be at least 8 characters" "COL_short: a 3-character token is refused"
col_has host "CollabHost: hosting on 127.0.0.1:" "COL_host: a token host listens on loopback by default"
col_has host "token required" "COL_host: ... and says a token is required"
col_line host "CollabStatus: Host, 0 peer(s), 1 snapshot(s) sent, 0 applied this session, token required, 0 connection(s) rejected" "COL_host: CollabStatus reports the token and 0 rejected connections"
col_has plain "CollabHost: hosting on 127.0.0.1:" "COL_plain: a tokenless host still works as before"
col_hasnot plain "token required" "COL_plain: ... with no token mentioned"
col_line plain "CollabStatus: Host, 0 peer(s), 1 snapshot(s) sent, 0 applied this session" "COL_plain: the tokenless CollabStatus line is unchanged"
col_has lanok "CollabHost: hosting on 0.0.0.0:" "COL_lanok: Lan=Yes with a token listens on every interface"
col_has lanok "(LAN" "COL_lanok: ... and says so"
col_line lanok "CollabStatus: Host, 0 peer(s), 1 snapshot(s) sent, 0 applied this session, token required, 0 connection(s) rejected" "COL_lanok: CollabStatus reports the token"
col_line joinshort "! CollabJoin: the Token must be at least 8 characters" "COL_join: a too-short token is refused before connecting"

# ---- two real processes: a token host, one joiner with the right token, one with a wrong token -------------------------------------
#   host   : CollabHost Token=correct-horse-9, then idles ~1500 frames and reports CollabStatus and its object count
#   joiner A (right token): authenticates (no "waiting for the host" in its status),
#            then draws one Point; the host must end with exactly 1 object - the Point crossed the authenticated relay
#   joiner B (wrong token): is turned away; its CollabStatus then says the session ended because the host rejected the token, and
#            the host's own CollabStatus counts 1 rejected connection
if [ "$IS_WINDOWS" = 1 ]; then
  echo "skip COL_xproc (not run on Windows)"
else
  col_app() {  # col_app <config home> <script> <frames>
    if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
      XDG_CONFIG_HOME="$1" HOME="$1" timeout 900 "$BIN" --smoke "$3" --script "$2" 2>&1
    else
      XDG_CONFIG_HOME="$1" HOME="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke "$3" --script "$2" 2>&1
    fi
  }
  mkdir -p "$COL_DIR/cfg_host" "$COL_DIR/cfg_a" "$COL_DIR/cfg_b"
  printf '%s\n' '@wait 3' 'CollabHost Token=correct-horse-9' '@wait 1500' 'COLBEGIN_hostend' 'CollabStatus' > "$COL_DIR/host.txt"
  col_app "$COL_DIR/cfg_host" "$COL_DIR/host.txt" 1520 > "$COL_DIR/host.out" 2>&1 &
  COL_HPID=$!
  COL_PORT=""
  for _ in $(seq 1 120); do
    COL_PORT="$(sed -n 's/^history: CollabHost: hosting on 127\.0\.0\.1:\([0-9]*\),.*/\1/p' "$COL_DIR/host.out" 2>/dev/null | head -1)"
    [ -n "$COL_PORT" ] && break
    sleep 2
  done
  if [ -z "$COL_PORT" ]; then
    col_bad "COL_xproc: the host process never reported its port"; kill "$COL_HPID" 2>/dev/null || true
  else
    printf '%s\n' '@wait 3' "CollabJoin Port=$COL_PORT Token=correct-horse-9" '@wait 80' 'COLBEGIN_a1' 'CollabStatus' 'Point 5,6,7' '@wait 60' 'COLBEGIN_a2' 'CollabStatus' > "$COL_DIR/ja.txt"
    printf '%s\n' '@wait 3' "CollabJoin Port=$COL_PORT Token=wrong-token-xyz" '@wait 80' 'COLBEGIN_b1' 'CollabStatus' > "$COL_DIR/jb.txt"
    COL_JA="$(col_app "$COL_DIR/cfg_a" "$COL_DIR/ja.txt" 160)" || { echo "$COL_JA" | tail -5; col_bad "COL_xproc: joiner A exited non-zero"; COL_JA=""; }
    COL_JB="$(col_app "$COL_DIR/cfg_b" "$COL_DIR/jb.txt" 100)" || { echo "$COL_JB" | tail -5; col_bad "COL_xproc: joiner B exited non-zero"; COL_JB=""; }
    wait "$COL_HPID" || col_bad "COL_xproc: the host exited non-zero"
    COL_H="$(cat "$COL_DIR/host.out")"
    col_blk() { awk -v name="$1" '$0 ~ ("Unknown command: COLBEGIN_" name "$") { on = 1; next } on && /Unknown command: COLBEGIN_/ { exit } on { print }' <<<"$2"; }
    if grep -qE "^history: CollabStatus: Joined, 0 peer\(s\), [0-9]+ snapshot\(s\) sent, [0-9]+ applied this session$" <<<"$(col_blk a1 "$COL_JA")"; then
      col_ok "COL_xproc: joiner A authenticated (Joined, and no "waiting for the host to accept the token" suffix)"
    else col_bad "COL_xproc: joiner A did not authenticate"; col_blk a1 "$COL_JA" | head -3 | sed 's/^/     actual: /'; fi
    if grep -qxF "history: CollabStatus: no active session (CollabHost or CollabJoin to start one); the last session ended: collab client: the host rejected the token" <<<"$(col_blk b1 "$COL_JB")"; then
      col_ok "COL_xproc: joiner B (wrong token) was turned away and CollabStatus says the host rejected the token"
    else col_bad "COL_xproc: joiner B was not turned away with the expected message"; col_blk b1 "$COL_JB" | head -3 | sed 's/^/     actual: /'; fi
    if grep -qE "^history: CollabStatus: Host, [01] peer\(s\), [0-9]+ snapshot\(s\) sent, [1-9][0-9]* applied this session, token required, 1 connection\(s\) rejected$" <<<"$(col_blk hostend "$COL_H")"; then
      col_ok "COL_xproc: the host applied the authenticated joiner's edit and counts exactly 1 rejected connection"
    else col_bad "COL_xproc: unexpected host status"; col_blk hostend "$COL_H" | head -3 | sed 's/^/     actual: /'; fi
    if grep -qE "^smoke: frames=[0-9]+ objects=1 " <<<"$COL_H"; then col_ok "COL_xproc: the host document holds exactly 1 object - joiner A's Point crossed the authenticated relay"
    else col_bad "COL_xproc: the host did not end with 1 object"; { grep "^smoke:" <<<"$COL_H" || true; } | sed 's/^/     actual: /'; fi
  fi
fi
