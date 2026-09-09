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
 * Usage: dwg_fixture_gen <output.dwg>
 *
 * Contents (all in millimetres, matching Dino 8's default document units):
 *   - a LINE and a CIRCLE directly in model space (baseline sanity, also
 *     covered by the plain export/import DWG test)
 *   - a BLOCK_HEADER "TESTBLK" with base point (0,0,0) containing one LINE
 *     from (0,0,0) to (2,0,0)
 *   - an INSERT of TESTBLK at (100,50,0), scale (3,3,1), rotation 90
 *     degrees - so the flattened line should land at (100,50,0) to
 *     (100,56,0) (a 2-unit line, scaled 3x to 6, rotated 90 degrees from
 *     +X to +Y, translated to the insertion point).
 */
#include <stdio.h>
#include <stdlib.h>

#include <dwg.h>
#include <dwg_api.h>

/* pi/2, spelled out rather than relying on math.h's (non-standard,
 * glibc-extension) M_PI_2 macro. */
#define DINO8_HALF_PI 1.5707963267948966

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <output.dwg>\n", argv[0]);
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
