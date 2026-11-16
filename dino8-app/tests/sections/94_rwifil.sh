# ---------------------------------------------------------------------------
# RWIFIL: Save, SaveACopy, SaveAs, Revert and a failed save (rwi_file_script.txt); every .3dm file is read by the stand-alone rwi_oracle.py, which walks
# the openNURBS archive chunks itself (32 byte start string, 8 byte chunk lengths, object table 0x10000013, object type per record) and shares no code
# with the application. Rows: RHINO-FILE-0018 (Revert), 0019 (Save), 0020 (SaveACopy), 0021 (SaveAs; the new file becomes the current one) and
# BLENDER-WM-0045 (Save As: reading back reproduces the content, a failed save leaves the previous file intact).
# Expected values are counts of the script (two box breps, object type code 16):
#   a.3dm   saved with 2 boxes by Save <path>, then (after SaveACopy and deleting one box) by a bare Save: 1 brep. A SaveACopy that moved the
#           current file would have sent that bare Save to c.3dm instead
#   c.3dm   the copy made before the delete: 2 breps (unchanged by the later bare Save)
#   b.3dm   SaveAs then a box added and a bare Save: 2 breps; the bare Save went to b.3dm, so SaveAs made it the current file
#   Revert  after a third box was added: back to 2 objects (the file's content), @expect_objects in the script
#   failed  SaveAs into a folder that does not exist reports an error and writes nothing; SaveAs over the existing c.3dm with an invalid
#           archive version (Version=55) reports an error and c.3dm still holds its 2 breps (a refused save does not touch the earlier file);
#           the document keeps its 2 objects. A write that fails half way through an overwrite is not simulated here
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIFIL_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwifil"; mkdir -p "$RWI_D"
sed -e "s|@TMP@|$RWI_D|g" "$HERE/rwi_file_script.txt" > "$TMPW/rwifil_script.txt"
rwi_app "$TMPW/rwifil_script.txt"
rwi_has "Saved $RWI_D/a.3dm" "RWIFIL: Save <path> wrote a.3dm"
rwi_has "Saved $RWI_D/c.3dm" "RWIFIL: SaveACopy wrote c.3dm"
rwi_has "Saved $RWI_D/b.3dm" "RWIFIL: SaveAs wrote b.3dm"
rwi_oracle "RWIFIL: a.3dm (Save) holds 1 brep: the bare Save after SaveACopy went to a.3dm" 3dm "$RWI_D/a.3dm" --parts 1 --types 16
rwi_has "not a valid .3dm archive version" "RWIFIL: SaveAs over c.3dm with Version=55 is refused with a message"
rwi_oracle "RWIFIL: c.3dm (SaveACopy) holds the 2 breps of the copy, also after the refused SaveAs over it" 3dm "$RWI_D/c.3dm" --parts 2 --types 16,16
rwi_oracle "RWIFIL: b.3dm (SaveAs, then Save) holds 2 breps: SaveAs made b.3dm the current file" 3dm "$RWI_D/b.3dm" --parts 2 --types 16,16
if [ -e "$RWI_D/no_such_folder/x.3dm" ]; then rwi_bad "RWIFIL: SaveAs into a missing folder created a file"; else rwi_ok "RWIFIL: SaveAs into a missing folder wrote nothing"; fi
rwi_hasnt "Saved $RWI_D/no_such_folder/x.3dm" "RWIFIL: SaveAs into a missing folder is not reported as saved"
fi
