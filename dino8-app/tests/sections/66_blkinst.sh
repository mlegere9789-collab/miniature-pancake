# ---------------------------------------------------------------------------
# BLKI_: Save3dm writes a static block as a real ON_InstanceDefinition and each untouched placement as one ON_InstanceRef
# (File3dm.cpp BlockDefinitionIsStatic / PristineBlockInstance). Before this a Dino 8 block was saved as its definition's objects
# written as visible loose geometry plus every instance as baked copies and private user-text tags, so a plain OpenNURBS reader
# (Rhino) saw no block at all, only duplicated loose geometry. A placement that is edited afterwards (here: one member moved) no
# longer equals the definition translated and keeps the baked export; Open reads both back.
#
# Same split as DIMWR_ / DIMORD_: one app process exports, the independent checker blockinst3dm_write_check parses the saved file
# with OpenNURBS alone, a second fresh process reopens it.
#
# Oracles (closed-form, none read from the app). Block BLK = Line 0,0,0 10,0,0 + Line 0,0,0 0,5,0, base (0,0,0):
#  * The Block command replaces the two lines with an instance at the base, then Insert BLK places two more at (100,0,0) and
#    (0,50,0), and a fourth at (200,0,0) whose second line is then moved by (0,7,0), so it is no longer pristine.
#  * File: exactly 1 ON_InstanceDefinition "BLK" with 2 members (two curves, mode idef_object, boxes (0,0,0)-(10,0,0) and
#    (0,0,0)-(0,5,0)), exactly 3 ON_InstanceRef to it with translations (0,0,0), (100,0,0), (0,50,0) and no rotation, so the world
#    boxes of the 3rd reference's members are (0,50,0)-(10,50,0) and (0,50,0)-(0,55,0) (the definition boxes moved by the
#    translation, computed by the checker).
#  * Negative control: the only loose geometry is the 2 baked objects of the edited fourth placement (the pristine placements and
#    the definition members leave no loose copies): 2 loose, 3 references, 2 definition members = 7 geometry objects.
#  * Reopen in a fresh process: 8 objects in instances (3 x 2 rebuilt from the references + 2 baked), the placements' BlockInsert
#    tags are exactly 0,0,0 / 100,0,0 / 0,50,0, and the block definition is the same one (2 objects, base 0,0,0).
blki_ok() { echo "ok   $1"; }
blki_bad() { echo "FAIL $1"; fail=1; }
blki_exec() {  # blki_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 60 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$1" 2>&1
  fi
}
blki_has() {  # blki_has <what> <text> <fixed string>
  if grep -qF -- "$3" <<<"$2"; then blki_ok "$1"; else blki_bad "$1 (missing '$3')"; fi
}

cat > "$TMPW/blki_export.txt" <<EOS
SetActiveViewport Top
New
Line 0,0,0 10,0,0
Line 0,0,0 0,5,0
SelAll
Block
0,0,0
BLK
Insert
BLK
100,0,0
0,50,0
200,0,0
Enter
SelLast
Move
0,0,0
0,7,0
SaveAs $TMPW/blki.3dm
EOS
cat > "$TMPW/blki_reopen.txt" <<EOS
SetActiveViewport Top
Open $TMPW/blki.3dm
BlockManager
SelAll
GetUserText
EOS
BLKI_E="$(blki_exec "$TMPW/blki_export.txt" || true)"
blki_has "BLKI: the export process saved the file" "$BLKI_E" "Saved $TMPW/blki.3dm"

BLKI_CHECKER="$(dirname "$BIN")/blockinst3dm_write_check"
if [ -x "$BLKI_CHECKER" ]; then
  BLKI_C="$("$BLKI_CHECKER" "$TMPW/blki.3dm" 2>&1 || true)"
  blki_has "BLKI: an independent OpenNURBS reader finds 1 real ON_InstanceDefinition, 3 real ON_InstanceRef and exactly the 2 baked objects of the edited placement as loose geometry" "$BLKI_C" "1 real ON_InstanceDefinition, 3 real ON_InstanceRef, 2 loose geometry object(s)"
  blki_has "BLKI: the definition is named BLK and has 2 members" "$BLKI_C" "idef 1: name=BLK members=2"
  blki_has "BLKI: member 1 is the (0,0,0)-(10,0,0) curve, an idef_object" "$BLKI_C" "type=Curve mode=idef_object bbox=(0,0,0)-(10,0,0)"
  blki_has "BLKI: member 2 is the (0,0,0)-(0,5,0) curve, an idef_object" "$BLKI_C" "type=Curve mode=idef_object bbox=(0,0,0)-(0,5,0)"
  blki_has "BLKI: reference 1 places BLK at (0,0,0) without rotation" "$BLKI_C" "iref 1: def=BLK translate=(0,0,0) rotation=none"
  blki_has "BLKI: reference 2 places BLK at (100,0,0) without rotation" "$BLKI_C" "iref 2: def=BLK translate=(100,0,0) rotation=none"
  blki_has "BLKI: reference 3 places BLK at (0,50,0) without rotation" "$BLKI_C" "iref 3: def=BLK translate=(0,50,0) rotation=none"
  blki_has "BLKI: the world box of reference 3's first member is the definition box moved by (0,50,0)" "$BLKI_C" "world 3.0: bbox=(0,50,0)-(10,50,0)"
  blki_has "BLKI: ...and of its second member" "$BLKI_C" "world 3.1: bbox=(0,50,0)-(0,55,0)"
  if [ "$(grep -c '^idefmember .*mode=idef_object' <<<"$BLKI_C")" = 2 ]; then blki_ok "BLKI: both definition members are in idef_object mode (they are not loose geometry of the model)"; else blki_bad "BLKI: definition members are not both idef_object ($(grep '^idefmember' <<<"$BLKI_C" | tr '\n' '|'))"; fi
else
  echo "SKIP BLKI: blockinst3dm_write_check was not built next to $BIN (DINO8_BUILD_TESTS off) - the independent .3dm checks did not run"
fi

BLKI_R="$(blki_exec "$TMPW/blki_reopen.txt" || true)"
blki_has "BLKI reopen: the block definition BLK is back with its 2 objects and base 0,0,0" "$BLKI_R" "Block 'BLK': 2 object(s), base 0,0,0, 8 object(s) in instances"
blki_has "BLKI reopen: the origin placement is rebuilt from its reference" "$BLKI_R" "BlockInsert = 0,0,0"
blki_has "BLKI reopen: the placement at (100,0,0) is rebuilt from its reference" "$BLKI_R" "BlockInsert = 100,0,0"
blki_has "BLKI reopen: the placement at (0,50,0) is rebuilt from its reference" "$BLKI_R" "BlockInsert = 0,50,0"
