# ---------------------------------------------------------------------------
# RWIIGS: IGES export (rwi_iges_script.txt), parsed by the stand-alone rwi_oracle.py (Python standard library only: 80-column records, the S G D P T
# sections, the T counts, every directory entry's parameter data, the global unit flag). Row: AUTOCAD-DWGFID-0069 (write chosen objects to a new
# .igs or .iges file, with a notification when it is done; the typed path stands for the file dialog's choice).
# Expected values are closed forms of the construction (two 10 mm boxes, millimetre document):
#   all.igs / all.iges  every record 80 columns with consecutive sequence numbers, the T record's counts equal the sections', global unit flag 2
#                       (millimetres in IGES), 12 trimmed surfaces (entity 144: 6 faces per box), bounding box (0,0,0)-(30,10,10) from the 110 line end
#                       points and the 128 surface control points
#   one.igs             only the selected box: 6 trimmed surfaces, bounding box (20,0,0)-(30,10,10)
#   rt.igs              the IGES read back and written again: the same trimmed-surface count and bounding box
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIIGS_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiigs"; mkdir -p "$RWI_D"
printf 'this is not an IGES file\n' > "$RWI_D/bad.igs"
sed -e "s|@TMP@|$RWI_D|g" "$HERE/rwi_iges_script.txt" > "$TMPW/rwiigs_script.txt"
rwi_app "$TMPW/rwiigs_script.txt"
rwi_has "Exported $RWI_D/all.igs" "RWIIGS: the notification names all.igs"
rwi_has "Exported $RWI_D/all.iges" "RWIIGS: the notification names all.iges"
rwi_oracle "RWIIGS: all.igs parsed independently: T counts, unit flag 2 (mm), 12 trimmed surfaces, bbox (0,0,0)-(30,10,10)" iges "$RWI_D/all.igs" --faces 12 --unit-flag 2 --min 0,0,0 --max 30,10,10
rwi_oracle "RWIIGS: all.iges (the long extension) is the same file content" iges "$RWI_D/all.iges" --same-as "$RWI_D/all.igs" --faces 12 --unit-flag 2
rwi_oracle "RWIIGS: one.igs holds only the selected box: 6 trimmed surfaces, bbox (20,0,0)-(30,10,10)" iges "$RWI_D/one.igs" --faces 6 --unit-flag 2 --min 20,0,0 --max 30,10,10
rwi_has "Imported $RWI_D/all.igs" "RWIIGS: Import read all.igs back"
rwi_oracle "RWIIGS: rt.igs (import then export) equals all.igs" iges "$RWI_D/rt.igs" --same-as "$RWI_D/all.igs" --unit-flag 2
fi
