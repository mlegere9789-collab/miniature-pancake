# ---------------------------------------------------------------------------
# RWIIGX: the IGESEXPORT command (AutoCAD name; Dino 8 IgesExport), the written files parsed by the stand-alone rwi_oracle.py (80-column records, the S G D P T
# sections, the T counts, the global unit flag, entity 144 count, bounding box). Row: AUTOCAD-DWGFID-0069 (write chosen objects to a new .igs or .iges file and
# show a notification when processing completes; the typed path stands for the file dialog's choice). Two 10 mm boxes (0..10 and 20..30 in x), millimetre document.
# Expected values are closed forms of the construction:
#   igx.igs / igx.iges  every record 80 columns, T counts equal the sections', unit flag 2 (millimetres), 12 trimmed surfaces (6 faces per box), bbox (0,0,0)-(30,10,10)
#   one.igs             only the selected box: 6 trimmed surfaces, bbox (20,0,0)-(30,10,10)
#   wrong.stl           refused (not an IGES name): nothing written, a message says so
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIIGX_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiigx"; mkdir -p "$RWI_D"
cat > "$TMPW/rwiigx_script.txt" <<EOS
Box 0,0,0 10,10,0 10
Box 20,0,0 30,10,0 10
SelAll
IGESEXPORT $RWI_D/igx.igs
SelAll
IgesExport $RWI_D/igx.iges
SelAll
IgesExport $RWI_D/wrong.stl
SelLast
IgesExport $RWI_D/one.igs
EOS
rwi_app "$TMPW/rwiigx_script.txt"
rwi_has "Exported $RWI_D/igx.igs" "RWIIGX: IGESEXPORT announces igx.igs when done"
rwi_has "Exported $RWI_D/igx.iges" "RWIIGX: IgesExport announces igx.iges when done"
rwi_oracle "RWIIGX: igx.igs parsed independently: T counts, unit flag 2 (mm), 12 trimmed surfaces, bbox (0,0,0)-(30,10,10)" iges "$RWI_D/igx.igs" --faces 12 --unit-flag 2 --min 0,0,0 --max 30,10,10
rwi_oracle "RWIIGX: igx.iges (the long extension) has the same content" iges "$RWI_D/igx.iges" --same-as "$RWI_D/igx.igs" --faces 12 --unit-flag 2
rwi_oracle "RWIIGX: one.igs holds only the selected box: 6 trimmed surfaces, bbox (20,0,0)-(30,10,10)" iges "$RWI_D/one.igs" --faces 6 --unit-flag 2 --min 20,0,0 --max 30,10,10
rwi_has "IgesExport: $RWI_D/wrong.stl is not a .igs or .iges file name; nothing written" "RWIIGX: a name that is not .igs or .iges is refused with a message"
rwi_absent "$RWI_D/wrong.stl" "RWIIGX: nothing was written for the refused name"
fi
