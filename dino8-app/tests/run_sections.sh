#!/usr/bin/env bash
# Run only some sections of smoke.sh (and/or tests/sections/*.sh) against a Dino8 binary.
#
#   run_sections.sh [options] NAME-OR-PREFIX...
#     -b BIN      Dino8 binary (default: $DINO8_BIN, else ../build/Dino8 like smoke.sh)
#     -j N        run up to N selected sections in parallel, each in its own temp dir and its own
#                 Xvfb display (default 1: all selected sections run in one script, in file order)
#     -t SECS     timeout per job (default 3000)
#     -l          list the sections found and exit
#     -k          keep the per-job directories (logs, assembled scripts) and print where
#
# How it works (see README_sections.md): smoke.sh's first lines up to the `check() {...}` definition are the
# harness (variables, helpers, the initial --smoke run). A SECTION of smoke.sh starts at a header comment
# line `# PREFIX: ...` (PREFIX = 2-6 capitals/digits, optional trailing `_`) that follows a blank line or a
# `# ----` rule, and runs to the line before the next such header (the last one to the final `exit $fail`).
# A section file tests/sections/<NN_>name.sh is selected by `name`. NAME matches a section name
# case-insensitively: an exact match wins, otherwise every section whose name starts with NAME.
# `base` selects the un-headed checks between the harness and the first header.
#
# The assembled script = harness + the selected section text (verbatim lines of smoke.sh, then section
# files sourced, as smoke.sh itself does) + `exit $fail`, run with SIGPIPE ignored (`trap '' PIPE`, like the
# CI runner) under `timeout`. Exit status is nonzero if any job exits nonzero or prints a FAIL line.
#
# Limits: a section that uses a helper function or variable defined in ANOTHER section of smoke.sh is not
# self-contained; it then fails here with "command not found" (name the defining section too, in the same job).
set -u
trap '' PIPE

HERE="$(cd "$(dirname "$0")" && pwd)"
SMOKE="$HERE/smoke.sh"
SECDIR="$HERE/sections"
BIN="${DINO8_BIN:-$HERE/../build/Dino8}"
JOBS=1
TMO=3000
LIST=0
KEEP=0
while getopts "b:j:t:lkh" o; do
  case "$o" in
    b) BIN="$OPTARG";;
    j) JOBS="$OPTARG";;
    t) TMO="$OPTARG";;
    l) LIST=1;;
    k) KEEP=1;;
    *) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 2;;
  esac
done
shift $((OPTIND - 1))
case "$JOBS" in ''|*[!0-9]*|0) echo "run_sections: -j needs a positive integer" >&2; exit 2;; esac

# --- parse smoke.sh: harness end line, section table "NAME START END" -----------------------------------
HEAD_END="$(grep -n '^check() {' "$SMOKE" | head -1 | cut -d: -f1)"
[ -n "$HEAD_END" ] || { echo "run_sections: cannot find the harness end (check() definition) in smoke.sh" >&2; exit 2; }
EXIT_LINE="$(grep -n '^exit \$fail' "$SMOKE" | tail -1 | cut -d: -f1)"
[ -n "$EXIT_LINE" ] || { echo "run_sections: cannot find the final 'exit \$fail' in smoke.sh" >&2; exit 2; }
# Everything the harness needs ends at HEAD_END. If smoke.sh sources sections/*.sh, that loop sits just
# before the final exit and is not part of any section.
SRC_LOOP_LINE="$(grep -n '^# >>> sections/\*.sh sourced here' "$SMOKE" | tail -1 | cut -d: -f1)"
BODY_END=$(( ${SRC_LOOP_LINE:-$EXIT_LINE} - 1 ))

TABLE="$(awk -v hend="$HEAD_END" -v bend="$BODY_END" '
  NR <= hend { prev = $0; next }
  NR > bend { exit }
  /^# [A-Z][A-Z0-9]{1,5}_?: / && (prev == "" || prev ~ /^# ---/) {
    if (name != "") print name, start, NR - 1
    n = $2; sub(/:$/, "", n); sub(/_$/, "", n); name = n; start = NR
  }
  { prev = $0 }
  END { if (name != "") print name, start, bend }
' "$SMOKE")"
FIRST_HDR="$(head -1 <<<"$TABLE" | awk '{print $2}')"
if [ -n "$FIRST_HDR" ]; then TABLE="BASE $((HEAD_END + 1)) $((FIRST_HDR - 1))"$'\n'"$TABLE"; fi

