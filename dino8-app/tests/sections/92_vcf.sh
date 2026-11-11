# ---------------------------------------------------------------------------
# VCF_: ViewCaptureToFile writes PNG and records DPI (vcf_script.txt), checked by an independent parser (image_check.py: struct + zlib
# for the PNG chunks / BMP header and its own pixel decoders; standard library only, the runner has no Pillow). Before: BMP only, resolution unknown, "x.png" became "x.png.bmp".
#   PNG   : signature, every chunk CRC, IHDR 8-bit RGB, one IDAT inflating to H x (1 + 3 W), no pHYs without Dpi
#   Dpi   : PNG pHYs = round(dpi / 0.0254) = 11811 (300), BMP biX/YPelsPerMeter = 3780 (96)
#   same  : the PNG and the BMP of one static frame decode to the same pixels
#   legacy: "vc_noext" still becomes vc_noext.bmp, and no vc.png.bmp is left behind
# ---------------------------------------------------------------------------
mkdir -p "$TMPW/vcf"
sed "s|@TMP@|$TMPW/vcf|g" "$HERE/vcf_script.txt" > "$TMPW/vcf_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  VCFO="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/vcf_script.txt" 2>&1)" || { echo "$VCFO" | tail -20; echo "FAIL VCF_run: script exited non-zero"; fail=1; VCFO=""; }
else
  VCFO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/vcf_script.txt" 2>&1)" || { echo "$VCFO" | tail -20; echo "FAIL VCF_run: script exited non-zero"; fail=1; VCFO=""; }
fi
vcf_run() {  # args to image_check.py
  VCF_R="$(python3 "$HERE/image_check.py" "$@" 2>&1)" && VCF_RC=0 || VCF_RC=$?
  echo "$VCF_R"
  if [ "$VCF_RC" != "0" ] || grep -q "^FAIL" <<<"$VCF_R"; then fail=1; fi
}
vcf_file() { if [ -s "$TMPW/vcf/$1" ]; then return 0; fi; echo "FAIL VCF_$1: $1 was not written"; { grep -E "Saved|Cannot|rendered|Could not" <<<"$VCFO" || true; } | head -4 | sed 's/^/     app: /'; fail=1; return 1; }
if vcf_file vc.png; then vcf_run png "$TMPW/vcf/vc.png" 0; vcf_run notblank "$TMPW/vcf/vc.png"; fi
if vcf_file vc300.png; then vcf_run png "$TMPW/vcf/vc300.png" 300; fi
if vcf_file vc.bmp; then vcf_run bmp "$TMPW/vcf/vc.bmp" 0; fi
if vcf_file vc96.bmp; then vcf_run bmp "$TMPW/vcf/vc96.bmp" 96; fi
if [ -s "$TMPW/vcf/vc.bmp" ] && [ -s "$TMPW/vcf/vc.png" ]; then vcf_run same "$TMPW/vcf/vc.bmp" "$TMPW/vcf/vc.png"; fi
if [ -s "$TMPW/vcf/vc.png" ] && [ -s "$TMPW/vcf/vc300.png" ]; then vcf_run same "$TMPW/vcf/vc.png" "$TMPW/vcf/vc300.png"; fi
if vcf_file vc_noext.bmp; then vcf_run bmp "$TMPW/vcf/vc_noext.bmp" 0; fi
if [ -e "$TMPW/vcf/vc.png.bmp" ]; then echo "FAIL VCF_legacy: vc.png.bmp exists (the old .png -> .png.bmp behaviour)"; fail=1; else echo "ok   VCF_legacy: no vc.png.bmp was written for a .png path"; fi
