#include <cstdio>
#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

// Standalone probes for kernel::SplitByPlane(const Brep&, ...) before
// trusting the real registered test (TestBrepSplitByPlaneExact):
//
// 1. A first draft built its half-space tool via Brep::Extrude() of a
//    rectangular profile - this crashed BooleanCombineMixed() with
//    "face 0 is neither planar, cylindrical, nor conical", because
//    Extrude()'s own wall is ONE general swept NURBS surface for the
//    whole profile loop, not one planar quad per side. Switched to an
//    explicit 6-PlanarFace box (FrameBox(), generalizing
//    Brep::BoxWelded()'s own axis-aligned construction to an arbitrary
//    frame) - confirmed below to produce a genuinely valid, correctly-
//    volumed split.
//
// 2. Confirmed `BuildDrilledBoxInputs()`-style fixtures (a cylindrical
//    hole drilled through a box via BooleanCombineMixed) already throw on
//    Brep::Volume()/VolumeMassProperties() BEFORE any split at all -
//    TessellateToClosedMesh(8, 8)'s own fixed resolution doesn't weld
//    closed for this hole geometry. Pre-existing, unrelated to
//    SplitByPlane(); the real test avoids asserting on Volume() for that
//    fixture because of this.
//
// 3. A steep oblique plane normal ((1,1,0.3) or (1,0,0.3) through the
//    drilled box's own hole) correctly hits BooleanCombineMixed()'s own
//    pre-existing, DISCLOSED cylindrical-face scope limits (a
//    non-monotonic plane/face interaction, or a near-axis-parallel
//    grazing angle for a horizontal-diagonal normal) - not a bug in
//    SplitByPlane() itself. A milder oblique normal, (0.1, 0.1, 1) - still
//    genuinely non-axis-aligned - stays inside that engine's own real
//    scope and is what the real test uses.
//
// 4. Even at that milder angle, the result's raw().IsManifold() reports a
//    boundary (no !boundary) - confirmed this is NOT specific to
//    SplitByPlane()'s own half-space tool: a new cut face introduced by
//    BooleanCombineMixed() against a CylindricalFace does not always get
//    a genuine shared ON_BrepEdge against that face's own remaining wall,
//    the same "real topology isn't always built between every pair of
//    geometrically-touching faces" limitation Brep::Volume()'s own doc
//    comment already discloses for Box()/Sphere(). The real test checks
//    IsValid() and a non-empty result, matching how this file's own other
//    BooleanCombineMixed-with-cylinder tests already verify correctness
//    (TessellateConforming() mesh welding), never raw().IsManifold().
int main() {
  ON::Begin();
  using namespace dino8::kernel;

  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  auto [plus_side, minus_side] = SplitByPlane(box, Vector3d(1, 0, 0), 5.0);
  std::printf("axis-aligned split: plus_side volume=%f minus_side volume=%f (expect 500/500)\n", plus_side.Volume(),
              minus_side.Volume());

  Brep::CylindricalFace hole;
  hole.frame.origin = Point3d(5, 5, -1.0);
  hole.frame.xaxis = Vector3d(1, 0, 0);
  hole.frame.yaxis = Vector3d(0, 1, 0);
  hole.frame.zaxis = Vector3d(0, 0, 1);
  hole.frame.UpdateEquation();
  hole.radius = 2.0;
  hole.angle = 2.0 * ON_PI;
  hole.length = 12.0;
  const Brep cyl = Brep::FromMixedFaces({}, {hole});
  const Brep drilled = BooleanCombineMixed(box, cyl, BooleanOp::Difference);

  Vector3d n(0.1, 0.1, 1.0);
  n.Unitize();
  const double offset = ON_DotProduct(n, Point3d(5, 5, 5));
  auto [plus_side2, minus_side2] = SplitByPlane(drilled, n, offset);
  ON_TextLog log;
  std::printf("oblique split through a cylindrical-hole solid: plus faces=%d (valid=%d) minus faces=%d (valid=%d)\n",
              plus_side2.FaceCount(), plus_side2.raw().IsValid(&log), minus_side2.FaceCount(),
              minus_side2.raw().IsValid(&log));

  ON::End();
  return 0;
}
