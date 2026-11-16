# ZZDBG: debug only (object counts after each block of srfedit_script.txt)
python3 - "$HERE/srfedit_script.txt" "$TMPW/zzdbg_script.txt" "$TMPW/zzdbg_labels.txt" <<'PY'
import sys
src, dst, lab = sys.argv[1:4]
out = []; labels = []
for ln in open(src).read().split('\n'):
    if ln.startswith('# ---'):
        out.append('@expect_objects 0'); labels.append(ln[:70])
    out.append(ln)
out.append('@expect_objects 0'); labels.append('END')
open(dst, 'w').write('\n'.join(out) + '\n'); open(lab, 'w').write('\n'.join(labels) + '\n')
PY
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ZZ="$(timeout 900 "$BIN" --smoke 150 --script "$TMPW/zzdbg_script.txt" 2>&1 || true)"
else
  ZZ="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMPW/zzdbg_script.txt" 2>&1 || true)"
fi
echo "$ZZ" | grep -o "expect_objects 0 (got [0-9]*)" | sed 's/expect_objects 0 (got \([0-9]*\))/\1/' > "$TMPW/zzdbg_counts.txt"
paste -d'|' "$TMPW/zzdbg_counts.txt" "$TMPW/zzdbg_labels.txt" | while IFS= read -r l; do echo "ok   ZZDBG count|$l"; done
echo "$ZZ" | grep -E "Hydrostatics|ExtractWireframe|Silhouette|Squish|Ribbon|SrfControlPtGrid|Isocurve|DupBorder|DupEdge" | head -40 | sed 's/^/ok   ZZDBGLOG /'
