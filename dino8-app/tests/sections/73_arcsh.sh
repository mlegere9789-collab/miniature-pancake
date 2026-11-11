# ---------------------------------------------------------------------------
# ARCSH: exact open / variable-thickness Shell of a sphere bowl, dome or cap (arcshell_script.txt) - kernel::ShellRevolvedArcProfile.
# Expected volumes are closed forms (spherical-cap volumes pi h^2 (3 rho - h) / 3), read from the display mesh so the ranges carry
# tessellation error (measured 0.4-1% low on these rational faces):
#   hemisphere bowl R5 t1, top removed            (2/3) pi (125 - 64)                     = 127.76
#   dome (centre z=-2, cap height 3), base removed   cap(5, h3) - cap(4, h2) = 36 pi - 40/3 pi = 71.21
#   dome, spherical wall 0.5                      cap(5, h3) - cap(4.5, h2.5) = 36 pi - 22.917 pi = 41.10
#   large cap (centre z=+2, height 7), base removed  cap(5, h7) - cap(4, h6) = 130.667 pi - 72 pi    = 184.30
#   spherical face removed, base slab left        cap(5, h3) - cap(5, h2) = 36 pi - 17.333 pi        = 58.64
# and the exact whole-body OffsetSrf (Solid=No) of the dome (kernel::OffsetRevolvedArcProfile): +1 -> radius 6 cap cut at z=-1,
#   cap(6, h5) = 108.333 pi = 340.34;  -0.5 -> radius 4.5 cap cut at z=+0.5, cap(4.5, h2) = 15.333 pi = 48.17
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ARCSHOUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/arcshell_script.txt" 2>&1)" || { echo "$ARCSHOUT"; echo "FAIL: arcshell script exited non-zero"; fail=1; ARCSHOUT=""; }
else
  ARCSHOUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/arcshell_script.txt" 2>&1)" || { echo "$ARCSHOUT"; echo "FAIL: arcshell script exited non-zero"; fail=1; ARCSHOUT=""; }
fi
arcshblock() { awk -v name="$1" '$0 ~ ("Unknown command: ARCSHBEGIN_" name "$") { on = 1; next } on && /Unknown command: ARCSHBEGIN_/ { exit } on { print }' <<<"$ARCSHOUT"; }
arcshcheck() { local blk; blk="$(arcshblock "$1")"; if grep -q -- "$2" <<<"$blk"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; { grep -E "^(history: )?(Shell|Object|Volume)" <<<"$blk" || true; } | head -8 | sed 's/^/     actual:   /'; fail=1; fi; }
arcshvol() {
  local blk v; blk="$(arcshblock "$1")"
  v="$({ grep -E "^history: Volume = " <<<"$blk" || true; } | tail -1 | sed -E 's/^history: Volume = ([0-9.eE+-]+).*/\1/')"
  if [ -n "$v" ] && awk -v v="$v" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v >= lo && v <= hi) }'; then echo "ok   $4 (Volume $v in [$2, $3])"; else echo "FAIL $4 (Volume '${v:-none}' not in [$2, $3])"; fail=1; fi
}
arcshcheck bowl "Shell: thickness 1, open (1 face(s) removed), exact B-rep" "ARCSH_bowl: a hemisphere with its flat top clicked off is an exact B-rep (before: mesh construction)"
arcshvol bowl 125.5 129 "ARCSH_bowl: Volume = (2/3) pi (125 - 64) = 127.76"
arcshcheck bowl "Object [0-9]*: valid" "ARCSH_bowl: ...Check valid"
arcshcheck dome "Shell: thickness 1, open (1 face(s) removed), exact B-rep" "ARCSH_dome: a sphere cap (less than a hemisphere) with its base removed is exact"
arcshvol dome 70.2 71.8 "ARCSH_dome: Volume = 36 pi - 40/3 pi = 71.21"
arcshcheck dome "Object [0-9]*: valid" "ARCSH_dome: ...Check valid"
arcshcheck domevary "Shell: thickness 1, 1 face(s) at their own thickness, open (1 face(s) removed), exact B-rep" "ARCSH_domevary: the spherical wall at its own thickness 0.5 is exact"
arcshvol domevary 40.4 41.5 "ARCSH_domevary: Volume = 36 pi - 22.917 pi = 41.10"
arcshcheck domevary "Object [0-9]*: valid" "ARCSH_domevary: ...Check valid"
arcshcheck bigcap "Shell: thickness 1, open (1 face(s) removed), exact B-rep" "ARCSH_bigcap: a cap larger than a hemisphere, base removed, is exact"
arcshvol bigcap 182 185.5 "ARCSH_bigcap: Volume = 130.667 pi - 72 pi = 184.30"
arcshcheck bigcap "Object [0-9]*: valid" "ARCSH_bigcap: ...Check valid"
arcshcheck curvedgone "Shell: thickness 1, open (1 face(s) removed), exact B-rep" "ARCSH_curvedgone: removing the SPHERICAL face leaves the base slab, exact"
arcshvol curvedgone 57.4 59 "ARCSH_curvedgone: Volume = 36 pi - 17.333 pi = 58.64"
arcshcheck curvedgone "Object [0-9]*: valid" "ARCSH_curvedgone: ...Check valid"
arcshcheck closed "exact B-rep shell not used (.*at least one face must be removed" "ARCSH_closed: a per-face thickness with nothing removed is refused WITH the kernel's reason (negative control)"
arcshcheck offdome "OffsetSrf: distance 1, 1 object(s), 1 exact B-rep" "ARCSH_offdome: OffsetSrf of a dome grown by 1 is an exact whole-body B-rep offset (before: mesh offset)"
arcshvol offdome 338 341.2 "ARCSH_offdome: Volume = cap(6, h5) = 108.333 pi = 340.34"
arcshcheck offdome "Object [0-9]*: valid" "ARCSH_offdome: ...Check valid"
arcshcheck offdomein "OffsetSrf: distance -0.5, 1 object(s), 1 exact B-rep" "ARCSH_offdomein: the dome shrunk by 0.5 is exact"
arcshvol offdomein 47.4 48.3 "ARCSH_offdomein: Volume = cap(4.5, h2) = 15.333 pi = 48.17"
arcshcheck offdomein "Object [0-9]*: valid" "ARCSH_offdomein: ...Check valid"
arcshcheck offcollapse "exact B-rep offset not used (.*collapses it to or through its centre" "ARCSH_offcollapse: shrinking the dome by its own radius is refused WITH the kernel's reason (negative control)"
