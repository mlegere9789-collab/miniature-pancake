# ---------------------------------------------------------------------------
# NVW_: named views keep their zoom in .3dm (nvw_export_script.txt, nvw_reopen_script.txt). Save3dm wrote only a named view's camera and
# left the frustum at OpenNURBS' default, so a parallel view reopened 40 units tall whatever it was saved at and the lens of a
# perspective view was lost (read back as ~0 mm). It now writes a 4:3 screen port and a frustum with the saved height / 35 mm lens;
# Load3dm reads the lens back. view3dm_check is a plain OpenNURBS ONX_Model author/reader, independent of Save3dm / Load3dm.
#   export  : Top zoomed in once (printed height H), Perspective at 35 mm, both saved as named views, document saved
#   file    : TopV is parallel with frustum height H; PerspV is perspective with a 35 mm-film lens of 35
#   reopen  : restoring TopV prints height H again and PerspV lens 35 mm
#   fixture : OpenNURBS-authored RhinoTop (frustum 60 tall) restores to height 60, RhinoPersp (28 mm lens) to lens 28 mm
# ---------------------------------------------------------------------------
NVWBIN="$(dirname "$BIN")/view3dm_check"
if [ -x "$NVWBIN" ]; then
  mkdir -p "$TMPW/nvw"
  "$NVWBIN" fixture "$TMPW/nvw/nv_fixture.3dm" >/dev/null || { echo "FAIL NVW_fixture: view3dm_check could not author the fixture"; fail=1; }
  sed "s|@TMP@|$TMPW/nvw|g" "$HERE/nvw_export_script.txt" > "$TMPW/nvw_export_script.txt"
  sed "s|@TMP@|$TMPW/nvw|g" "$HERE/nvw_reopen_script.txt" > "$TMPW/nvw_reopen_script.txt"
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    NVE="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/nvw_export_script.txt" 2>&1)" || { echo "$NVE" | tail -20; echo "FAIL NVW_run: export script exited non-zero"; fail=1; NVE=""; }
    NVI="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/nvw_reopen_script.txt" 2>&1)" || { echo "$NVI" | tail -20; echo "FAIL NVW_run: reopen script exited non-zero"; fail=1; NVI=""; }
  else
    NVE="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/nvw_export_script.txt" 2>&1)" || { echo "$NVE" | tail -20; echo "FAIL NVW_run: export script exited non-zero"; fail=1; NVE=""; }
    NVI="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/nvw_reopen_script.txt" 2>&1)" || { echo "$NVI" | tail -20; echo "FAIL NVW_run: reopen script exited non-zero"; fail=1; NVI=""; }
  fi
  nvw_ok() { echo "ok   $1"; }
  nvw_bad() { echo "FAIL $1"; fail=1; }
  # the Nth "Camera (<view>): ..." line printed by the Camera command
  nvw_cam() { { grep -E "^history: Camera \(" <<<"$1" || true; } | sed -n "${2}p"; }
  nvw_height() { sed -nE 's/.*parallel projection, height ([0-9.eE+-]+).*/\1/p' <<<"$1"; }
  nvw_lens() { sed -nE 's/.*, lens ([0-9.eE+-]+) mm.*/\1/p' <<<"$1"; }
  nvw_close() { awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(v != "" && d <= t * (e < 0 ? -e : e)) }'; }

  NVW_H="$(nvw_height "$(nvw_cam "$NVE" 1)")"
  NVW_L="$(nvw_lens "$(nvw_cam "$NVE" 2)")"
  if [ -n "$NVW_H" ] && [ "$NVW_H" != "40" ]; then nvw_ok "NVW_export: the Top view was zoomed in to a height of $NVW_H (not the OpenNURBS default 40)"; else nvw_bad "NVW_export: Top height '${NVW_H}' (want a value other than the default 40)"; fi
  if nvw_close "$NVW_L" 35 0.000001; then nvw_ok "NVW_export: the Perspective lens is 35 mm"; else nvw_bad "NVW_export: Perspective lens '${NVW_L}' (want 35)"; fi

  NVW_D="$("$NVWBIN" dump "$TMPW/nvw/nv_export.3dm" 2>&1 || true)"
  NVW_TOP="$(grep -E "^view [0-9]+ name=TopV " <<<"$NVW_D" || true)"
  NVW_PER="$(grep -E "^view [0-9]+ name=PerspV " <<<"$NVW_D" || true)"
  grep -q "^views: 2$" <<<"$NVW_D" && nvw_ok "NVW_file: the saved file holds exactly 2 named views (read by plain OpenNURBS)" || nvw_bad "NVW_file: named view count is not 2"
  grep -q " persp=0 " <<<"$NVW_TOP" && nvw_ok "NVW_file: TopV is a parallel view" || nvw_bad "NVW_file: TopV is not parallel: $NVW_TOP"
  NVW_FH="$(sed -nE 's/.* frustum_h=([0-9.eE+-]+) .*/\1/p' <<<"$NVW_TOP")"
  if nvw_close "$NVW_FH" "${NVW_H:-0}" 0.0001; then nvw_ok "NVW_file: TopV's frustum is $NVW_FH tall = the saved view height $NVW_H (was the default 40)"; else nvw_bad "NVW_file: TopV frustum height '${NVW_FH}' vs the saved view height '${NVW_H}'"; fi
  grep -q " persp=1 " <<<"$NVW_PER" && nvw_ok "NVW_file: PerspV is a perspective view" || nvw_bad "NVW_file: PerspV is not perspective: $NVW_PER"
  NVW_FL="$(sed -nE 's/.* lens=([0-9.eE+-]+)$/\1/p' <<<"$NVW_PER")"
  if nvw_close "$NVW_FL" 35 0.0001; then nvw_ok "NVW_file: PerspV's 35 mm-film lens length is $NVW_FL (was ~0)"; else nvw_bad "NVW_file: PerspV lens '${NVW_FL}' (want 35)"; fi

  NVW_R1="$(nvw_height "$(nvw_cam "$NVI" 1)")"
  NVW_R2="$(nvw_lens "$(nvw_cam "$NVI" 2)")"
  NVW_R3="$(nvw_height "$(nvw_cam "$NVI" 3)")"
  NVW_R4="$(nvw_lens "$(nvw_cam "$NVI" 4)")"
  if [ -n "$NVW_R1" ] && nvw_close "$NVW_R1" "${NVW_H:-0}" 0.0001; then nvw_ok "NVW_reopen: restoring TopV in the reopened file gives height $NVW_R1 = the saved $NVW_H"; else nvw_bad "NVW_reopen: TopV restored to height '${NVW_R1}' (saved '${NVW_H}')"; { grep -E "^history: Camera \(" <<<"$NVI" || true; } | head -4 | sed 's/^/     actual:   /'; fi
  if nvw_close "$NVW_R2" 35 0.0001; then nvw_ok "NVW_reopen: restoring PerspV gives lens $NVW_R2 mm = the saved 35"; else nvw_bad "NVW_reopen: PerspV restored to lens '${NVW_R2}' (saved 35)"; fi
  if nvw_close "$NVW_R3" 60 0.0001; then nvw_ok "NVW_fixture: the OpenNURBS-authored RhinoTop (frustum 60 tall) restores to height $NVW_R3"; else nvw_bad "NVW_fixture: RhinoTop restored to height '${NVW_R3}' (want 60)"; fi
  if nvw_close "$NVW_R4" 28 0.0001; then nvw_ok "NVW_fixture: the OpenNURBS-authored RhinoPersp (28 mm lens) restores to lens $NVW_R4 mm"; else nvw_bad "NVW_fixture: RhinoPersp restored to lens '${NVW_R4}' (want 28)"; fi
else
  echo "FAIL view3dm_check was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the named-view .3dm checks"
  fail=1
fi
