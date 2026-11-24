# ---------------------------------------------------------------------------
# UXDOC_: document-level settings read and written through the command layer and kept in the 3dm file: units and tolerances, notes,
# document user text and the model grid.
# Rows D8-RHINO-SETTING-0058 Units and tolerances, 0057 Document notes, 0059 Document user text, 0052 Document grid and
# D8-RHINO-UI-0013 DocumentText.
# Oracles: the Rhino 8 help text kept in data/commands.json for Notes ("Notes are saved with Rhino 3DM and IGES files"; the Notes panel
# holds free text for the model) and for user text (key/value, a value of two double quotes removes a key; GetUserText lists them);
# units names and the default Millimeters / 0.001 / 1 degree from Rhino's "Small Objects - Millimeters" template (from recollection, needs
# owner or independent check); everything else is a value typed in this script and read back after a Save, New, Open round trip, so the
# expected text is the typed text. A deliberate control: after New (before Open) every value is back at its default, so the Open really
# restored it from the file.
# Not tested (needs a real window): the Document Properties dialog pages and the Notes panel layout.
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXDOC (not run on Windows: needs python3 and the Linux headless launch)"
else
UXDOC_S="$TMPW/uxdoc_script.txt"
UXDOC_F="$TMPW/uxdoc_a.3dm"
rm -f "$UXDOC_F"
cat > "$UXDOC_S" <<EOS
@wait 2
UXBEGIN_units
Units
Units Inches
Units foot
Units Furlongs
Units cm
Units Inches
SetTolerance 0.01 0.5
SetTolerance abc
SetTolerance
UXBEGIN_notes
Notes "Level 3 review notes"
Notes
UXBEGIN_text
DocumentText Project NorthTower
DocumentText Phase "Design Development"
GetDocumentUserText
DocumentText Phase
GetDocumentUserText
DocumentText Phase "Design Development"
UXBEGIN_grid
GridOptions 2.5 10 80
GridOptions 0
GridOptions abc
GridOptions
UXBEGIN_save
Save $UXDOC_F
New
UXBEGIN_afternew
Units
Notes
GetDocumentUserText
GridOptions
UXBEGIN_open
Open $UXDOC_F
UXBEGIN_afteropen
Units
Notes
GetDocumentUserText
GridOptions
UXBEGIN_end
EOS
if ux_launch "$UXDOC_S"; then
  P=UXDOC_Units
  ux_eq $P units "Units reports the defaults, names and abbreviations set the unit system, an unknown name is refused, SetTolerance sets both tolerances" \
    "Units: Millimeters, absolute tolerance 0.001, angle tolerance 1 degrees" \
    "Units: Inches, absolute tolerance 0.001, angle tolerance 1 degrees" \
    "Units: Feet, absolute tolerance 0.001, angle tolerance 1 degrees" \
    "! Units: unknown unit system 'Furlongs' (use Millimeters, Centimeters, Meters, Inches or Feet)" \
    "Units: Centimeters, absolute tolerance 0.001, angle tolerance 1 degrees" \
    "Units: Inches, absolute tolerance 0.001, angle tolerance 1 degrees" \
    "Units: Inches, absolute tolerance 0.01, angle tolerance 0.5 degrees" \
    "! SetTolerance: expected a number for the absolute tolerance, got 'abc'" \
    "Units: Inches, absolute tolerance 0.01, angle tolerance 0.5 degrees"
  ux_eq UXDOC_Notes notes "Notes with text stores it, a bare Notes reports what is stored" "Notes: Level 3 review notes" "Notes: Level 3 review notes"
  ux_eq UXDOC_DocumentText text "DocumentText key value sets, GetDocumentUserText lists the keys sorted, a key alone removes it" \
    "Document user text Project = NorthTower" "Document user text Phase = Design Development" \
    "Phase = Design Development" "Project = NorthTower" \
    "Document user text 'Phase' removed" "Project = NorthTower" \
    "Document user text Phase = Design Development"
  ux_eq UXDOC_Grid grid "GridOptions spacing major extents sets the grid; zero or a non-number is refused and the grid is kept" \
    "Grid: spacing 2.5, major line every 10, extents 80" \
    "! GridOptions: spacing must be greater than zero, major-every and extents at least 1" \
    "! GridOptions: expected numbers for spacing, major-every and extents" \
    "Grid: spacing 2.5, major line every 10, extents 80"
  ux_eq UXDOC_roundtrip afternew "after New (before Open) units, notes, user text and grid are back at their defaults" \
    "Units: Millimeters, absolute tolerance 0.001, angle tolerance 1 degrees" "Notes: (empty)" "Grid: spacing 1, major line every 5, extents 50"
  ux_eq UXDOC_roundtrip afteropen "Open restores units, both tolerances, notes, user text and the grid from the 3dm file" \
    "Units: Inches, absolute tolerance 0.01, angle tolerance 0.5 degrees" "Notes: Level 3 review notes" "Phase = Design Development" "Project = NorthTower" \
    "Grid: spacing 2.5, major line every 10, extents 80"
fi
fi
