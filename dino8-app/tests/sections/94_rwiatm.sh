# ---------------------------------------------------------------------------
# RWIATM: a .3dm save is atomic: the archive is written to a temporary file and renamed over the target only when complete, so a failed save leaves the previous
# file byte for byte as it was and no temporary file behind. Rows: BLENDER-WM-0045 and RHINO-FILE-0021 (Save As: a failed save leaves the previous file intact)
# are the statements this section proves further than RWIFIL could (which could not make a write fail half way). Files are compared by SHA-256 and read by the
# stand-alone rwi_oracle.py (openNURBS chunk walker). Failures are injected three ways: DINO8_TEST_FAULT_SAVE3DM=write (the temporary file is cut in half and the save
# fails, as an interrupted write would), =rename (a complete temporary file whose final rename fails), and a real one (a directory sits where the temporary file
# has to be created, so the write itself fails; the application deletes only a regular temporary file, so the directory stays). Expected values are counts and equalities of the script:
#   a.3dm  saved with one box (1 object); three failed overwrites by a document of box + sphere leave the SHA-256 as it was and the oracle still reads 1 object
#   no a.3dm.d8tmp is left by the injected failures; the failed SaveAs to a new name b.3dm writes no b.3dm and no b.3dm.d8tmp
#   a clean overwrite by the same document then succeeds: the file changes and holds 2 objects, with no temporary file left
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1 || ! command -v sha256sum >/dev/null 2>&1; then
  echo "skip RWIATM_all (needs python3 and sha256sum)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiatm"; rm -rf "$RWI_D"; mkdir -p "$RWI_D"
unset DINO8_TEST_FAULT_SAVE3DM
cat > "$TMPW/rwiatm_a.txt" <<EOS
Box 0,0,0 10,10,0 10
SaveAs $RWI_D/a.3dm
EOS
rwi_app "$TMPW/rwiatm_a.txt"
rwi_has "Saved $RWI_D/a.3dm" "RWIATM: the first save wrote a.3dm"
rwi_oracle "RWIATM: a.3dm holds 1 object" 3dm "$RWI_D/a.3dm" --parts 1
H1="$(sha256sum "$RWI_D/a.3dm" | cut -d' ' -f1)"
# the document that fails to overwrite: the file's box plus a sphere
cat > "$TMPW/rwiatm_f.txt" <<EOS
Open $RWI_D/a.3dm
Sphere 50,0,0 5
SaveAs $RWI_D/a.3dm
SaveAs $RWI_D/b.3dm
EOS
export DINO8_TEST_FAULT_SAVE3DM=write
rwi_app "$TMPW/rwiatm_f.txt"
unset DINO8_TEST_FAULT_SAVE3DM
rwi_has "injected write failure; the previous file was not touched" "RWIATM: an interrupted write is reported and the previous file declared untouched"
rwi_hasnt "Saved $RWI_D/a.3dm" "RWIATM: the interrupted write is not reported as saved"
if [ "$(sha256sum "$RWI_D/a.3dm" | cut -d' ' -f1)" = "$H1" ]; then rwi_ok "RWIATM: a.3dm is byte for byte the same after the interrupted write"; else rwi_bad "RWIATM: a.3dm changed after an interrupted write"; fi
rwi_absent "$RWI_D/a.3dm.d8tmp" "RWIATM: no temporary file is left by the interrupted write"
rwi_absent "$RWI_D/b.3dm" "RWIATM: the failed SaveAs to a new name wrote no b.3dm"
rwi_absent "$RWI_D/b.3dm.d8tmp" "RWIATM: the failed SaveAs to a new name left no temporary file"
export DINO8_TEST_FAULT_SAVE3DM=rename
rwi_app "$TMPW/rwiatm_f.txt"
unset DINO8_TEST_FAULT_SAVE3DM
rwi_has "injected rename failure; the previous file was not touched" "RWIATM: a failed final rename is reported and the previous file declared untouched"
if [ "$(sha256sum "$RWI_D/a.3dm" | cut -d' ' -f1)" = "$H1" ]; then rwi_ok "RWIATM: a.3dm is byte for byte the same after the failed rename"; else rwi_bad "RWIATM: a.3dm changed after a failed rename"; fi
rwi_absent "$RWI_D/a.3dm.d8tmp" "RWIATM: no temporary file is left by the failed rename"
rwi_absent "$RWI_D/b.3dm" "RWIATM: the failed rename wrote no b.3dm"
# a real failure: a directory occupies the temporary file's name
mkdir "$RWI_D/a.3dm.d8tmp"
rwi_app "$TMPW/rwiatm_f.txt"
rwi_has "OpenNURBS could not write $RWI_D/a.3dm" "RWIATM: a write that really fails is reported"
if [ "$(sha256sum "$RWI_D/a.3dm" | cut -d' ' -f1)" = "$H1" ]; then rwi_ok "RWIATM: a.3dm is byte for byte the same after the real write failure"; else rwi_bad "RWIATM: a.3dm changed after a real write failure"; fi
rmdir "$RWI_D/a.3dm.d8tmp" 2>/dev/null || true
rwi_oracle "RWIATM: a.3dm still holds 1 object after all three failures" 3dm "$RWI_D/a.3dm" --parts 1
# and a clean overwrite works and leaves no temporary file
rwi_app "$TMPW/rwiatm_f.txt"
rwi_has "Saved $RWI_D/a.3dm" "RWIATM: the clean overwrite is reported as saved"
rwi_oracle "RWIATM: a.3dm now holds 2 objects (the overwrite went through)" 3dm "$RWI_D/a.3dm" --parts 2
if [ "$(sha256sum "$RWI_D/a.3dm" | cut -d' ' -f1)" != "$H1" ]; then rwi_ok "RWIATM: the clean overwrite changed the file"; else rwi_bad "RWIATM: the clean overwrite left the file unchanged"; fi
rwi_absent "$RWI_D/a.3dm.d8tmp" "RWIATM: no temporary file is left by the clean overwrite"
fi
