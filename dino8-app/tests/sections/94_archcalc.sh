# ---------------------------------------------------------------------------
# ARCHCALC_: design-rule and quantity commands over dino8/kernel/arch_calc.h (cmd_arch.cpp): StairCheck, StairFit, RampPlan,
# RoofPitchConv, ZoneArea, WallQuantities. One real app process runs $TMPW/archcalc_script.txt, written below.
#
# Oracles (hand-derived closed forms, none read from the app; the kernel tests extra::Arch* derive each one in full):
#  * stair 0.18 / 0.28 / 1.1: 2R + T = 0.64, passes; riser 0.20 breaks the 0.19 maximum (2R + T = 0.68).
#  * rise 2.8: riser counts 15..28 give a riser in [0.10, 0.19], 14 options; the first is 15 risers of 2.8/15 = 0.1867 with
#    tread 0.63 - 2 * 0.186667 = 0.2567 and run 14 * 0.256667 = 3.5933.
#  * a 16-riser stair relinked to story 3.2: riser 0.20 invalid, nearest fix 17 risers of 0.1882 keeping the 0.28 tread; to 2.96:
#    riser 0.185, 2R + T = 0.65, valid.
#  * ramp rise 0.9 at 1:12 with 9.0 max run: run 10.8 -> 2 flights of 5.4, 1 landing, footprint 12.3.
#  * pitch 4 in 12 = atan(1/3) = 18.4349 degrees = 33.3333 percent; elevation 4.5 at 3.0 from an eave at 3.0: atan(0.5) = 26.5651.
#  * L-shaped zone (6 x 5 minus 3 x 3) = 21; minus a 0.5 stub and a 0.3 pilaster = 20.2; volume (21 - 0.5) * 2.5 = 51.25.
#  * 6 x 4 zone, ceiling 3.0 - 0.3 x: 16 under 2.4, 4 under 1.5, counted 24 - 0.5 * 12 - 4 = 14.
#  * recesses (0.15, 0.30), (0.08, 0.50), (0.20, 0.10), (0.05, 0.05) with minimums 0.10 / 0.25, OR rule: 0.30 + 0.50 + 0.10 = 0.90.
#  * wall 6 x 2.8 x 0.3 with a 0.9 x 2.1 door and a 1.2 x 1.2 window: face 16.8 - 3.33 = 13.47, volume 5.04 / 4.041.
#  * wall 8 long sloping 2 -> 4, 0.2 thick, openings 2 and 1.5 (the second cut by the sloped top): face 24 - 3.5 = 20.5, volume 4.8 / 4.1.
acalc_ok() { echo "ok   $1"; }
acalc_bad() { echo "FAIL $1"; fail=1; }
acalc_exec() {  # acalc_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 100 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$1" 2>&1
  fi
}
acalc_has() {  # acalc_has <what> <text> <fixed string>
  if grep -qF -- "$3" <<<"$2"; then acalc_ok "$1"; else acalc_bad "$1 (missing '$3')"; fi
}