# section files: "NAME FILE"
FILES=""
if [ -d "$SECDIR" ]; then
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    b="$(basename "$f" .sh)"; n="$(sed -E 's/^[0-9]+_//' <<<"$b" | tr '[:lower:]' '[:upper:]')"
    FILES+="$n $f"$'\n'
  done < <(printf '%s\n' "$SECDIR"/*.sh 2>/dev/null | LC_ALL=C sort | grep -v '\*' || true)
fi

if [ "$LIST" = 1 ]; then
  echo "sections of smoke.sh (name first-line last-line):"; sed 's/^/  /' <<<"$TABLE"
  echo "section files in tests/sections/ (name file):"; [ -n "$FILES" ] && sed 's/^/  /' <<<"${FILES%$'\n'}"
  exit 0
fi
[ "$#" -ge 1 ] || { echo "usage: $0 [-b BIN] [-j N] [-t SECS] [-l] [-k] NAME-OR-PREFIX..." >&2; exit 2; }
[ -x "$BIN" ] || { echo "run_sections: Dino8 binary not found or not executable: $BIN" >&2; exit 2; }
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"

# --- select -----------------------------------------------------------------------------------------------
# SEL entries: "NAME|smoke|START|END" or "NAME|file|PATH"
declare -a SEL=()
for arg in "$@"; do
  a="$(tr '[:lower:]' '[:upper:]' <<<"${arg%_}")"
  found=0
  for mode in exact prefix; do
    while read -r n s e; do
      [ -n "$n" ] || continue
      if { [ $mode = exact ] && [ "$n" = "$a" ]; } || { [ $mode = prefix ] && [[ "$n" == "$a"* ]]; }; then SEL+=("$n|smoke|$s|$e"); found=1; fi
    done <<<"$TABLE"
    while read -r n f; do
      [ -n "$n" ] || continue
      if { [ $mode = exact ] && [ "$n" = "$a" ]; } || { [ $mode = prefix ] && [[ "$n" == "$a"* ]]; }; then SEL+=("$n|file|$f"); found=1; fi
    done <<<"$FILES"
    [ $found = 1 ] && break
  done
  [ $found = 1 ] || { echo "run_sections: no section matches '$arg' (try -l)" >&2; exit 2; }
done
# drop duplicates, keep order
declare -a UNIQ=(); declare -A SEEN=()
for s in "${SEL[@]}"; do [ -n "${SEEN[$s]:-}" ] || { SEEN[$s]=1; UNIQ+=("$s"); }; done
SEL=("${UNIQ[@]}")

WORK="$(mktemp -d "${TMPDIR:-/tmp}/run_sections.XXXXXX")"
cleanup() {
  local pf
  for pf in "$WORK"/job*/xvfb.pid; do [ -f "$pf" ] && kill "$(cat "$pf")" 2>/dev/null; done
  if [ "$KEEP" = 1 ]; then echo "run_sections: job directories kept in $WORK"; else rm -rf "$WORK"; fi
}
trap cleanup EXIT

# --- assemble one script -----------------------------------------------------------------------------------
# $1 = output path; remaining = SEL entries to include
assemble() {
  local out="$1"; shift
  # smoke.sh derives HERE from $0; the assembled script lives elsewhere, so pin it.
  sed -n "1,${HEAD_END}p" "$SMOKE" | sed "s|^HERE=.*|HERE=\"$HERE\"|" > "$out"
  local e
  for e in "$@"; do
    IFS='|' read -r n kind a b <<<"$e"
    echo "" >> "$out"
    if [ "$kind" = smoke ]; then sed -n "${a},${b}p" "$SMOKE" >> "$out"
    else echo ". \"$a\"" >> "$out"; fi
  done
  printf '\nexit $fail\n' >> "$out"
}

# --- run ---------------------------------------------------------------------------------------------------
REAL_XVFB_RUN="$(command -v xvfb-run || true)"
start_xvfb() {  # $1 = job dir; prints display number, records pid in $1/xvfb.pid
  local d n tries=0
  while [ $tries -lt 60 ]; do
    n=$((500 + (RANDOM % 4000)))
    if [ ! -e "/tmp/.X${n}-lock" ] && [ ! -e "/tmp/.X11-unix/X${n}" ]; then
      Xvfb ":$n" -screen 0 1600x900x24 -nolisten tcp >"$1/xvfb.log" 2>&1 &
      local pid=$!
      for _ in $(seq 1 50); do [ -e "/tmp/.X11-unix/X${n}" ] && { echo "$pid" > "$1/xvfb.pid"; echo "$n"; return 0; }; kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
      kill "$pid" 2>/dev/null
    fi
    tries=$((tries + 1))
  done
  return 1
}

