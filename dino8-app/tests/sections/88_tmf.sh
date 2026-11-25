# ---------------------------------------------------------------------------
# TMF_: 3MF Import / Export (tmf_script.txt). ImportMeshFile / ExportMeshFile (io/File3dm.cpp) gained .3mf through the kernel's
# SaveThreeMf / LoadThreeMf (deflate-compressed ZIP package, document unit written as the 3MF unit, units converted on import).
# Oracles independent of the app and the kernel writer: Python's zipfile + xml.etree (dino8-kernel/tests/threemf_oracle.py) reads the
# exported file (CRC-checked deflate, unit, signed volume by the divergence theorem, closed and consistently oriented, bounding box)
# and writes inch.3mf (a 1 x 2 x 3 inch box, closed forms 6 in^3 = 6 x 25.4^3 mm^3, extents 25.4 x 50.8 x 76.2 mm).
#   box  : Box 0,0,0 20,10,0 5 -> volume 1000, area 700, bbox 0,0,0 - 20,10,5, exported then re-imported
#   inch : importing the inch box into a millimetre document scales by 25.4
# ---------------------------------------------------------------------------
TMF_PY="$HERE/../../dino8-kernel/tests/threemf_oracle.py"
mkdir -p "$TMPW/tmf"
if python3 "$TMF_PY" inchbox "$TMPW/tmf/inch.3mf"; then
  echo "ok   TMF_oracle: the Python oracle wrote inch.3mf"
else
  echo "FAIL TMF_oracle: could not write inch.3mf"; fail=1
fi
sed "s|@TMP@|$TMPW/tmf|g" "$HERE/tmf_script.txt" > "$TMPW/tmf_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  TMFO="$(timeout 900 "$BIN" --smoke 60 --script "$TMPW/tmf_script.txt" 2>&1)" || { echo "$TMFO" | tail -30; echo "FAIL TMF_run: script exited non-zero"; fail=1; TMFO=""; }
else
  TMFO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/tmf_script.txt" 2>&1)" || { echo "$TMFO" | tail -30; echo "FAIL TMF_run: script exited non-zero"; fail=1; TMFO=""; }
fi
tmf_ok() { echo "ok   $1"; }
tmf_bad() { echo "FAIL $1"; fail=1; }
tmf_block() { awk -v name="$1" '$0 ~ ("Unknown command: TMFBEGIN_" name "$") { on = 1; next } on && /Unknown command: TMFBEGIN_/ { exit } on { print }' <<<"$TMFO"; }
tmf_has() { if grep -qF -- "$2" <<<"$(tmf_block "$1")"; then tmf_ok "$3"; else tmf_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Volume|Area|Bounding|Object|  )" <<<"$(tmf_block "$1")" || true; } | head -4 | sed 's/^/     actual:   /'; fi; }
# the number after "<label> = " in a block
tmf_val() { { grep -oE "$2 = [0-9.eE+-]+" <<<"$(tmf_block "$1")" || true; } | sed -n "${3}p" | sed -E "s/$2 = //"; }
tmf_num() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then tmf_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else tmf_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }

# --- the exported box, as an independent reader sees it
if [ -s "$TMPW/tmf/box.3mf" ]; then
  tmf_ok "TMF_box: Export wrote box.3mf"
  TMF_SUM="$(python3 "$TMF_PY" mesh "$TMPW/tmf/box.3mf" 2>&1)" || { echo "$TMF_SUM"; TMF_SUM=""; }
  echo "     oracle: $TMF_SUM"
  tmf_field() { { grep -oE "(^| )$1=[^ ]+" <<<"$TMF_SUM" || true; } | head -1 | sed -E "s/^ ?$1=//"; }
  [ "$(tmf_field unit)" = "millimeter" ] && tmf_ok "TMF_box: the package says unit millimeter (the document unit)" || tmf_bad "TMF_box: unit is '$(tmf_field unit)'"
  [ "$(tmf_field parts)" = "1" ] && tmf_ok "TMF_box: one build item" || tmf_bad "TMF_box: parts is '$(tmf_field parts)'"
  [ "$(tmf_field method)" = "8" ] && tmf_ok "TMF_box: the model part is deflate-compressed (ZIP method 8) and zipfile CRC-checked it" || tmf_bad "TMF_box: ZIP method is '$(tmf_field method)'"
  [ "$(tmf_field closed)" = "1" ] && tmf_ok "TMF_box: every directed edge is matched by its reverse (closed, consistently oriented)" || tmf_bad "TMF_box: the exported mesh is not closed/oriented"
  tmf_num "$(tmf_field volume)" 1000 0.000001 "TMF_box: signed volume summed from the file's triangles"
  [ "$(tmf_field bbox)" = "0,0,0,20,10,5" ] && tmf_ok "TMF_box: bounding box of the file's vertices is 0,0,0 - 20,10,5" || tmf_bad "TMF_box: bbox is '$(tmf_field bbox)'"
  TMF_NV="$(tmf_field vertices)"; TMF_NT="$(tmf_field triangles)"
  [ "${TMF_NT:-0}" -ge 12 ] 2>/dev/null && tmf_ok "TMF_box: at least the 12 triangles of a box ($TMF_NT triangles, $TMF_NV vertices)" || tmf_bad "TMF_box: triangle count '$TMF_NT'"
else
  tmf_bad "TMF_box: box.3mf was not written"; TMF_NV=""; TMF_NT=""
fi
# --- re-imported into a fresh document: counts match the file, volume / area / bounding box are the box's
if [ -n "$TMF_NV" ] && grep -qE "^history:   ${TMF_NV} vertices, ${TMF_NT} faces$" <<<"$(tmf_block box)"; then
  tmf_ok "TMF_box: the re-imported mesh has the file's ${TMF_NV} vertices and ${TMF_NT} triangles"
else
  tmf_bad "TMF_box: re-imported counts differ from the file's (${TMF_NV} vertices, ${TMF_NT} triangles)"; { grep -E "vertices, " <<<"$(tmf_block box)" || true; } | head -2 | sed 's/^/     actual:   /'
fi
tmf_num "$(tmf_val box Volume 1)" 1000 0.0001 "TMF_box: re-imported volume"
tmf_num "$(tmf_val box Area 1)" 700 0.0001 "TMF_box: re-imported area (2 x (200 + 100 + 50))"
tmf_has box "Bounding box min 0,0,0 max 20,10,5" "TMF_box: re-imported bounding box 0,0,0 - 20,10,5"
# --- the inch box from the oracle, scaled 25.4 into the millimetre document
tmf_has inch "Bounding box min 0,0,0 max 25.4,50.8,76.2" "TMF_inch: bounding box 25.4 x 50.8 x 76.2 mm (1 x 2 x 3 inch x 25.4)"
tmf_num "$(tmf_val inch Volume 1)" 98322.384 0.001 "TMF_inch: volume 6 x 25.4^3 = 98322.384 mm^3"
tmf_has inch " 8 vertices, 12 faces" "TMF_inch: 8 vertices, 12 triangles"
