# ---------------------------------------------------------------------------
# DIM_: associative HATCH (drafting/cmd_drafting2.cpp DoUpdateHatches, run per frame by Application.cpp's
# UpdateAssociativeAnnotations). A hatch made by the Hatch command remembers its boundary curve; when that curve is moved,
# scaled or reshaped the hatch is rebuilt to fill the CURRENT boundary (AutoCAD's associative hatch). Before this a hatch
# stayed exactly where it was built.
# One real app process runs $TMPW/dim_script.txt, written below.
#
# Oracles (all closed-form, none taken from the app):
#  * Solid hatch = trimmed planar face of the boundary, so Area and AreaCentroid are those of the rectangle:
#      10 x 6 at the origin                              area 60,  centroid (5,3)
#      scaled x2 about the origin  -> 20 x 12            area 240, centroid (10,6)
#      moved by (100,50)                                 area 240, centroid (110,56)
#  * ANSI31 pattern hatch = parallel 45 degree lines at perpendicular spacing d, so total line length ~ area / d. Scaling
#    the 20 x 10 boundary x2 quadruples the area: total length x4 (+-3% for the clipped line ends) and the line count,
#    proportional to (W + H) / d, doubles (+-5%).
#  * Negative controls. Two solid hatches, R1 (10x6 at the origin) and R2 (10x6 at x=200). Only R1 is scaled x2: total
#    area 240 + 60 = 300, centroids {(10,6), (205,3)}; if the untouched hatch had been rebuilt, moved or duplicated, the
#    area, a centroid or the hatch-object count would differ. Deleting R1 must leave its hatch alone (same area and
#    centroids, still 2 hatch objects). R2 is then moved by (0,100): its hatch follows, the orphan stays: centroids
#    {(10,6), (205,103)}. (AreaCentroid prints one centroid per selected object, in id order, and a rebuilt hatch gets a
#    new id, so the two are compared sorted.)
dim_ok() { echo "ok   $1"; }
dim_bad() { echo "FAIL $1"; fail=1; }
dim_exec() {  # dim_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 100 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$1" 2>&1
  fi
}
dim_near() {  # dim_near <what> <got> <want> <tolerance fraction of want, or absolute if want is 0>
  if awk -v g="$2" -v w="$3" -v t="$4" 'BEGIN { d = g - w; if (d < 0) d = -d; a = w < 0 ? -w : w; lim = a > 0 ? t * a : t; exit !(g != "" && d <= lim) }'; then
    dim_ok "$1: $2 (expected $3)"
  else
    dim_bad "$1: got '$2', expected $3 (tolerance $4)"
  fi
}
dim_is() {  # dim_is <what> <got> <want>
  if [ "$2" = "$3" ]; then dim_ok "$1: $2"; else dim_bad "$1: got '$2', expected '$3'"; fi
}

cat > "$TMPW/dim_script.txt" <<'EOS'
# --- A: solid hatch follows its boundary through Scale and Move
Rectangle 0,0,0 10,6,0
SelLast
SetObjectName HA_R1
Hatch Pattern=Solid
SelNone
SelHatch
Area
AreaCentroid
SelNone
SelName HA_R1
Scale 0,0,0 2
SelNone
SelHatch
Area
AreaCentroid
SelNone
SelName HA_R1
Move 0,0,0 100,50,0
SelNone
SelHatch
Area
AreaCentroid
SelNone
SelAll
Delete
# --- B: ANSI31 pattern hatch refills the scaled boundary
Rectangle 0,0,0 20,10,0
SelLast
SetObjectName HA_P1
Hatch Pattern=ANSI31 Scale=1 Rotation=45
SelNone
SelHatch
Length
SelNone
SelName HA_P1
Scale 0,0,0 2
SelNone
SelHatch
Length
SelNone
SelAll
Delete
# --- C: negative controls
Rectangle 0,0,0 10,6,0
SelLast
SetObjectName HA_R1
Hatch Pattern=Solid
SelNone
Rectangle 200,0,0 210,6,0
SelLast
SetObjectName HA_R2
Hatch Pattern=Solid
SelNone
SelName HA_R1
Scale 0,0,0 2
SelNone
SelHatch
Area
AreaCentroid
SelNone
SelName HA_R1
Delete
SelNone
SelHatch
Area
AreaCentroid
SelNone
SelName HA_R2
Move 0,0,0 0,100,0
SelNone
SelHatch
Area
AreaCentroid
SelNone
EOS
DIM_OUT="$(dim_exec "$TMPW/dim_script.txt" || true)"
mapfile -t DIM_AREA < <(grep -oE '^history: Area = [-0-9.e+]+' <<<"$DIM_OUT" | sed 's/^history: Area = //')
mapfile -t DIM_CEN < <(grep -oE '^history: Area centroid [-0-9.e+,]+' <<<"$DIM_OUT" | sed 's/^history: Area centroid //')
mapfile -t DIM_LEN < <(grep -oE '^history: Total length = [-0-9.e+]+' <<<"$DIM_OUT" | sed 's/^history: Total length = //')
mapfile -t DIM_SEL < <(awk '/^history: Command: SelHatch/ { f = 1; next } f && /object\(s\) selected$/ { print $2; f = 0 }' <<<"$DIM_OUT")

