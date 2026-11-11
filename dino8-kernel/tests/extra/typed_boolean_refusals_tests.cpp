// Typed, structured refusals of the general Boolean engine: BooleanReassemblyError (the "edge claimed by 3 or more fragment loops"
// refusal of BooleanCombineGeneral) and BooleanSealError (every "not a closed solid" refusal of BooleanCombineGeneralSealed).
//
// Before: both were plain std::runtime_error whose only structure was text. Now both are still std::runtime_error with the identical
// what() text (so every existing catch is untouched) but carry reason(), function_name() and the facts as fields.
//
// The oracles for the structured fields are independent of the engine's own bookkeeping:
//   * the reported non-manifold edge's end points must lie on the ACTUAL intersection of the operands: on the sphere (distance from its
//     centre = its radius) AND on a face plane of the box, both computed here from the operands' own definitions;
//   * the reported naked / non-manifold edge counts, total naked length and sliver count must equal what this test counts on
//     BooleanSealError::attempt() with plain ON_Brep calls (edge trim counts, ON_Curve::GetLength, the bounding-box diagonal);
//   * the orientation-clash case must have every edge shared by exactly two faces yet ON_Brep::IsManifold() not oriented.
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/boolean_diagnostics.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneral;
using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanFailureReason;
using dino8::kernel::BooleanOp;
using dino8::kernel::BooleanReassemblyError;
using dino8::kernel::BooleanSealError;
using dino8::kernel::Brep;
using dino8::kernel::Point3d;

// A box [0,2]^3 and a sphere of radius 1 + d centred in it: for d > 0 the sphere pokes out of all six faces by d (shallow crossings).
struct Shallow {
  Brep box = Brep::Box(0, 0, 0, 2, 2, 2);
  double radius;
  Brep sphere;
  explicit Shallow(double d) : radius(1.0 + d), sphere(Brep::Sphere(Point3d(1, 1, 1), 1.0 + d)) {}
};

bool OnSphere(const Point3d& p, double r) { return std::fabs(p.DistanceTo(Point3d(1, 1, 1)) - r) < 2e-3; }
bool OnABoxFacePlane(const Point3d& p) {
  for (const double v : {0.0, 2.0})
    if (std::fabs(p.x - v) < 2e-3 || std::fabs(p.y - v) < 2e-3 || std::fabs(p.z - v) < 2e-3) return true;
  return false;
}

}  // namespace

D8_EXTRA_TEST(TypedReassemblyRefusalCarriesTheNonManifoldEdge) {
  const Shallow s(0.01);
  bool threw = false, typed = false, as_runtime_error = false;
  BooleanFailureReason reason = BooleanFailureReason::Unclassified;
  std::string fn, what;
  dino8::kernel::BooleanNonManifoldEdge edge;
  try {
    BooleanCombineGeneral(s.box, s.sphere, BooleanOp::Union);
  } catch (const BooleanReassemblyError& e) {
    threw = typed = as_runtime_error = true;
    reason = e.reason();
    fn = e.function_name();
    what = e.what();
    edge = e.edge();
  } catch (const std::runtime_error&) {
    threw = true;
    as_runtime_error = true;
  } catch (...) {
    threw = true;
  }
  d8test::Check(threw, "box + a sphere 0.01 past inscribed: Union is refused (the fixture still reaches the reassembly refusal)");
  d8test::Check(typed, "...with the typed BooleanReassemblyError, not a plain std::runtime_error");
  if (!typed) return;
  d8test::Check(as_runtime_error, "...which is still a std::runtime_error");
  d8test::Check(reason == BooleanFailureReason::NonManifoldResult, "reason() is NonManifoldResult");
  d8test::Check(fn == "BooleanCombineGeneral", "function_name() names the engine");
  d8test::Check(what.find("dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result)") == 0,
                "what() text is the unchanged message (backward compatible)");
  d8test::Check(dino8::kernel::ClassifyBooleanFailureMessage(what) == reason, "the free-text classifier agrees with the typed reason");
  d8test::Check(OnSphere(edge.from, s.radius) && OnSphere(edge.to, s.radius),
                "oracle: both end points of the reported edge lie on the sphere (|p - centre| = 1.01 within 2e-3)");
  d8test::Check(OnABoxFacePlane(edge.from) && OnABoxFacePlane(edge.to),
                "oracle: ... and on a face plane of the box, i.e. on the true sphere/box intersection curve");
  d8test::Check(edge.faces_on_edge.size() >= 2, "the edge was already used by two faces before the third claimed it");
  d8test::Check(edge.offending_face >= 0, "the offending face index is reported");
  d8test::Check(what.find("this loop's face " + std::to_string(edge.offending_face) + "]") != std::string::npos,
                "the structured offending face agrees with the one the message names");
}