run_job() {  # $1 = job index; rest = SEL entries (one job)
  local idx="$1"; shift
  local jd="$WORK/job$idx"; mkdir -p "$jd/tmp" "$jd/shim"
  assemble "$jd/script.sh" "$@"
  local disp=""
  if command -v Xvfb >/dev/null 2>&1; then
    disp="$(start_xvfb "$jd")" || { echo "run_sections: could not start an Xvfb display" > "$jd/log"; echo 97 > "$jd/rc"; return; }
    # The script calls `xvfb-run -a -s ... BIN ...` for every app launch. Inside this job those calls run on the
    # job's own persistent display instead of racing other jobs for a free display number.
    cat > "$jd/shim/xvfb-run" <<'SHIM'
#!/bin/bash
while [ $# -gt 0 ]; do
  case "$1" in
    -a|--auto-servernum) shift;;
    -s|--server-args|-n|--server-num|-e|--error-file|-f|--auth-file|-w|--wait) shift 2;;
    -*) shift;;
    *) break;;
  esac
done
exec "$@"
SHIM
    chmod +x "$jd/shim/xvfb-run"
  fi
  local s0; s0=$(date +%s)
  (
    trap '' PIPE
    cd "$jd" || exit 99
    export TMPDIR="$jd/tmp"
    if [ -n "$disp" ]; then export DISPLAY=":$disp"; export PATH="$jd/shim:$PATH"; else unset DISPLAY; fi
    exec timeout "$TMO" bash "$jd/script.sh" "$BIN"
  ) > "$jd/log" 2>&1
  echo $? > "$jd/rc"
  echo $(( $(date +%s) - s0 )) > "$jd/secs"
  [ -f "$jd/xvfb.pid" ] && kill "$(cat "$jd/xvfb.pid")" 2>/dev/null
  return 0
}

T0=$(date +%s)
declare -a JOBNAMES=()
if [ "$JOBS" -le 1 ]; then
  run_job 0 "${SEL[@]}"
  names=""; for e in "${SEL[@]}"; do names+="${e%%|*} "; done
  JOBNAMES+=("${names% }")
else
  i=0; running=0
  for e in "${SEL[@]}"; do
    JOBNAMES+=("${e%%|*}")
    run_job "$i" "$e" &
    i=$((i + 1)); running=$((running + 1))
    if [ "$running" -ge "$JOBS" ]; then wait -n; running=$((running - 1)); fi
  done
  wait
fi
WALL=$(( $(date +%s) - T0 ))

# --- merge -------------------------------------------------------------------------------------------------
rc=0; tot_ok=0; tot_fail=0
SUMMARY=""
for ((i = 0; i < ${#JOBNAMES[@]}; i++)); do
  jd="$WORK/job$i"
  jrc="$(cat "$jd/rc" 2>/dev/null || echo 98)"; secs="$(cat "$jd/secs" 2>/dev/null || echo 0)"
  nok=$(grep -c '^ok  ' "$jd/log" || true); nfail=$(grep -c '^FAIL' "$jd/log" || true)
  nbad=$(grep -c 'command not found' "$jd/log" || true)
  echo "================ ${JOBNAMES[$i]} (exit $jrc, ${secs}s, $nok ok, $nfail FAIL) ================"
  cat "$jd/log"
  tot_ok=$((tot_ok + nok)); tot_fail=$((tot_fail + nfail))
  [ "$jrc" = 0 ] || rc=1
  [ "$nfail" = 0 ] || rc=1
  SUMMARY+="$(printf '  %-12s exit %-3s %5ss  %5s ok  %3s FAIL%s' "${JOBNAMES[$i]}" "$jrc" "$secs" "$nok" "$nfail" "$([ "$nbad" != 0 ] && echo "  ($nbad 'command not found': needs a helper from another section?)")")"$'\n'
done
echo "================ summary (wall ${WALL}s, -j$JOBS) ================"
printf '%s' "$SUMMARY"
echo "run_sections: total $tot_ok ok, $tot_fail FAIL, result: $([ $rc = 0 ] && echo PASS || echo FAIL)"
exit $rc
