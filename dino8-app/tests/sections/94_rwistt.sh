# ---------------------------------------------------------------------------
# RWISTT: STEPTree <file.stp>, which lists the product structure of a STEP file without importing it. Row: RHINO-FILE-0025 (browse the structure of a STEP file).
# The assembly file is written by this script from the AP214 entities: PRODUCT / PRODUCT_DEFINITION_FORMATION / PRODUCT_DEFINITION per part and one
# NEXT_ASSEMBLY_USAGE_OCCURRENCE (parent definition, child definition) per use. Structure by hand: Assembly holds Plate and Bolt, Bolt holds Nut (4 product
# definitions, 3 usages), so the listing is, one line each: "- Assembly", "  - Plate", "  - Bolt", "    - Nut". The application's own export of one box is one
# product, "- Dino 8 export". Nothing is imported, so the document keeps its single box.
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWISTT_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwistt"; mkdir -p "$RWI_D"
{ printf "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('tree test'),'2;1');\nFILE_NAME('t.stp','2026-01-01T00:00:00',(''),(''),'','','');\nFILE_SCHEMA(('AUTOMOTIVE_DESIGN'));\nENDSEC;\nDATA;\n"
cat <<'EOS'
#91=APPLICATION_CONTEXT('x');
#90=PRODUCT_DEFINITION_CONTEXT('part definition',#91,'design');
#1=PRODUCT('A-100','Assembly','',());
#2=PRODUCT_DEFINITION_FORMATION('1','',#1);
#3=PRODUCT_DEFINITION('design','',#2,#90);
#4=PRODUCT('P-1','Plate','',());
#5=PRODUCT_DEFINITION_FORMATION('1','',#4);
#6=PRODUCT_DEFINITION('design','',#5,#90);
#7=PRODUCT('B-1','Bolt','',());
#8=PRODUCT_DEFINITION_FORMATION('1','',#7);
#9=PRODUCT_DEFINITION('design','',#8,#90);
#10=PRODUCT('N-1','Nut','',());
#11=PRODUCT_DEFINITION_FORMATION('1','',#10);
#12=PRODUCT_DEFINITION('design','',#11,#90);
#20=NEXT_ASSEMBLY_USAGE_OCCURRENCE('u1','','',#3,#6,$);
#21=NEXT_ASSEMBLY_USAGE_OCCURRENCE('u2','','',#3,#9,$);
#22=NEXT_ASSEMBLY_USAGE_OCCURRENCE('u3','','',#9,#12,$);
EOS
printf "ENDSEC;\nEND-ISO-10303-21;\n"; } > "$RWI_D/asm.stp"
printf 'this is not a STEP file\n' > "$RWI_D/notstep.stp"
cat > "$TMPW/rwistt_script.txt" <<EOS
Box 0,0,0 10,10,0 10
SelAll
Export $RWI_D/one.stp
STEPTree $RWI_D/asm.stp
STEPTree $RWI_D/one.stp
STEPTree $RWI_D/notstep.stp
@expect_objects 1
EOS
rwi_app "$TMPW/rwistt_script.txt"
rwi_line "STEP product structure of $RWI_D/asm.stp: 4 product definitions, 3 assembly usages" "RWISTT: the listing names 4 product definitions and 3 assembly usages"
rwi_line "- Assembly" "RWISTT: the root is Assembly"
rwi_line "  - Plate" "RWISTT: Plate is a child of Assembly"
rwi_line "  - Bolt" "RWISTT: Bolt is a child of Assembly"
rwi_line "    - Nut" "RWISTT: Nut is a child of Bolt"
rwi_line "STEP product structure of $RWI_D/one.stp: 1 product definitions, 0 assembly usages" "RWISTT: the application's own export is one product"
rwi_line "- Dino 8 export" "RWISTT: the exported product is named Dino 8 export"
rwi_has "Not an ISO-10303-21 file: $RWI_D/notstep.stp" "RWISTT: a file that is not STEP is refused with a message"
fi
