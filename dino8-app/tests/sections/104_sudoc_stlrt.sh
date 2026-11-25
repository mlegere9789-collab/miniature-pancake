# ---------------------------------------------------------------------------
# SUDOC_STLRT: STL for 3D printing: export a solid, re-import it, volume within 1e-9 relative and watertight (D8-SKETCHUP-IO-0010). A 10 x 20 x 30 mm box (coordinates exact in the
# 32-bit floats of the STL), volume 10 x 20 x 30 = 6000 mm^3, bounding box (0,0,0)-(10,20,30). The box is exported to b.stl, the document is cleared (New), b.stl is imported, and the
# import is exported twice more: rt.stl and rt.obj. The stand-alone rwi_oracle.py (Python standard library only) parses all three files: signed volume by the divergence theorem
# within 1e-9 relative, bounding box, a closed manifold (every edge shared by two oppositely directed facets). The app's import also reports the solid
# check (the meshed box has 56 triangles). Decoys: the oracle must reject the volume 6000.01 at the same 1e-9 relative tolerance, and must reject "open" for the closed solid.
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip SUDOC_STLRT (needs python3)"
else
. "$HERE/rwi_lib.sh"
SUDOC_S_D="$TMPW/sudoc_stlrt"; mkdir -p "$SUDOC_S_D"
cat > "$TMPW/sudoc_stlrt_script.txt" <<EOS
SetActiveViewport Top
New
Box 0,0,0 10,20,0 30
SelAll
Export $SUDOC_S_D/b.stl
New
Import $SUDOC_S_D/b.stl
SelAll
Export $SUDOC_S_D/rt.stl
SelAll
Export $SUDOC_S_D/rt.obj
EOS
rwi_app "$TMPW/sudoc_stlrt_script.txt"
rwi_has "Exported $SUDOC_S_D/b.stl" "SUDOC_STLRT: the box was exported to b.stl"
rwi_has "Imported $SUDOC_S_D/b.stl" "SUDOC_STLRT: b.stl was imported into the cleared document"
rwi_has "mesh is closed and manifold" "SUDOC_STLRT: the import reports the solid check: closed and manifold"
rwi_oracle "SUDOC_STLRT: b.stl parsed independently: volume 6000 within 1e-9 relative, bbox (0,0,0)-(10,20,30), closed" stl "$SUDOC_S_D/b.stl" --min 0,0,0 --max 10,20,30 --vol 6000 --vol-tol 1e-9 --closed
rwi_oracle "SUDOC_STLRT: rt.stl (import, then export) parsed independently: volume 6000 within 1e-9 relative, closed" stl "$SUDOC_S_D/rt.stl" --min 0,0,0 --max 10,20,30 --vol 6000 --vol-tol 1e-9 --closed
rwi_oracle "SUDOC_STLRT: rt.obj (import, then export) parsed independently: volume 6000 within 1e-9 relative, bbox (0,0,0)-(10,20,30), closed" obj "$SUDOC_S_D/rt.obj" --parts 1 --min 0,0,0 --max 10,20,30 --vol 6000 --vol-tol 1e-9 --closed
if python3 -I "$HERE/rwi_oracle.py" stl "$SUDOC_S_D/rt.stl" --vol 6000.01 --vol-tol 1e-9 >/dev/null 2>&1; then rwi_bad "SUDOC_STLRT_decoy: the oracle accepted the volume 6000.01 at 1e-9"; else rwi_ok "SUDOC_STLRT_decoy: the oracle rejects the volume 6000.01 at 1e-9 relative"; fi
if python3 -I "$HERE/rwi_oracle.py" stl "$SUDOC_S_D/rt.stl" --open >/dev/null 2>&1; then rwi_bad "SUDOC_STLRT_decoy: the oracle accepted the solid as open"; else rwi_ok "SUDOC_STLRT_decoy: the oracle rejects 'open' for the re-imported solid"; fi
fi
