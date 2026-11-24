# VNDTXT_: the shipped data files carry no vendor-derived text (clean-room rule; see docs/clean-room/VENDOR_TEXT_SCRUB.md).
# tools/check_vendor_text.py reads data/commands.json, the docs-site copies, data/i18n/*.json and data/hatchpatterns.pat: every description
# has at most 25 words, none equals a description that was removed in the scrub (digests only), the help field is empty, and the product
# names McNeel / Rhinoceros appear nowhere outside the OpenNURBS credit. Its --self-test plants one violation of each kind in memory and
# requires every one to be reported, so the check cannot pass by being blind. No app process is started.
VNDTXT_PY="$HERE/../../tools/check_vendor_text.py"
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip VNDTXT_all (python3 not available)"
elif [ ! -f "$VNDTXT_PY" ]; then
  echo "FAIL VNDTXT_tool: $VNDTXT_PY is missing"; fail=1
else
  if VNDTXT_O="$(python3 -I "$VNDTXT_PY" 2>&1)"; then echo "ok   VNDTXT_data: $VNDTXT_O"
  else echo "$VNDTXT_O" | head -20; echo "FAIL VNDTXT_data: vendor-derived text found in the shipped data files"; fail=1; fi
  if VNDTXT_S="$(python3 -I "$VNDTXT_PY" --self-test 2>&1)"; then echo "ok   VNDTXT_self: $VNDTXT_S"
  else echo "$VNDTXT_S" | head -20; echo "FAIL VNDTXT_self: the checker missed a planted violation"; fail=1; fi
fi
