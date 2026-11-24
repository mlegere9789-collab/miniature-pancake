# ZZDBG3 (debug only): Silhouette of a sphere at x = 0 / 400 in several views, with mesh and silhouette-graph statistics
{
  for ZZX in 0 400; do
    printf 'New\nSphere %s,0,0 5\nSelPolysrf\nSilhouette\n' "$ZZX"
    for ZZV in Top Front Right Perspective; do printf 'SetActiveViewport %s\nSelPolysrf\nSilhouette\n' "$ZZV"; done
  done
} > "$TMPW/zzdbg3_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ZZ3="$(DINO8_SIL_DEBUG=1 timeout 300 "$BIN" --smoke 60 --script "$TMPW/zzdbg3_script.txt" 2>&1 || true)"
else
  ZZ3="$(DINO8_SIL_DEBUG=1 timeout 300 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/zzdbg3_script.txt" 2>&1 || true)"
fi
echo "$ZZ3" | grep -E "SILDBG|Silhouette:|Command: (Sphere|SetActiveViewport|New)" | sed 's/\r$//' | sed 's/^/ok   ZZDBG3 /' | cut -c1-600
