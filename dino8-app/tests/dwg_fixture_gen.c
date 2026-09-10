/* dwg_fixture_gen - writes a small, real DWG file containing a block
 * definition and a scaled/rotated/translated INSERT of it, built directly
 * through GNU LibreDWG's public dwg_add_* API (the same API LibreDWG's own
 * test/unit-testing/add_test.c uses to build DWGs by hand).
 *
 * Dino 8 has no live block/instance object of its own - a block instance
 * placed in-app is stored as already-flattened geometry (see
 * InstantiateBlock in cmd_drafting.cpp) - so Dino8-authored DWG exports
 * never contain a real INSERT entity for ImportDwg's block-flattening code
 * (WalkDwgEntities in src/io/FileExchange.cpp) to read back. This generator
 * exists to give that code a real INSERT, built by a completely independent
 * path (LibreDWG's own object-construction API, not Dino 8's), so
 * smoke.sh's DWG test proves the flattening logic actually works rather
 * than merely not crashing on Dino8's own output.
 *
 * Usage: dwg_fixture_gen <output.dwg> [hatch]
 *
 * Default contents (all in millimetres, matching Dino 8's default document
 * units):
 *   - a LINE and a CIRCLE directly in model space (baseline sanity, also
 *     covered by the plain export/import DWG test)
 *   - a BLOCK_HEADER "TESTBLK" with base point (0,0,0) containing one LINE
 *     from (0,0,0) to (2,0,0)
 *   - an INSERT of TESTBLK at (100,50,0), scale (3,3,1), rotation 90
 *     degrees - so the flattened line should land at (100,50,0) to
 *     (100,56,0) (a 2-unit line, scaled 3x to 6, rotated 90 degrees from
 *     +X to +Y, translated to the insertion point).
 *
 * With a second argument "hatch", instead writes a single real HATCH
 * entity (see write_hatch_fixture below) - kept as a separate mode/output
 * file rather than folded into the fixture above so it doesn't change that
 * fixture's existing curve/point/block counts and break the INSERT/TEXT
 * smoke.sh assertions that already pin those exactly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dwg.h>
#include <dwg_api.h>

/* pi/2, spelled out rather than relying on math.h's (non-standard,
 * glibc-extension) M_PI_2 macro. */
#define DINO8_HALF_PI 1.5707963267948966

/* A single HATCH entity, pattern-filled (ANSI31) with one boundary path
 * that is a real DWG "polyline path" (Dwg_HATCH_Path::flag bit 2) - a
 * closed 10x10 square - proving ImportDwg's DWG_TYPE_HATCH polyline-
 * boundary case (WalkDwgEntities in src/io/FileExchange.cpp) against a
 * real HATCH built by LibreDWG's own object-construction API, independent
 * of Dino 8's own writer (which has no HATCH export at all - see
 * ExportDwg/ExportDxf).
 *
 * dwg_add_HATCH only emits a polyline-type path (flag bit 2) for a
 * DWG_TYPE_POLYLINE_2D boundary object (or an LWPOLYLINE with a nonzero
 * bulge on every vertex); a plain LWPOLYLINE boundary is instead split
 * into an edge-type path of LINE segments (flag bit 2 clear) - the case
 * ImportDwg's own comments document as out of scope. So the boundary here
 * is a real (old-style) POLYLINE_2D, not the more common LWPOLYLINE.
 */
static int
write_hatch_fixture (const char *path)
{
  Dwg_Data *dwg = dwg_new_Document (R_2000, 0, 0);
  if (!dwg)
    {
      fprintf (stderr, "dwg_new_Document failed\n");
      return 1;
    }
  Dwg_Object *mspace = dwg_model_space_object (dwg);
  if (!mspace)
    {
      fprintf (stderr, "dwg_model_space_object failed\n");
      return 1;
    }
  Dwg_Object_BLOCK_HEADER *hdr = mspace->tio.object->tio.BLOCK_HEADER;

  const dwg_point_2d pts[] = { { 0.0, 0.0 }, { 10.0, 0.0 }, { 10.0, 10.0 }, { 0.0, 10.0 } };
  Dwg_Entity_POLYLINE_2D *pline = dwg_add_POLYLINE_2D (hdr, 4, pts);
  if (!pline)
    {
      fprintf (stderr, "dwg_add_POLYLINE_2D failed\n");
      return 1;
    }
  int error = 0;
  Dwg_Object *pline_obj = dwg_obj_generic_to_object ((const dwg_obj_generic *)pline, &error);
  if (error || !pline_obj)
    {
      fprintf (stderr, "dwg_obj_generic_to_object failed\n");
      return 1;
    }
  const Dwg_Object *pathobjs[1] = { pline_obj };
  Dwg_Entity_HATCH *hatch = dwg_add_HATCH (hdr, /*pattern_type=*/1, "ANSI31", /*is_associative=*/false, 1, pathobjs);
  if (!hatch)
    {
      fprintf (stderr, "dwg_add_HATCH failed\n");
      return 1;
    }

  const int werr = dwg_write_file (path, dwg);
  dwg_free (dwg);
  if (werr >= DWG_ERR_CRITICAL)
    {
      fprintf (stderr, "dwg_write_file failed: 0x%x\n", werr);
      return 1;
    }
  printf ("wrote %s\n", path);
  return 0;
}

