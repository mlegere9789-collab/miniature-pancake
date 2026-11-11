// PlanarCurveBoolean: see planar_curve_boolean.h.
#include "dino8/kernel/planar_curve_boolean.h"

namespace dino8::kernel {

PlanarCurveBooleanResult PlanarCurveBoolean(const ON_Curve&, const ON_Curve&, const ON_Plane&, CurveRegionOp, double) {
  PlanarCurveBooleanResult r;
  r.refusal_reason = "not implemented";
  return r;
}

}  // namespace dino8::kernel