# readings: 3 (A) + 3 (C) areas; 3 (A) + 2 x 3 (C) centroids; 2 lengths (B); 3 + 2 + 3 SelHatch counts
if [ "${#DIM_AREA[@]}" = 6 ] && [ "${#DIM_CEN[@]}" = 9 ] && [ "${#DIM_LEN[@]}" = 2 ] && [ "${#DIM_SEL[@]}" = 8 ]; then
  dim_ok "DIM: the script printed 6 Area, 9 AreaCentroid, 2 Length and 8 SelHatch readings"
else
  dim_bad "DIM: expected 6/9/2/8 Area/AreaCentroid/Length/SelHatch readings, got ${#DIM_AREA[@]}/${#DIM_CEN[@]}/${#DIM_LEN[@]}/${#DIM_SEL[@]}"
  echo "$DIM_OUT" | grep -E '^history: (Area|Total|Hatch|[0-9]+ object)' | head -30 | sed 's/^/     /'
fi

# A: solid hatch, one object per reading
dim_near "DIM A built: solid hatch area of the 10x6 boundary" "${DIM_AREA[0]:-}" 60 0.001
dim_is   "DIM A built: solid hatch centroid" "${DIM_CEN[0]:-}" "5,3,0"
dim_near "DIM A after Scale x2: the hatch fills the 20x12 boundary (area 240, not the stale 60)" "${DIM_AREA[1]:-}" 240 0.001
dim_is   "DIM A after Scale x2: hatch centroid" "${DIM_CEN[1]:-}" "10,6,0"
dim_near "DIM A after Move (100,50): area unchanged" "${DIM_AREA[2]:-}" 240 0.001
dim_is   "DIM A after Move (100,50): the hatch moved with its boundary" "${DIM_CEN[2]:-}" "110,56,0"
dim_is   "DIM A: exactly one hatch object after each rebuild (no duplicate left behind)" "${DIM_SEL[0]:-}/${DIM_SEL[1]:-}/${DIM_SEL[2]:-}" "1/1/1"

# B: pattern hatch
LEN0="${DIM_LEN[0]:-0}"; LEN1="${DIM_LEN[1]:-0}"
dim_ok "DIM B: ANSI31 hatch of the 20x10 boundary: ${DIM_SEL[3]:-?} lines, total length $LEN0"
if awk -v a="$LEN0" -v b="$LEN1" 'BEGIN { exit !(a > 0 && b / a >= 3.88 && b / a <= 4.12) }'; then
  dim_ok "DIM B after Scale x2: total pattern length $LEN1 is 4x the original $LEN0 (area ratio 4, +-3%)"
else
  dim_bad "DIM B after Scale x2: total pattern length $LEN1 vs original $LEN0, expected a ratio of 4 +-3%"
fi
if awk -v a="${DIM_SEL[3]:-0}" -v b="${DIM_SEL[4]:-0}" 'BEGIN { exit !(a > 0 && b / a >= 1.9 && b / a <= 2.1) }'; then
  dim_ok "DIM B after Scale x2: ${DIM_SEL[4]} lines is 2x the original ${DIM_SEL[3]} ((W+H) ratio 2, +-5%)"
else
  dim_bad "DIM B after Scale x2: ${DIM_SEL[4]:-?} lines vs original ${DIM_SEL[3]:-?}, expected a ratio of 2 +-5%"
fi

# C: negative controls
dim_near "DIM C only R1 scaled: combined area 240 + 60" "${DIM_AREA[3]:-}" 300 0.001
dim_is   "DIM C only R1 scaled: centroids, the untouched hatch stayed at x=205" "$(printf '%s\n' "${DIM_CEN[3]:-}" "${DIM_CEN[4]:-}" | LC_ALL=C sort | paste -sd' ')" "10,6,0 205,3,0"
dim_near "DIM C boundary R1 deleted: its hatch is left alone (area 300)" "${DIM_AREA[4]:-}" 300 0.001
dim_is   "DIM C boundary R1 deleted: centroids unchanged" "$(printf '%s\n' "${DIM_CEN[5]:-}" "${DIM_CEN[6]:-}" | LC_ALL=C sort | paste -sd' ')" "10,6,0 205,3,0"
dim_near "DIM C R2 moved (0,100) with R1 gone: area 300" "${DIM_AREA[5]:-}" 300 0.001
dim_is   "DIM C R2 moved (0,100): R2's hatch followed, the orphan stayed" "$(printf '%s\n' "${DIM_CEN[7]:-}" "${DIM_CEN[8]:-}" | LC_ALL=C sort | paste -sd' ')" "10,6,0 205,103,0"
dim_is   "DIM C: always exactly 2 hatch objects (no stray, duplicated or lost hatch)" "${DIM_SEL[5]:-}/${DIM_SEL[6]:-}/${DIM_SEL[7]:-}" "2/2/2"