D8_EXTRA_TEST(TypedSealRefusalCountsMatchAnIndependentCountOfTheSewnAttempt) {
  const Shallow s(0.01);
  bool typed = false, as_runtime_error = false;
  BooleanFailureReason reason = BooleanFailureReason::Unclassified;
  dino8::kernel::BooleanSealReport rep;
  Brep attempt;
  std::string what, fn;
  try {
    BooleanCombineGeneralSealed(s.box, s.sphere, BooleanOp::Intersection);
  } catch (const BooleanSealError& e) {
    typed = as_runtime_error = true;
    reason = e.reason();
    rep = e.report();
    attempt = e.attempt();
    what = e.what();
    fn = e.function_name();
  } catch (...) {
  }
  d8test::Check(typed, "box intersect a sphere 0.01 past inscribed: refused with the typed BooleanSealError");
  if (!typed) return;
  d8test::Check(as_runtime_error, "...a std::runtime_error");
  d8test::Check(fn == "BooleanCombineGeneralSealed", "function_name() names the sealed entry point");
  d8test::Check(what.rfind("BooleanCombineGeneralSealed: ", 0) == 0, "what() text keeps its unchanged prefix");
  d8test::Check(reason == BooleanFailureReason::OpenResult, "naked edges remain: reason() is OpenResult");

  // independent count on the attempt, with plain ON_Brep calls
  const ON_Brep& b = attempt.raw();
  ON_BoundingBox box;
  b.GetBoundingBox(box);
  const double diag = box.Diagonal().Length();
  int naked = 0, nonmanifold = 0, slivers = 0;
  double total = 0.0, longest = 0.0;
  for (int i = 0; i < b.m_E.Count(); ++i) {
    const ON_BrepEdge& e = b.m_E[i];
    if (e.m_edge_index < 0) continue;
    if (e.TrimCount() >= 3) ++nonmanifold;
    if (e.TrimCount() != 1) continue;
    ++naked;
    double len = 0.0;
    if (const ON_Curve* c = e.EdgeCurveOf()) {  // 4000 chords: an independent, much finer measure than the report's 32
      const ON_Interval dom = c->Domain();
      ON_3dPoint prev = c->PointAt(dom[0]);
      for (int k = 1; k <= 4000; ++k) {
        const ON_3dPoint q = c->PointAt(dom.ParameterAt(k / 4000.0));
        len += prev.DistanceTo(q);
        prev = q;
      }
    }
    total += len;
    longest = std::max(longest, len);
    if (len < 1e-3 * diag) ++slivers;
  }
  d8test::Check(naked > 0 && rep.naked_edges == naked, ("the report's naked-edge count " + std::to_string(rep.naked_edges) + " equals the independent count " + std::to_string(naked)).c_str());
  d8test::Check(rep.nonmanifold_edges == nonmanifold, "the non-manifold edge count equals the independent count");
  d8test::Check(rep.naked_slivers == slivers, "the sliver count (naked edges shorter than 0.1% of the diagonal) equals the independent count");
  d8test::Check(std::fabs(rep.naked_total_length - total) < 0.01 * total + 1e-9, "the total naked length agrees with a 4000-chord measure to 1% (the report samples 32 chords per edge)");
  d8test::Check(std::fabs(rep.naked_longest - longest) < 0.01 * longest + 1e-9, "the longest naked edge agrees likewise");
  d8test::Check(what.find(std::to_string(naked) + " unpaired edge(s)") != std::string::npos, "the message states the same count");
}