/* A single SPLINE entity built via LibreDWG's own dwg_add_SPLINE - marked
 * "Experimental. Does not work yet properly" in dwg_api.h, and indeed: it
 * only ever populates fit_pts (num_ctrl_pts stays 0), never real NURBS
 * control points, confirmed by hand before relying on it here. So this
 * fixture exercises ImportDwg's DWG_TYPE_SPLINE fit-points fallback path
 * (WalkDwgEntities in src/io/FileExchange.cpp) - the same fallback DXF
 * SPLINE import already had for fit-point-only splines - not its
 * primary real-control-point path (which no available fixture generator
 * can produce; the control-point path is exercised indirectly by every
 * curve this app itself writes as a NURBS, and is a straightforward,
 * directly-inspectable translation of the same struct fields DXF's own
 * SPLINE cvs/knots/weights handling already reads).
 */
static int
write_spline_fixture (const char *path)
{
  Dwg_Data *dwg = dwg_new_Document (R_2000, 0, 0);
  if (!dwg)
    {
      fprintf (stderr, "dwg_new_Document failed\n");
      return 1;
    }
  Dwg_Object *mspace = dwg_model_space_object (dwg);
  if (!mspace)
    {
      fprintf (stderr, "dwg_model_space_object failed\n");
      return 1;
    }
  Dwg_Object_BLOCK_HEADER *hdr = mspace->tio.object->tio.BLOCK_HEADER;

  const dwg_point_3d fit[4] = { { 0.0, 0.0, 0.0 }, { 5.0, 10.0, 0.0 }, { 10.0, -5.0, 0.0 }, { 15.0, 5.0, 0.0 } };
  const dwg_point_3d tan1 = { 1.0, 0.0, 0.0 };
  const dwg_point_3d tan2 = { 1.0, 0.0, 0.0 };
  Dwg_Entity_SPLINE *spline = dwg_add_SPLINE (hdr, 4, fit, &tan1, &tan2);
  if (!spline)
    {
      fprintf (stderr, "dwg_add_SPLINE failed\n");
      return 1;
    }

  const int werr = dwg_write_file (path, dwg);
  dwg_free (dwg);
  if (werr >= DWG_ERR_CRITICAL)
    {
      fprintf (stderr, "dwg_write_file failed: 0x%x\n", werr);
      return 1;
    }
  printf ("wrote %s\n", path);
  return 0;
}

/* A single MTEXT entity built via LibreDWG's own dwg_add_MTEXT - marked
 * "Experimental. Does not work yet properly" in dwg_api.h, like
 * dwg_add_SPLINE above, so its output is checked by hand before relying on
 * it here: dwg_add_MTEXT populates ins_pt/rect_width/text (via
 * dwg_add_u8_input, a real UTF-8 copy) and defaults x_axis_dir to (1,0,0)
 * (no rotation), attachment to 1 (top-left), and text_height from the
 * document's $TEXTSIZE header variable - not from any argument this
 * function takes. This fixture overwrites text_height and attachment
 * directly on the returned Dwg_Entity_MTEXT (plain public struct fields,
 * the same ones ImportDwg's DWG_TYPE_MTEXT case reads - see WalkDwgEntities
 * in src/io/FileExchange.cpp) to get a known height and to exercise a
 * non-default attachment point (5 = middle-center) rather than only ever
 * testing the top-left default.
 *
 * The text itself embeds the identical inline-formatting-code shape as
 * tests/dxf_mtext_fixture.dxf ("{\C1;Hi}\PH\H2x;i" - a colour override and
 * a formatting-group brace pair around the first "Hi", a \P paragraph
 * break, then a mid-run height-override code inside the second "Hi"), so
 * this proves ImportDwg's MTextToLines stripping against a real DWG_TYPE_
 * MTEXT independent of Dino 8's own writer (which has no MTEXT export -
 * ExportDxf/ExportDwg never write one), not just its glyph-outline layout.
 */
