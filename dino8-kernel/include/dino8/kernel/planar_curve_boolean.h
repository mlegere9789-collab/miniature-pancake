// Exact (non-tessellated) boolean of two simple closed PLANAR CURVES - the curve-level counterpart of PolygonBooleanPlanar() for
// regions bounded by arcs, ellipses, splines or any mixture, not just straight edges. Implemented in src/planar_curve_boolean.cpp.
//
// Construction (no sampling of the result): the two boundaries are intersected with IntersectCurves() (Newton-polished), each is
// cut at its own crossing parameters with the exact knot-insertion Trim of OpenNURBS, every piece is classified by whether a point
// strictly inside the piece lies inside or outside the OTHER region (winding number of a dense sampling of that boundary - used for
// classification only, never for geometry), the pieces the operation keeps are chained end to end by position, and each chain is
// Join()ed into one closed NURBS curve. A circle stays exactly a circle arc (rational, not a polyline): the loops of two
// intersecting circles are made of the two original rational arcs.
//
// Orientation: the result follows the PolygonBooleanPlanar() convention - an outer boundary runs counterclockwise as seen from
// `plane.zaxis`, a hole clockwise. Inputs may run either way (a clockwise input is reversed on a private copy first).
//
// Honest scope (each refusal is a measured reason in `refusal_reason`; `loops` is left empty):
//   * both curves must be closed, lie in `plane` within `tolerance`, and be simple (no self-crossing, checked with
//     IntersectCurveSelfIntersections());
//   * a TANGENT contact (the two boundaries touch without crossing: externally or internally tangent circles) or a shared span of
//     boundary (coincident arcs) is refused - it needs a classification of degenerate pieces this does not attempt;
//   * a crossing that lands within `tolerance` of another is refused (a piece shorter than the tolerance has no reliable side);
//   * operands are single loops (no holes), as for PolygonBooleanPlanar().
#pragma once

#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

enum class CurveRegionOp { Union, Intersection, Difference };

struct PlanarCurveBooleanResult {
  bool ok = false;
  std::string refusal_reason;
  // Closed loops of the result region: `loop_is_hole[i]` is true for a clockwise (hole) boundary.
  std::vector<ON_NurbsCurve> loops;
  std::vector<bool> loop_is_hole;
  int crossings = 0;  // transversal boundary crossings found (0 for nested or disjoint operands)
};

// a OP b, where Difference is a - b. `tolerance` is the gap allowed when chaining pieces and the distance under which two
// crossings, or a piece and the other boundary, are considered coincident.
PlanarCurveBooleanResult PlanarCurveBoolean(const ON_Curve& a, const ON_Curve& b, const ON_Plane& plane, CurveRegionOp op,
                                            double tolerance = 1e-6);

}  // namespace dino8::kernel
