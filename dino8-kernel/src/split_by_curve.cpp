// SplitBrepByProjectedCurve(): see boolean_general.h for the contract.
//
// The cutter is the ruled sheet S(u, v) = curve(u) + v * direction, long enough in both directions that it spans every point of `target`
// along `direction`; the cut itself is SplitBySheet(), so a sheet is all this has to build and the two pieces, their caps and their
// orientation are the engine's own.
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

#include <opennurbs.h>

#include "dino8/kernel/boolean_general.h"

namespace dino8::kernel {

namespace {

// A SplitBySheet() piece is a shell of faces trimmed by dense-polyline cut edges, so it is often NOT IsSolid() (naked T-junction / keyhole
// edges) and its mesh volume can be off by far more than the cut's own accuracy. SealGeneralBooleanResult() sews it into an IsSolid() +
// IsValid() body whose exact divergence-theorem volume is available (Brep::VolumeOfSealedSolid()). The operand volumes are unknown here,
// so its bounds check is skipped (NaN); a piece that cannot be sealed is returned as the engine made it rather than dropped.
Brep SealPiece(Brep piece) {
  if (piece.FaceCount() == 0 || (piece.raw().IsSolid() && piece.raw().IsValid())) return piece;
  try {
    SealedGeneralBoolean sealed = SealGeneralBooleanResult(piece, BooleanOp::Union, std::nan(""), std::nan(""));
    if (sealed.brep.FaceCount() > 0 && sealed.brep.raw().IsSolid() && sealed.brep.raw().IsValid()) return std::move(sealed.brep);
  } catch (const std::exception&) {
  }
  return piece;
}

bool IsSealedOrEmpty(const Brep& b) { return b.FaceCount() == 0 || (b.raw().IsSolid() && b.raw().IsValid()); }

}  // namespace

std::pair<Brep, Brep> SplitBrepByProjectedCurve(const Brep& target, const NurbsCurve& curve, Vector3d direction,
                                                 double tolerance) {
  if (target.FaceCount() == 0) {
    throw std::invalid_argument("dino8::kernel::SplitBrepByProjectedCurve: target has no faces");
  }
  if (!(tolerance > 0.0) || !std::isfinite(tolerance)) {
    throw std::invalid_argument("dino8::kernel::SplitBrepByProjectedCurve: tolerance must be positive");
  }
  if (!direction.IsValid() || !direction.Unitize()) {
    throw std::invalid_argument("dino8::kernel::SplitBrepByProjectedCurve: direction must be a non-zero finite vector");
  }
  const ON_NurbsCurve& c = curve.raw();
  if (!c.IsValid() || c.CVCount() < 2) {
    throw std::invalid_argument("dino8::kernel::SplitBrepByProjectedCurve: curve is not a valid curve of at least two control points");
  }
  if (c.IsClosed()) {
    throw std::invalid_argument(
        "dino8::kernel::SplitBrepByProjectedCurve: curve is closed - a closed cutter does not sever a solid into two sides this way");
  }

  // A curve whose every tangent is parallel to `direction` sweeps a zero-area sheet.
  const ON_Interval dom = c.Domain();
  bool any_transverse = false;
  for (int i = 0; i <= 32 && !any_transverse; ++i) {
    ON_3dVector t = c.TangentAt(dom.ParameterAt(i / 32.0));
    if (!t.IsValid() || t.IsZero()) continue;
    t.Unitize();
    if (ON_CrossProduct(t, direction).Length() > 1e-9) any_transverse = true;
  }
  if (!any_transverse) {
    throw std::invalid_argument("dino8::kernel::SplitBrepByProjectedCurve: curve is parallel to direction everywhere (zero-area cutting sheet)");
  }

  // Sweep half-length: every target point must lie within it of the curve's own position along `direction`.
  const BoundingBox tb = target.GetTightBoundingBox();
  const ON_BoundingBox tbox(tb.min, tb.max);
  const ON_BoundingBox cbox = c.BoundingBox();
  const double reach = tbox.Diagonal().Length() + cbox.Diagonal().Length() + tbox.Center().DistanceTo(cbox.Center()) + 1.0;
  const double half = 2.0 * reach;

  // SplitBySheet() cuts exactly what it is given but is sensitive to where the sheet's own parametric grid falls relative to `target`'s faces
  // (measured: a sheet whose mid-isocurve lies IN a face plane of the target, e.g. a curve drawn on a box's bottom face, is refused with "an edge
  // is claimed by 3 or more fragment loops" while the identical cut with the sheet merely slid along `direction` succeeds). The cut does not
  // depend on the sweep's offset, so on a refusal slide the sheet and retry; only if every placement is refused is the first refusal rethrown.
  static const double kOffsets[] = {0.618033988749895, 0.381966011250105, 0.77, 0.23, 0.9, 0.1};
  // Whether the pieces seal also depends on the placement (the dense-polyline cut edges of the pieces and of the cap pair up differently), so
  // the first placement whose non-empty pieces BOTH seal wins; if none does, the first unsealed result is returned.
  std::exception_ptr first_error;
  bool have_fallback = false;
  std::pair<Brep, Brep> fallback;
  for (const double offset : kOffsets) {
    NurbsCurve shifted = curve;
    shifted.raw().Translate(-offset * half * direction);
    try {
      const Brep sheet = Brep::Extrude(shifted, 2.0 * half * direction, /*cap=*/false);
      std::pair<Brep, Brep> halves = SplitBySheet(target, sheet, tolerance);
      std::pair<Brep, Brep> sealed{SealPiece(std::move(halves.first)), SealPiece(std::move(halves.second))};
      if (IsSealedOrEmpty(sealed.first) && IsSealedOrEmpty(sealed.second)) return sealed;
      if (!have_fallback) {
        fallback = std::move(sealed);
        have_fallback = true;
      }
    } catch (const std::exception&) {
      if (!first_error) first_error = std::current_exception();
    }
  }
  if (have_fallback) return fallback;
  std::rethrow_exception(first_error);
}

}  // namespace dino8::kernel
