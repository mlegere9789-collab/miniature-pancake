# ---------------------------------------------------------------------------
# RWISAT: ACIS SAT text export of selected solids (rwi_sat_script.txt), parsed by the stand-alone rwi_oracle.py (Python standard library only; it
# resolves every record pointer itself). Row: AUTOCAD-DWGFID-0045 (export selected 3D solids to an ACIS SAT text file, ignoring other object types
# in the selection). The oracle does not use the application's own summary line.
# Expected values are closed forms of the construction (two 10 mm cubes, millimetre document, plus a line that has no solid):
#   one.sat    1 body, 6 faces, Euler number V - E + F = 2, every edge used by exactly two coedges, bounding box (20,0,0)-(30,10,10), volume 1000
#              (from the face loops, each loop counter-clockwise about its face normal, agreeing with the plane-surface record)
#   mixed.sat  two boxes and a line selected: 2 bodies, 12 faces, Euler 2 each, bounding box (0,0,0)-(30,10,10) (the line adds nothing), volume 2000
#   mixed.sat  after a second export with only the line selected: unchanged
#   rt.sat     the SAT read back (2 bodies) and written again: the same bodies, faces, bounding box and volume
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWISAT_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwisat"; mkdir -p "$RWI_D"
printf 'this is not a SAT file\n' > "$RWI_D/bad.sat"
sed -e "s|@TMP@|$RWI_D|g" "$HERE/rwi_sat_script.txt" > "$TMPW/rwisat_script.txt"
rwi_app "$TMPW/rwisat_script.txt"
rwi_has "Exported $RWI_D/one.sat" "RWISAT: Export of one box wrote one.sat"
rwi_has "Exported $RWI_D/mixed.sat" "RWISAT: Export of two boxes and a line wrote mixed.sat"
rwi_oracle "RWISAT: one.sat parsed independently: 1 body, 6 faces, Euler 2, bbox (20,0,0)-(30,10,10), volume 1000" sat "$RWI_D/one.sat" --parts 1 --faces 6 --euler 2 --min 20,0,0 --max 30,10,10 --vol 1000
rwi_oracle "RWISAT: mixed.sat: the line is ignored, 2 bodies, 12 faces, Euler 2, bbox (0,0,0)-(30,10,10), volume 2000" sat "$RWI_D/mixed.sat" --parts 2 --faces 12 --euler 2 --min 0,0,0 --max 30,10,10 --vol 2000
rwi_has "Imported $RWI_D/mixed.sat" "RWISAT: Import read mixed.sat back"
rwi_oracle "RWISAT: rt.sat (import then export) equals mixed.sat" sat "$RWI_D/rt.sat" --same-as "$RWI_D/mixed.sat" --euler 2
fi