cat > "$TMPW/archcalc_script.txt" <<'EOS'
StairCheck Riser=0.18 Tread=0.28 Width=1.1
StairCheck Riser=0.2 Tread=0.28 Width=1.1
StairFit Rise=2.8
StairFit Risers=16 Tread=0.28 Rise=3.2
StairFit Risers=16 Tread=0.28 Rise=2.96
RampPlan Rise=0.9 MaxSlope=0.0833333333333333 MaxRun=9 Landing=1.5
RoofPitchConv Rise=4 Run=12
RoofPitchConv Elevation=4.5 Distance=3 Eave=3
ZoneArea Poly=0,0/6,0/6,2/3,2/3,5/0,5 Height=2.5 Elem=0.5:subtract/0.3:reduce/0.16:subtract MinElem=0.2
ZoneArea Poly=0,0/6,0/6,4/0,4 Height=2.5 Ceiling=3:-0.3:0 Limit1=2.4:0.5 Limit2=1.5:1
ZoneArea Poly=0,0/6,0/6,4/0,4 Height=2.5 Recess=0.15:0.3/0.08:0.5/0.2:0.1/0.05:0.05 MinDepth=0.1 MinRecessArea=0.25 Rule=or
WallQuantities Length=6 H1=2.8 Thickness=0.3 Open=1:0.9:0:2.1/3:1.2:0.9:1.2
WallQuantities Length=8 H1=2 H2=4 Thickness=0.2 Open=2:2:0.5:1/6:2:3:2
EOS
ACALC_OUT="$(acalc_exec "$TMPW/archcalc_script.txt" 2>&1 || true)"
acalc_has "StairCheck passes 0.18 / 0.28 / 1.1 with 2R+T = 0.64" "$ACALC_OUT" "StairCheck: riser 0.1800 tread 0.2800 width 1.1000 2R+T=0.6400 -> PASS"
acalc_has "StairCheck fails riser 0.20 (2R+T = 0.68)" "$ACALC_OUT" "StairCheck: riser 0.2000 tread 0.2800 width 1.1000 2R+T=0.6800 -> FAIL"
acalc_has "StairCheck names the broken riser rule" "$ACALC_OUT" "violation: riser 0.2 exceeds the maximum 0.19"
acalc_has "StairFit finds the 14 riser counts for rise 2.8" "$ACALC_OUT" "StairFit: rise 2.8000 -> 14 option(s)"
acalc_has "StairFit first option is 15 risers of 0.1867, tread 0.2567, run 3.5933" "$ACALC_OUT" "first: 15 risers x 0.1867, tread 0.2567, run 3.5933"
acalc_has "StairFit flags a 16-riser stair relinked to story 3.2 as invalid" "$ACALC_OUT" "StairFit: 16 risers over rise 3.2000 -> riser 0.2000 2R+T=0.6800 INVALID"
acalc_has "StairFit proposes 17 risers keeping the 0.28 tread" "$ACALC_OUT" "fix: 17 risers x 0.1882, tread 0.2800, run 4.4800"
acalc_has "StairFit keeps the stair valid for a small story change (2.96)" "$ACALC_OUT" "StairFit: 16 risers over rise 2.9600 -> riser 0.1850 2R+T=0.6500 VALID"
acalc_has "RampPlan splits rise 0.9 at 1:12 into 2 flights with one landing" "$ACALC_OUT" "RampPlan: 2 flight(s), 1 landing(s), run per flight 5.4000, total run 10.8000, footprint 12.3000, slope 0.0833"
acalc_has "RoofPitchConv 4 in 12 is 18.4349 degrees = 33.3333 percent" "$ACALC_OUT" "RoofPitchConv: 18.4349 degrees = 33.3333 percent = 0.3333 rise per run"
acalc_has "RoofPitchConv from a typed elevation gives 26.5651 degrees" "$ACALC_OUT" "RoofPitchConv: 26.5651 degrees = 50.0000 percent = 0.5000 rise per run"
acalc_has "ZoneArea L-shaped zone: area 21, net 20.2, volume 51.25 (only elements above the minimum count)" "$ACALC_OUT" "ZoneArea: gross area 21.0000, net area 20.2000, volume 51.2500 (2 element(s) counted)"
acalc_has "ZoneArea low-ceiling reduction counts 14 of 24" "$ACALC_OUT" "ZoneArea: under limit 1 16.0000, under limit 2 4.0000, counted area 14.0000"
acalc_has "ZoneArea adds 0.90 of wall recess area under the OR rule" "$ACALC_OUT" "ZoneArea: recess area added 0.9000"
acalc_has "WallQuantities door and window: net face 13.47, volumes 5.04 / 4.041" "$ACALC_OUT" "WallQuantities: gross face 16.8000, net face 13.4700, gross volume 5.0400, net volume 4.0410, 2 opening(s)"
acalc_has "WallQuantities sloped top: net face 20.5, volumes 4.8 / 4.1" "$ACALC_OUT" "WallQuantities: gross face 24.0000, net face 20.5000, gross volume 4.8000, net volume 4.1000, 2 opening(s)"
rm -f "$TMPW/archcalc_script.txt"