static int
write_mtext_fixture (const char *path)
{
  Dwg_Data *dwg = dwg_new_Document (R_2000, 0, 0);
  if (!dwg)
    {
      fprintf (stderr, "dwg_new_Document failed\n");
      return 1;
    }
  Dwg_Object *mspace = dwg_model_space_object (dwg);
  if (!mspace)
    {
      fprintf (stderr, "dwg_model_space_object failed\n");
      return 1;
    }
  Dwg_Object_BLOCK_HEADER *hdr = mspace->tio.object->tio.BLOCK_HEADER;

  const dwg_point_3d ins_pt = { 20.0, 20.0, 0.0 };
  Dwg_Entity_MTEXT *mtext
      = dwg_add_MTEXT (hdr, &ins_pt, 0.0, "{\\C1;Hi}\\PH\\H2x;i");
  if (!mtext)
    {
      fprintf (stderr, "dwg_add_MTEXT failed\n");
      return 1;
    }
  mtext->text_height = 5.0;
  mtext->attachment = 5; /* middle-center, not the top-left default */

  const int werr = dwg_write_file (path, dwg);
  dwg_free (dwg);
  if (werr >= DWG_ERR_CRITICAL)
    {
      fprintf (stderr, "dwg_write_file failed: 0x%x\n", werr);
      return 1;
    }
  printf ("wrote %s\n", path);
  return 0;
}

/* A real DIMENSION_LINEAR and a real DIMENSION_RADIUS entity, built via
 * LibreDWG's own dwg_add_DIMENSION_LINEAR/dwg_add_DIMENSION_RADIUS -
 * *not* marked "Experimental" in dwg_api.h (unlike dwg_add_SPLINE/
 * dwg_add_MTEXT above), and confirmed by reading their implementation
 * (src/dwg_api.c) directly: dwg_add_DIMENSION_LINEAR stores xline1_pt/
 * xline2_pt verbatim and def_pt verbatim (its own comment there literally
 * says "// dimline_pt"), and rotation_angle verbatim into dim_rotation, in
 * RADIANS (ADD_CHECK_ANGLE rejects |angle| > 12, i.e. it expects radians,
 * not degrees) - so 0.0 here really is a horizontal dimension.
 * dwg_add_DIMENSION_RADIUS stores center_pt into def_pt and chord_pt into
 * first_arc_pt verbatim. This is exactly the field mapping
 * DxfImporter::Dimension/WalkDwgEntities in src/io/FileExchange.cpp expect
 * (see that comment for the DXF-side group-code derivation, cross-checked
 * against this same dwg.spec).
 *
 * Linear: xline1=(0,0,0), xline2=(10,0,0), def_pt(dimline location)=
 * (5,5,0), rotation=0 (horizontal) - so the measured distance is exactly
 * 10 (the two points' X-difference), independent of the dimension-line's Y
 * offset.
 * Radius: center=(40,0,0), chord (first_arc_pt)=(45,0,0) - radius exactly
 * 5 (the distance between them) - leader_len=1.
 */
static int
write_dim_fixture (const char *path)
{
  Dwg_Data *dwg = dwg_new_Document (R_2000, 0, 0);
  if (!dwg)
    {
      fprintf (stderr, "dwg_new_Document failed\n");
      return 1;
    }
  Dwg_Object *mspace = dwg_model_space_object (dwg);
  if (!mspace)
    {
      fprintf (stderr, "dwg_model_space_object failed\n");
      return 1;
    }
  Dwg_Object_BLOCK_HEADER *hdr = mspace->tio.object->tio.BLOCK_HEADER;

  const dwg_point_3d x1 = { 0.0, 0.0, 0.0 };
  const dwg_point_3d x2 = { 10.0, 0.0, 0.0 };
  const dwg_point_3d defp = { 5.0, 5.0, 0.0 };
  Dwg_Entity_DIMENSION_LINEAR *lin
      = dwg_add_DIMENSION_LINEAR (hdr, &x1, &x2, &defp, 0.0);
  if (!lin)
    {
      fprintf (stderr, "dwg_add_DIMENSION_LINEAR failed\n");
      return 1;
    }

  const dwg_point_3d center = { 40.0, 0.0, 0.0 };
  const dwg_point_3d chord = { 45.0, 0.0, 0.0 };
  Dwg_Entity_DIMENSION_RADIUS *rad
      = dwg_add_DIMENSION_RADIUS (hdr, &center, &chord, 1.0);
  if (!rad)
    {
      fprintf (stderr, "dwg_add_DIMENSION_RADIUS failed\n");
      return 1;
    }

  const int werr = dwg_write_file (path, dwg);
  dwg_free (dwg);
  if (werr >= DWG_ERR_CRITICAL)
    {
      fprintf (stderr, "dwg_write_file failed: 0x%x\n", werr);
      return 1;
    }
  printf ("wrote %s\n", path);
  return 0;
}

