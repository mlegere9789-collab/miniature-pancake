# ---------------------------------------------------------------------------
# RWISUT: StepUnitsAndTolerance <file.stp>, which reads the length unit and the model tolerance a STEP file declares (nothing is imported). Row: RHINO-FILE-0026
# (check units and tolerance in a STEP file before import). The first two files are written by this script from the ISO 10303-21 entity definitions, the third is
# written by the application's own STEP export, whose values the script reads back from the file text itself. The document is in millimetres (0.001 m), tolerance 0.001.
# Expected values by hand:
#   inch.stp   length unit INCH = 25.4 x MILLI METRE = 0.0254 m; tolerance 0.001 file units = 0.001 x 0.0254 = 2.54e-05 m; geometry scale 0.0254 / 0.001 = x25.4,
#              tolerance 2.54e-05 / 0.001 = 0.0254 document units
#   metre.stp  SI metre = 1 m; tolerance 1e-05 file units = 1e-05 m; geometry scale x1000; tolerance 1e-05 / 0.001 = 0.01 document units
#   app.stp    exported by the application from the millimetre document: MILLI METRE = 0.001 m, the UNCERTAINTY_MEASURE_WITH_UNIT in the file text, scale x1
#   none.stp   no units and no uncertainty declared: said so, scale x1
#   notstep    a text file that is not ISO-10303-21: refused with a message
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWISUT_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwisut"; mkdir -p "$RWI_D"
step_file() {  # step_file OUTFILE  (the DATA lines come on stdin)
  { printf "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('units test'),'2;1');\nFILE_NAME('t.stp','2026-01-01T00:00:00',(''),(''),'','','');\nFILE_SCHEMA(('AUTOMOTIVE_DESIGN'));\nENDSEC;\nDATA;\n"; cat; printf "ENDSEC;\nEND-ISO-10303-21;\n"; } > "$1"
}
step_file "$RWI_D/inch.stp" <<'EOS'
#1=(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.));
#2=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#1);
#3=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);
#4=(CONVERSION_BASED_UNIT('INCH',#2)LENGTH_UNIT()NAMED_UNIT(#3));
#5=(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.));
#6=(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT());
#7=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.001),#4,'distance_accuracy_value','confusion accuracy');
#8=(GEOMETRIC_REPRESENTATION_CONTEXT(3)GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#7))GLOBAL_UNIT_ASSIGNED_CONTEXT((#4,#5,#6))REPRESENTATION_CONTEXT('c','3D'));
EOS
step_file "$RWI_D/metre.stp" <<'EOS'
#1=( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.) );
#2=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-05),#1,'distance_accuracy_value','confusion accuracy');
#3=( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#2)) GLOBAL_UNIT_ASSIGNED_CONTEXT((#1)) REPRESENTATION_CONTEXT('c','3D') );
EOS
step_file "$RWI_D/none.stp" <<'EOS'
#1=CARTESIAN_POINT('',(0.,0.,0.));
EOS
printf 'this is not a STEP file\n' > "$RWI_D/notstep.stp"
cat > "$TMPW/rwisut_script.txt" <<EOS
Box 0,0,0 10,10,0 10
SelAll
Export $RWI_D/app.stp
StepUnitsAndTolerance $RWI_D/inch.stp
StepUnitsAndTolerance $RWI_D/metre.stp
StepUnitsAndTolerance $RWI_D/app.stp
StepUnitsAndTolerance $RWI_D/none.stp
StepUnitsAndTolerance $RWI_D/notstep.stp
StepUnitsAndTolerance $RWI_D/missing.stp
@expect_objects 1
EOS
rwi_app "$TMPW/rwisut_script.txt"
rwi_has "StepUnitsAndTolerance $RWI_D/inch.stp: length unit 0.0254 m (INCH); model tolerance 0.001 file units = 2.54e-05 m; document unit Millimeters (0.001 m): geometry scale x25.4, tolerance 0.0254 document units" "RWISUT: inch.stp declares INCH = 0.0254 m, tolerance 0.001 inch = 2.54e-05 m: scale x25.4, 0.0254 document units"
rwi_has "StepUnitsAndTolerance $RWI_D/metre.stp: length unit 1 m (METRE); model tolerance 1e-05 file units = 1e-05 m; document unit Millimeters (0.001 m): geometry scale x1000, tolerance 0.01 document units" "RWISUT: metre.stp declares 1 m, tolerance 1e-05 m: scale x1000, 0.01 document units"
# the unit and tolerance the application's own export wrote, from the file text
if python3 -I - "$RWI_D/app.stp" <<'PYEOF'
import re, sys
t = open(sys.argv[1]).read()
sys.exit(0 if re.search(r"SI_UNIT\(\.MILLI\.,\.METRE\.\)", t) and re.search(r"UNCERTAINTY_MEASURE_WITH_UNIT\(LENGTH_MEASURE\(0\.001\),#1,", t) else 1)
PYEOF
then
  rwi_ok "RWISUT: the exported app.stp declares MILLI METRE and an uncertainty of 0.001 (read from the file text)"
  rwi_has "StepUnitsAndTolerance $RWI_D/app.stp: length unit 0.001 m (MILLI METRE); model tolerance 0.001 file units = 1e-06 m; document unit Millimeters (0.001 m): geometry scale x1, tolerance 0.001 document units" "RWISUT: the command reports what app.stp declares: 0.001 m, tolerance 1e-06 m, scale x1"
else
  rwi_bad "RWISUT: app.stp does not declare MILLI METRE with an uncertainty of 0.001"
fi
rwi_has "StepUnitsAndTolerance $RWI_D/none.stp: no length unit declared (import takes the geometry as document units); no model tolerance declared; document unit Millimeters (0.001 m): geometry scale x1" "RWISUT: a file with no unit and no uncertainty says so"
rwi_has "Not an ISO-10303-21 file: $RWI_D/notstep.stp" "RWISUT: a file that is not STEP is refused with a message"
rwi_has "Could not open $RWI_D/missing.stp" "RWISUT: a missing file is reported"
fi