// Every edge has two faces and the orientation still clashes: InconsistentOrientation, checked against ON_Brep's own IsManifold().
// (Box minus a cylinder with both ends flush with the box's faces is a known open gap of the sealer; if it ever seals this check is
// retired and the remaining checks above keep the typed paths covered.)
D8_EXTRA_TEST(TypedSealRefusalNamesAnInconsistentOrientation) {
  const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
  Brep::CylindricalFace cf;
  // The wall's seam is at (5, 3, 0): x axis (0, -1, 0), y axis (1, 0, 0). (With the default frame of ON_Plane(origin, normal) - x along
  // +x, the seam at (7, 5, 0) - the same Difference SEALS: the refusal below depends on where the seam falls, an open defect.)
  cf.frame.origin = ON_3dPoint(5, 5, 0);
  cf.frame.xaxis = ON_3dVector(0, -1, 0);
  cf.frame.yaxis = ON_3dVector(1, 0, 0);
  cf.frame.zaxis = ON_3dVector(0, 0, 1);
  cf.frame.UpdateEquation();
  cf.radius = 2.0;
  cf.angle = 2.0 * ON_PI;
  cf.length = 10.0;
  auto cap = [&](double h, bool flip) {
    Brep::PlanarFace pf;
    const ON_3dPoint c = cf.frame.origin + cf.frame.zaxis * h;
    pf.plane = ON_Plane(c, flip ? -cf.frame.zaxis : cf.frame.zaxis);
    const int n = 128;
    for (int i = 0; i < n; ++i) {
      const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
      pf.loop.push_back(c + cf.frame.xaxis * (cf.radius * std::cos(a)) + cf.frame.yaxis * (cf.radius * std::sin(a)));
    }
    pf.notch_begin = 0;
    pf.notch_count = n;
    return pf;
  };
  const Brep cyl = Brep::FromMixedFaces({cap(0.0, true), cap(10.0, false)}, {cf});
  bool sealed = false, typed = false;
  BooleanFailureReason reason = BooleanFailureReason::Unclassified;
  dino8::kernel::BooleanSealReport rep;
  Brep attempt;
  try {
    BooleanCombineGeneralSealed(box, cyl, BooleanOp::Difference);
    sealed = true;
  } catch (const BooleanSealError& e) {
    typed = true;
    reason = e.reason();
    rep = e.report();
    attempt = e.attempt();
  } catch (...) {
  }
  if (sealed) {
    d8test::Check(true, "box minus a flush-ended cylinder now seals: the orientation-refusal fixture is retired");
    return;
  }
  d8test::Check(typed, "box minus a flush-ended through cylinder: refused with the typed BooleanSealError");
  if (!typed) return;
  d8test::Check(reason == BooleanFailureReason::InconsistentOrientation, "every edge has two faces: reason() is InconsistentOrientation");
  d8test::Check(rep.naked_edges == 0 && rep.nonmanifold_edges == 0, "the report counts no naked and no non-manifold edge");
  int not_two = 0;
  for (int i = 0; i < attempt.raw().m_E.Count(); ++i)
    if (attempt.raw().m_E[i].m_edge_index >= 0 && attempt.raw().m_E[i].TrimCount() != 2) ++not_two;
  d8test::Check(not_two == 0, "oracle: every edge of the attempt really has exactly two trims");
  bool oriented = true, has_boundary = false;
  attempt.raw().IsManifold(&oriented, &has_boundary);
  d8test::Check(!oriented && !rep.oriented_manifold, "oracle: ON_Brep::IsManifold() agrees the attempt is not oriented, and so does the report");
}