int
main (int argc, char **argv)
{
  if (argc == 3 && strcmp (argv[2], "hatch") == 0)
    return write_hatch_fixture (argv[1]);
  if (argc == 3 && strcmp (argv[2], "spline") == 0)
    return write_spline_fixture (argv[1]);
  if (argc == 3 && strcmp (argv[2], "mtext") == 0)
    return write_mtext_fixture (argv[1]);
  if (argc == 3 && strcmp (argv[2], "dim") == 0)
    return write_dim_fixture (argv[1]);
  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <output.dwg> [hatch|spline|mtext|dim]\n", argv[0]);
      return 2;
    }

  Dwg_Data *dwg = dwg_new_Document (R_2000, 0, 0);
  if (!dwg)
    {
      fprintf (stderr, "dwg_new_Document failed\n");
      return 1;
    }

  Dwg_Object *mspace = dwg_model_space_object (dwg);
  if (!mspace)
    {
      fprintf (stderr, "dwg_model_space_object failed\n");
      return 1;
    }
  Dwg_Object_BLOCK_HEADER *ms_hdr = mspace->tio.object->tio.BLOCK_HEADER;

  const dwg_point_3d line_a = { 5.0, 5.0, 0.0 };
  const dwg_point_3d line_b = { 25.0, 5.0, 0.0 };
  dwg_add_LINE (ms_hdr, &line_a, &line_b);

  const dwg_point_3d circle_c = { 10.0, 20.0, 0.0 };
  dwg_add_CIRCLE (ms_hdr, &circle_c, 4.5);

  /* A real TEXT entity, for ImportDwg's glyph-outline conversion
   * (WalkDwgEntities's DWG_TYPE_TEXT case in src/io/FileExchange.cpp) to
   * read back - Dino 8 itself only ever writes text as pre-converted
   * curve outlines (see AddTextCurves in cmd_annotate.cpp), never a native
   * TEXT entity, so a Dino8-authored DWG export can never exercise this
   * import path either; same rationale as the INSERT fixture above. */
  const dwg_point_3d text_pt = { -30.0, 0.0, 0.0 };
  dwg_add_TEXT (ms_hdr, "Hi", &text_pt, 5.0);

  /* Block definition: one 2-unit LINE along +X from the block's origin. */
  Dwg_Object_BLOCK_HEADER *blk = dwg_add_BLOCK_HEADER (dwg, "TESTBLK");
  if (!blk)
    {
      fprintf (stderr, "dwg_add_BLOCK_HEADER failed\n");
      return 1;
    }
  dwg_add_BLOCK (blk, "TESTBLK");
  const dwg_point_3d blk_a = { 0.0, 0.0, 0.0 };
  const dwg_point_3d blk_b = { 2.0, 0.0, 0.0 };
  dwg_add_LINE (blk, &blk_a, &blk_b);
  dwg_add_ENDBLK (blk);

  /* INSERT: base at origin (dwg_add_BLOCK_HEADER's default), placed at
   * (100,50,0), scaled 3x in X/Y, rotated 90 degrees (pi/2 rad). */
  const dwg_point_3d ins_pt = { 100.0, 50.0, 0.0 };
  dwg_add_INSERT (ms_hdr, &ins_pt, "TESTBLK", 3.0, 3.0, 1.0, DINO8_HALF_PI);

  const int werr = dwg_write_file (argv[1], dwg);
  dwg_free (dwg);
  if (werr >= DWG_ERR_CRITICAL)
    {
      fprintf (stderr, "dwg_write_file failed: 0x%x\n", werr);
      return 1;
    }
  printf ("wrote %s\n", argv[1]);
  return 0;
}
