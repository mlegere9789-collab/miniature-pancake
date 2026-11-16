# ZZDBG2: debug only (Silhouette of a sphere at the origin and at 400,0,0)
for ZZX in 0 400; do
  printf 'Sphere %s,0,0 5\nSelPolysrf\nSilhouette\nSelCrv\nLength\n' "$ZZX" > "$TMPW/zzdbg2_$ZZX.txt"
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    ZZ="$(timeout 300 "$BIN" --smoke 40 --script "$TMPW/zzdbg2_$ZZX.txt" 2>&1 || true)"
  else
    ZZ="$(timeout 300 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 40 --script "$TMPW/zzdbg2_$ZZX.txt" 2>&1 || true)"
  fi
  echo "$ZZ" | grep -E "Silhouette:|length|Curve [0-9]+" | awk '!s[$0]++' | sed "s/^/ok   ZZDBG2 x=$ZZX /" | cut -c1-200
done
