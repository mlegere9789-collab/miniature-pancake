# QRM_: QuadrangulateMesh wired to the kernel quad-remesh pipeline (TrisToQuads / TrisToQuadsAligned +
# RelaxQuadVertices), with the measured quad quality printed (qrm_script.txt).
# The command used to be a silent placeholder (ON_Mesh::ConvertTrianglesToQuads at a fixed 2 degrees). Expectations are
# independent: the fixtures are OBJ files written here by awk, each case exports its result to an OBJ, and awk re-measures
# that file (quads, triangles, area, smallest corner angle, worst aspect, aligned-quad fraction); nothing below compares
# the command's printout with itself except the one explicit cross-check of its "smallest angle" against the awk value.
#   noisy 8 x 8 grid (interior vertices jittered 0.18):  64 quads whatever the jitter (one per cell), area exactly 64
#       pairing only      -> smallest corner angle well below 90 (the jitter skews the cells)
#       + 400 relaxations -> the regular grid: smallest angle 90, aspect 1
#   8 x 8 equilateral lattice (area 64 * sqrt(3)/2 = 55.4256), direction 120 deg
#       plain pairing     -> few quads with an edge along 120 deg (the cell-diagonal pairing runs at 0 / 60 deg)
#       aligned pairing   -> at least 95% of the quads have an edge within 0.5 deg of 120 deg, at least 50 of 64 cells merge
if ! command -v awk >/dev/null 2>&1; then echo "FAIL QRM_: awk not available"; fail=1; else
qrm_ok() { echo "ok   $1"; }
qrm_bad() { echo "FAIL $1"; fail=1; }
# Fixtures (deterministic awk srand): noisy grid and equilateral lattice
awk 'BEGIN {
  n = 8; amp = 0.18; srand(11)
  for (j = 0; j <= n; j++) for (i = 0; i <= n; i++) {
    edge = (i == 0 || j == 0 || i == n || j == n)
    dx = edge ? 0 : amp * (2 * rand() - 1); dy = edge ? 0 : amp * (2 * rand() - 1)
    printf "v %.6f %.6f 0\n", i + dx, j + dy
  }
  for (j = 0; j < n; j++) for (i = 0; i < n; i++) {
    a = j * (n + 1) + i + 1; b = a + 1; c = a + n + 2; d = a + n + 1
    printf "f %d %d %d\nf %d %d %d\n", a, b, c, a, c, d
  }
}' > "$TMPW/qrm_noisy.obj"
awk 'BEGIN {
  n = 8; h = sqrt(3) / 2
  for (j = 0; j <= n; j++) for (i = 0; i <= n; i++) printf "v %.7f %.7f 0\n", i + 0.5 * j, j * h
  for (j = 0; j < n; j++) for (i = 0; i < n; i++) {
    a = j * (n + 1) + i + 1; b = a + 1; d = a + n + 1; c = d + 1
    printf "f %d %d %d\nf %d %d %d\n", a, b, d, b, c, d
  }
}' > "$TMPW/qrm_hex.obj"
sed "s|@TMP@|$TMPW|g" "$HERE/qrm_script.txt" > "$TMPW/qrm_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  QRMO="$(timeout 900 "$BIN" --smoke 60 --script "$TMPW/qrm_script.txt" 2>&1)" || { echo "$QRMO"; echo "FAIL: qrm script exited non-zero"; exit 1; }
else
  QRMO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMPW/qrm_script.txt" 2>&1)" || { echo "$QRMO"; echo "FAIL: qrm script exited non-zero"; exit 1; }
