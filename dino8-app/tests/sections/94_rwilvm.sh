# ---------------------------------------------------------------------------
# RWILVM: SetIgesLayerLevelMap, the layer-to-level table of IGES export, checked in the IGES files by the stand-alone rwi_oracle.py (80-column records, the level field
# of every directory entry, the level definitions of entity 406 form 3). Row: RHINO-FILE-0024 (map Rhino layers to IGES levels). Two 10 mm boxes, one on layer Walls and
# one on layer Doors (each box is 6 trimmed surfaces, entity 144), both made by NewLayer in a document whose only other layer is Default (index 0), so Walls is layer
# index 1 and Doors index 2. Expected values by hand:
#   base.igs   no table: a layer keeps its index as its level: 6 surfaces at level 1 named Walls, 6 at level 2 named Doors; level definitions {1: Walls, 2: Doors}
#   map.igs    SetIgesLayerLevelMap Walls=10 Doors=20: 6 surfaces at level 10 (Walls), 6 at level 20 (Doors); level definitions {10: Walls, 20: Doors}
#   bad.igs    after three refused commands (unknown layer, level 0, a level that is not a number) the table is as it was: the same as map.igs
#   reset.igs  after Reset: the same as base.igs
#   the listing without arguments names every layer and its level, mapped or by index
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWILVM_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwilvm"; mkdir -p "$RWI_D"
cat > "$TMPW/rwilvm_script.txt" <<EOS
NewLayer Walls
Box 0,0,0 10,10,0 10
NewLayer Doors
Box 20,0,0 30,10,0 10
SelAll
Export $RWI_D/base.igs
SetIgesLayerLevelMap
SetIgesLayerLevelMap Walls=10 Doors=20
SetIgesLayerLevelMap
SelAll
Export $RWI_D/map.igs
SetIgesLayerLevelMap Nope=5
SetIgesLayerLevelMap Walls=0
SetIgesLayerLevelMap Walls=abc
SelAll
Export $RWI_D/bad.igs
SetIgesLayerLevelMap Reset
SetIgesLayerLevelMap
SelAll
Export $RWI_D/reset.igs
EOS
rwi_app "$TMPW/rwilvm_script.txt"
rwi_oracle "RWILVM: base.igs (no table): 6 surfaces at level 1 (Walls) and 6 at level 2 (Doors)" iges "$RWI_D/base.igs" --faces 12 --level-counts 1:6,2:6 --level-names 1=Walls,2=Doors --named-counts Walls:6,Doors:6
rwi_oracle "RWILVM: map.igs (Walls=10 Doors=20): 6 surfaces at level 10 (Walls) and 6 at level 20 (Doors), level definitions 10 and 20" iges "$RWI_D/map.igs" --faces 12 --level-counts 10:6,20:6 --level-names 10=Walls,20=Doors --named-counts Walls:6,Doors:6
rwi_oracle "RWILVM: bad.igs (after three refused commands) is the same as map.igs" iges "$RWI_D/bad.igs" --faces 12 --level-counts 10:6,20:6 --level-names 10=Walls,20=Doors --named-counts Walls:6,Doors:6
rwi_oracle "RWILVM: reset.igs (after Reset) is the same as base.igs" iges "$RWI_D/reset.igs" --faces 12 --level-counts 1:6,2:6 --level-names 1=Walls,2=Doors --named-counts Walls:6,Doors:6
rwi_line "  Walls -> level 1 (layer index)" "RWILVM: the listing before the table names Walls at its index, level 1"
rwi_line "  Walls -> level 10 (mapped)" "RWILVM: the listing names Walls at the mapped level 10"
rwi_line "  Doors -> level 20 (mapped)" "RWILVM: the listing names Doors at the mapped level 20"
rwi_has "SetIgesLayerLevelMap: no layer named 'Nope'" "RWILVM: an unknown layer is refused with a message"
rwi_has "SetIgesLayerLevelMap: level 0 is out of range (1 to 9999999)" "RWILVM: level 0 is refused with a message"
rwi_has "SetIgesLayerLevelMap: 'Walls=abc' is not <layer>=<level>" "RWILVM: a level that is not a number is refused with a message"
rwi_has "SetIgesLayerLevelMap: every layer is back to its index as IGES level" "RWILVM: Reset reports that the table is cleared"
fi
