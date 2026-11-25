# ---------------------------------------------------------------------------
# SUDOC_SELF: the SUDOC_* checkers (sudoc_rows.py) accept DXF files authored by hand and reject doctored copies; no application is run.
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip SUDOC_SELF (needs python3)"
else
  SUDOC_SELF_OUT="$(python3 "$HERE/sudoc_selftest.py" 2>&1; python3 "$HERE/sudoc_view.py" selftest 2>&1)" && SUDOC_SELF_RC=0 || SUDOC_SELF_RC=$?
  echo "$SUDOC_SELF_OUT" | grep -E '^(ok|FAIL)' || true
  if [ "$SUDOC_SELF_RC" != "0" ] || grep -q '^FAIL' <<<"$SUDOC_SELF_OUT"; then echo "FAIL SUDOC_SELF: the checker self-test failed"; echo "$SUDOC_SELF_OUT" | tail -5; fail=1; fi
fi