fi
qrmblock() { awk -v name="$1" '$0 ~ ("Unknown command: QRMBEGIN_" name "$") { on = 1; next } on && /Unknown command: QRMBEGIN_/ { exit } on { print }' <<<"$QRMO"; }
# qrmstat OBJFILE ANGLE TOL -> "quads triangles area minangle maxaspect alignedfraction", measured from the exported file alone
qrmstat() {
  awk -v A="$2" -v TOL="$3" '
    function acosd(x) { if (x > 1) x = 1; if (x < -1) x = -1; return atan2(sqrt(1 - x * x), x) * 180 / 3.14159265358979 }
    function tri(a, b, c,   ux, uy, uz, vx, vy, vz, cx, cy, cz) {
      ux = X[b] - X[a]; uy = Y[b] - Y[a]; uz = Z[b] - Z[a]; vx = X[c] - X[a]; vy = Y[c] - Y[a]; vz = Z[c] - Z[a]
      cx = uy * vz - uz * vy; cy = uz * vx - ux * vz; cz = ux * vy - uy * vx
      return sqrt(cx * cx + cy * cy + cz * cz) / 2
    }
    function len(a, b) { return sqrt((X[a] - X[b]) ^ 2 + (Y[a] - Y[b]) ^ 2 + (Z[a] - Z[b]) ^ 2) }
    function angle(p, c, q,   ux, uy, uz, vx, vy, vz) {
      ux = X[p] - X[c]; uy = Y[p] - Y[c]; uz = Z[p] - Z[c]; vx = X[q] - X[c]; vy = Y[q] - Y[c]; vz = Z[q] - Z[c]
      return acosd((ux * vx + uy * vy + uz * vz) / (sqrt(ux * ux + uy * uy + uz * uz) * sqrt(vx * vx + vy * vy + vz * vz)))
    }
    function dev(a, b,   d) {
      d = atan2(Y[b] - Y[a], X[b] - X[a]) * 180 / 3.14159265358979 - A
      while (d < 0) d += 180
      while (d >= 180) d -= 180
      return d < 180 - d ? d : 180 - d
    }
    BEGIN { minang = 180; maxasp = 0 }
    $1 == "v" { n++; X[n] = $2 + 0; Y[n] = $3 + 0; Z[n] = $4 + 0 }
    $1 == "f" {
      k = NF - 1
      for (i = 1; i <= k; i++) { split($(i + 1), t, "/"); id[i] = t[1] + 0 }
      if (k == 3) { nt++; area += tri(id[1], id[2], id[3]) }
      else if (k == 4) {
        nq++; area += tri(id[1], id[2], id[3]) + tri(id[1], id[3], id[4])
        shortest = 1e30; longest = 0
        for (i = 1; i <= 4; i++) {
          p = id[(i + 2) % 4 + 1]; c = id[i]; q = id[i % 4 + 1]
          a = angle(p, c, q); if (a < minang) minang = a
          l = len(c, q); if (l < shortest) shortest = l; if (l > longest) longest = l
        }
        if (longest / shortest > maxasp) maxasp = longest / shortest
        d1 = dev(id[1], id[2]); d2 = dev(id[2], id[3]); best = d1 < d2 ? d1 : d2
        if (best < TOL) al++
      }
    }
    END { printf "%d %d %.6f %.4f %.4f %.4f\n", nq, nt, area, minang, maxasp, (nq > 0 ? al / nq : 0) }' "$1"
}
qrmfield() { awk -v k="$2" '{ print $k }' <<<"$1"; }
# qrmnum VALUE EXPECTED ABSTOL DESC
qrmnum() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t) }'; then qrm_ok "$4 (measured $1, expected $2 +-$3)"; else qrm_bad "$4 (measured '${1:-none}', expected $2 +-$3)"; fi; }
# qrmrange VALUE LO HI DESC
qrmrange() { if [ -n "$1" ] && awk -v v="$1" -v l="$2" -v h="$3" 'BEGIN { exit !(v >= l && v <= h) }'; then qrm_ok "$4 (measured $1, in [$2, $3])"; else qrm_bad "$4 (measured '${1:-none}', outside [$2, $3])"; fi; }
qrmhas() { if grep -qF -- "$2" <<<"$(qrmblock "$1")"; then qrm_ok "$3"; else qrm_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Quadrangulate|!|Object)" <<<"$(qrmblock "$1")" || true; } | head -4 | sed 's/^/     actual:   /'; fi; }

# ---- noisy 8 x 8 grid: pairing only
QRM_PAIR="$(qrmstat "$TMPW/qrm_noisy_pair.obj" 0 1)"
qrmnum "$(qrmfield "$QRM_PAIR" 1)" 64 0 "QRM_noisy_pair: 64 quads (one per grid cell)"
qrmnum "$(qrmfield "$QRM_PAIR" 2)" 0 0 "QRM_noisy_pair: no triangle left"
qrmnum "$(qrmfield "$QRM_PAIR" 3)" 64 0.01 "QRM_noisy_pair: area is the 8 x 8 square, 64"
qrmrange "$(qrmfield "$QRM_PAIR" 4)" 30 80 "QRM_noisy_pair (negative control): the jitter leaves a smallest corner angle well below 90"
qrmhas noisy_pair "64 quad(s), 0 triangle(s)" "QRM_noisy_pair: the command reports 64 quads, 0 triangles"
# ---- noisy 8 x 8 grid: pairing + relaxation
QRM_RELAX="$(qrmstat "$TMPW/qrm_noisy_relax.obj" 0 1)"
qrmnum "$(qrmfield "$QRM_RELAX" 1)" 64 0 "QRM_noisy_relax: 64 quads"
qrmnum "$(qrmfield "$QRM_RELAX" 2)" 0 0 "QRM_noisy_relax: no triangle left"
qrmnum "$(qrmfield "$QRM_RELAX" 3)" 64 0.01 "QRM_noisy_relax: area preserved (64)"
qrmrange "$(qrmfield "$QRM_RELAX" 4)" 89.9 90.1 "QRM_noisy_relax: smallest corner angle is 90 (regular grid)"
qrmrange "$(qrmfield "$QRM_RELAX" 5)" 1.0 1.01 "QRM_noisy_relax: worst aspect ratio is 1 (squares)"
QRM_PRINTED="$({ grep -oE "smallest angle [0-9.]+ deg" <<<"$(qrmblock noisy_relax)" || true; } | head -1 | sed -E 's/smallest angle ([0-9.]+) deg/\1/')"
qrmnum "$QRM_PRINTED" "$(qrmfield "$QRM_RELAX" 4)" 0.05 "QRM_noisy_relax: the angle the command prints matches the angle awk measures from the exported file"
qrmhas noisy_relax "relaxed 400 iteration(s)" "QRM_noisy_relax: the command reports the 400 relaxation passes"
# ---- SubD cage output
qrmhas noisy_subd "-> SubD" "QRM_noisy_subd: ConvertToSubD=Yes makes a SubD cage"
QRM_N="$(grep -cE "^history: Object [0-9]+ \(" <<<"$(qrmblock noisy_subd)" || true)"
if [ "$QRM_N" = "1" ]; then qrm_ok "QRM_noisy_subd: the input mesh was replaced (1 object)"; else qrm_bad "QRM_noisy_subd: expected 1 object, List shows '$QRM_N'"; fi
# ---- equilateral lattice, plain vs aligned
QRM_HP="$(qrmstat "$TMPW/qrm_hex_plain.obj" 120 0.5)"
QRM_HA="$(qrmstat "$TMPW/qrm_hex_aligned.obj" 120 0.5)"
qrmnum "$(qrmfield "$QRM_HP" 3)" 55.425626 0.01 "QRM_hex_plain: area is 64 * sqrt(3)/2"
qrmnum "$(qrmfield "$QRM_HA" 3)" 55.425626 0.01 "QRM_hex_aligned: area preserved (64 * sqrt(3)/2)"
qrmrange "$(qrmfield "$QRM_HA" 1)" 50 64 "QRM_hex_aligned: at least 50 of the 64 cells merge"
qrmrange "$(qrmfield "$QRM_HA" 6)" 0.95 1.0 "QRM_hex_aligned: >= 95% of the quads have an edge within 0.5 deg of 120 deg"
qrmrange "$(qrmfield "$QRM_HP" 6)" 0 0.5 "QRM_hex_plain (negative control): without a direction fewer than half the quads follow 120 deg"
qrmhas hex_aligned "(aligned to the direction)" "QRM_hex_aligned: the command reports the aligned pairing"
# ---- not a mesh
qrmhas not_a_mesh "is not a mesh; skipped" "QRM_not_a_mesh: a solid is skipped with a warning"
fi
