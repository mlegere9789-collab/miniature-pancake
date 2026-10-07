// Property-based / randomized geometry fuzz test.
//
// Unlike test_brep_mesher.cpp (fixed known-good shapes), this generates
// random-but-valid NURBS curves/surfaces, random primitive-mesh pairs for
// boolean operations, and random surface/surface intersection queries (the
// core primitive the fillet family in cmd_fillet.cpp is built on), and
// checks invariants that must hold no matter what the specific input was:
//
//   - no crash (signal) and no hang (SIGALRM-based per-case timeout)
//   - no NaN/Inf coordinates anywhere in the output
//   - a mesh a solid-producing operation claims closed must be manifold
//   - volumes/areas stay finite, and a primitive's own volume stays
//     positive (consistent outward-normal convention this kernel documents
//     everywhere)
//
// Uses a fixed-seed PRNG (overridable via argv[1] or DINO8_FUZZ_SEED) so
// any failure is exactly reproducible: on failure the seed and the failing
// case's category/index are printed before aborting.
//
// Windows near/far macro hazard: deliberately no variable or parameter is
// named `near` or `far` anywhere in this file.
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <random>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>  // alarm() - POSIX only, see the RunCase Windows fallback below.
#endif

#include <opennurbs.h>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/boolean.h"
#include "geom/BrepMesher.h"
#include "geom/SurfaceIntersect.h"

using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using dino8::kernel::BooleanOp;
using dino8::app::BrepMeshOptions;
using dino8::app::MeshBrepClosed;
using dino8::app::IntersectSurfaces;
using dino8::app::IntersectOptions;

namespace {

// ---------------------------------------------------------------------------
// Failure bookkeeping: prints a reproducing seed and aborts. Category/index
// are kept in globals (not passed through signal handlers) so the
// SIGALRM/SIGSEGV/SIGFPE handlers can report them without unwinding.
// ---------------------------------------------------------------------------
uint64_t g_seed = 0;
volatile sig_atomic_t g_case_index = -1;
const char* g_case_desc = "";
int g_failures = 0;
int g_checks = 0;
std::jmp_buf g_timeout_jmp;
volatile sig_atomic_t g_in_timed_case = 0;

void ReportAndAbort(const char* why) {
  std::fprintf(stderr,
               "\nFUZZ FAILURE: %s\n  case #%d (%s)\n  reproduce with: "
               "DINO8_FUZZ_SEED=%llu ./dino8_test_fuzz_geometry\n",
               why, static_cast<int>(g_case_index), g_case_desc,
               static_cast<unsigned long long>(g_seed));
  std::fflush(stderr);
  std::_Exit(1);
}

extern "C" void OnSignal(int sig) {
#ifndef _WIN32
  if (sig == SIGALRM && g_in_timed_case) {
    // Longjmp back into the driving loop rather than aborting outright:
    // a timeout is the invariant violation itself ("no hangs"), and we
    // still want the seed/case report below to fire exactly once.
    std::longjmp(g_timeout_jmp, 1);
  }
#endif
  const char* name = sig == SIGSEGV ? "SIGSEGV (crash)"
                     : sig == SIGABRT ? "SIGABRT (assert/abort)"
                     : sig == SIGFPE  ? "SIGFPE (floating-point trap)"
                     : sig == SIGILL  ? "SIGILL"
                                      : "signal";
  ReportAndAbort(name);
}

void Check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    std::fprintf(stderr, "invariant violated: %s\n", what);
    ReportAndAbort(what);
  }
}

bool Finite(double v) { return std::isfinite(v); }
bool Finite(const Point3d& p) { return Finite(p.x) && Finite(p.y) && Finite(p.z); }
bool Finite(const Vector3d& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }
bool Finite(const ON_2dPoint& p) { return Finite(p.x) && Finite(p.y); }

// Runs `fn` under a per-case wall-clock timeout of `seconds`. Returns false
// (without invoking fn's own exceptions) if it timed out; otherwise runs fn
// and returns true. Any std::exception thrown by fn is treated as an
// *expected* rejection of bad input (Result::Failed / throw std::invalid_
// argument / std::runtime_error are this kernel's normal "no" answers) and
// is swallowed here - only crashes, hangs, and NaN/manifold violations are
// real fuzz findings.
template <typename Fn>
void RunCase(int index, const char* desc, unsigned seconds, Fn&& fn) {
  g_case_index = index;
  g_case_desc = desc;
#ifndef _WIN32
  if (setjmp(g_timeout_jmp) != 0) {
    alarm(0);
    g_in_timed_case = 0;
    ReportAndAbort("timeout (possible hang)");
    return;
  }
  g_in_timed_case = 1;
  alarm(seconds);
#else
  // Windows has no alarm()/SIGALRM: this fuzz run still gets crash detection
  // (SIGSEGV/SIGABRT/SIGFPE/SIGILL, handled below) on every platform, but a
  // genuine infinite hang on Windows would stall this binary/CI job instead
  // of being caught and reported as a finding - a known, accepted gap, not
  // silently pretended away. See fuzz_qa_notes.md.
  (void)seconds;
#endif
  try {
    fn();
  } catch (const std::exception&) {
    // Expected rejection of degenerate/invalid random input.
  } catch (...) {
#ifndef _WIN32
    alarm(0);
    g_in_timed_case = 0;
#endif
    ReportAndAbort("unknown (non-std::exception) C++ exception");
  }
#ifndef _WIN32
  alarm(0);
  g_in_timed_case = 0;
#endif
}

// ---------------------------------------------------------------------------
// Random generators for "valid-shaped but arbitrary" geometry.
// ---------------------------------------------------------------------------
struct Rng {
  std::mt19937_64 eng;
  explicit Rng(uint64_t seed) : eng(seed) {}
  double Uniform(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(eng); }
  int UniformInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(eng); }
  bool Bool(double p_true = 0.5) { return std::uniform_real_distribution<double>(0, 1)(eng) < p_true; }
};

std::vector<Point3d> RandomControlPoints(Rng& rng, int count, double extent) {
  std::vector<Point3d> pts;
  pts.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    pts.push_back(Point3d(rng.Uniform(-extent, extent), rng.Uniform(-extent, extent), rng.Uniform(-extent, extent)));
  }
  // Occasionally duplicate a point onto its neighbor (a real, adversarial
  // case: a degenerate zero-length control polygon segment) rather than
  // only ever generating well-separated points.
  if (count > 1 && rng.Bool(0.25)) {
    const int i = rng.UniformInt(0, count - 2);
    pts[static_cast<size_t>(i + 1)] = pts[static_cast<size_t>(i)];
  }
  return pts;
}

NurbsCurve RandomCurve(Rng& rng, int* out_degree = nullptr, int* out_cv_count = nullptr) {
  const int degree = rng.UniformInt(1, 5);
  const int cv_count = degree + 1 + rng.UniformInt(0, 6);
  if (out_degree) *out_degree = degree;
  if (out_cv_count) *out_cv_count = cv_count;
  NurbsCurve c = NurbsCurve::FromControlPoints(RandomControlPoints(rng, cv_count, 25.0), degree);
  // Randomly promote to a genuine rational curve with varied weights - the
  // more adversarial case NurbsCurve's own doc comments flag (SetWeightAt
  // moves the control point's own Euclidean position, not just influence).
  if (rng.Bool(0.4)) {
    const int n = c.ControlPointCount();
    for (int i = 0; i < n; ++i) {
      // Occasionally push a weight to an extreme (near-zero or large) -
      // the adversarial tail of the weight range, not just "reasonable"
      // rational curves.
      if (rng.Bool(0.5)) c.SetWeightAt(i, rng.Bool(0.15) ? rng.Uniform(1e-4, 1e-2) : rng.Uniform(0.05, 8.0));
    }
  }
  return c;
}

NurbsSurface RandomSurface(Rng& rng) {
  const int du = rng.UniformInt(1, 4);
  const int dv = rng.UniformInt(1, 4);
  const int nu = du + 1 + rng.UniformInt(0, 4);
  const int nv = dv + 1 + rng.UniformInt(0, 4);
  NurbsSurface s = NurbsSurface::FromControlGrid(RandomControlPoints(rng, nu * nv, 20.0), nu, nv, du, dv);
  if (rng.Bool(0.3)) {
    for (int i = 0; i < s.CVCountU(); ++i)
      for (int j = 0; j < s.CVCountV(); ++j)
        if (rng.Bool(0.3)) s.SetWeightAt(i, j, rng.Uniform(0.1, 5.0));
  }
  return s;
}

// One of a handful of real closed-solid primitives, at randomized
// parameters, meshed the same way test_brep_mesher.cpp does (via ON_Brep*
// factories + MeshBrepClosed) so the fuzz stays anchored to shapes this
// kernel is actually meant to close/mesh correctly, rather than drowning
// in "of course an arbitrary random NURBS blob doesn't mesh to a solid"
// noise.
Mesh RandomPrimitiveMesh(Rng& rng, std::string* desc) {
  const Point3d center(rng.Uniform(-10, 10), rng.Uniform(-10, 10), rng.Uniform(-10, 10));
  // Mostly moderate radii, occasionally an extreme (tiny/large) one - the
  // scale mismatch case real CAD kernels are notorious for mishandling.
  const double r = rng.Bool(0.1) ? rng.Uniform(0.01, 0.2) : (rng.Bool(0.1) ? rng.Uniform(30.0, 200.0) : rng.Uniform(0.5, 6.0));
  BrepMeshOptions opt;
  opt.chord_tolerance = rng.Uniform(0.01, 0.2);
  const int kind = rng.UniformInt(0, 4);
  ON_Brep* brep = nullptr;
  switch (kind) {
    case 0: {
      brep = ON_BrepSphere(ON_Sphere(center, r));
      if (desc) *desc = "sphere";
      break;
    }
    case 1: {
      const Vector3d axis(rng.Uniform(-1, 1), rng.Uniform(-1, 1), rng.Uniform(0.1, 1));
      brep = ON_BrepCylinder(ON_Cylinder(ON_Circle(ON_Plane(center, axis), r), rng.Uniform(1.0, 8.0)), true, true);
      if (desc) *desc = "cylinder";
      break;
    }
    case 2: {
      ON_Cone cone;
      cone.Create(ON_Plane(center, Vector3d(0, 0, 1)), rng.Uniform(1.0, 8.0), r);
      brep = ON_BrepCone(cone, true);
      if (desc) *desc = "cone";
      break;
    }
    case 3: {
      brep = ON_BrepTorus(ON_Torus(ON_Plane(center, Vector3d(0, 0, 1)), r + rng.Uniform(2.0, 5.0), r));
      if (desc) *desc = "torus";
      break;
    }
    default: {
      const double hx = rng.Uniform(1.0, 6.0), hy = rng.Uniform(1.0, 6.0), hz = rng.Uniform(1.0, 6.0);
      Point3d corners[8] = {
          center, center + Vector3d(hx, 0, 0), center + Vector3d(hx, hy, 0), center + Vector3d(0, hy, 0),
          center + Vector3d(0, 0, hz), center + Vector3d(hx, 0, hz), center + Vector3d(hx, hy, hz), center + Vector3d(0, hy, hz)};
      brep = ON_BrepBox(corners);
      if (desc) *desc = "box";
      break;
    }
  }
  Mesh m = MeshBrepClosed(*brep, opt);
  delete brep;
  // Randomly transform the primitive (translate/rotate/scale-free) so
  // boolean pairs overlap or miss in varied, seed-driven ways.
  ON_Xform x = ON_Xform::IdentityTransformation;
  ON_Xform rot;
  rot.Rotation(rng.Uniform(0, 2 * ON_PI), Vector3d(rng.Uniform(-1, 1), rng.Uniform(-1, 1), rng.Uniform(-1, 1)), ON_3dPoint::Origin);
  ON_Xform trans = ON_Xform::TranslationTransformation(Vector3d(rng.Uniform(-6, 6), rng.Uniform(-6, 6), rng.Uniform(-6, 6)));
  x = trans * rot;
  return m.Transform(x);
}

// ---------------------------------------------------------------------------
// Per-mesh / per-curve / per-surface invariant checks.
// ---------------------------------------------------------------------------
void CheckMeshFinite(const Mesh& m, const char* label) {
  // Round-trip through a bounding box + centroid/volume query touches every
  // vertex; a NaN/Inf vertex shows up as a non-finite bbox or (for a closed
  // mesh) a non-finite volume. Also directly walk vertices via the OBJ
  // writer's own data path is overkill; GetBoundingBox is the public,
  // cheap, whole-mesh finite-check surface this class exposes.
  auto bb = m.GetBoundingBox();
  char buf[192];
  std::snprintf(buf, sizeof(buf), "%s bounding box min finite", label);
  Check(Finite(bb.min), buf);
  std::snprintf(buf, sizeof(buf), "%s bounding box max finite", label);
  Check(Finite(bb.max), buf);
  if (m.IsClosedManifold() && m.FaceCount() > 0) {
    const double vol = m.Volume();
    std::snprintf(buf, sizeof(buf), "%s closed-manifold volume finite", label);
    Check(Finite(vol), buf);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGSEGV, OnSignal);
  std::signal(SIGABRT, OnSignal);
  std::signal(SIGFPE, OnSignal);
  std::signal(SIGILL, OnSignal);
#ifndef _WIN32
  std::signal(SIGALRM, OnSignal);
#endif

  uint64_t seed = 0xD1D0u * 0x9A5Bu + 8ull;  // fixed default seed
  if (argc > 1) seed = std::strtoull(argv[1], nullptr, 10);
  if (const char* env = std::getenv("DINO8_FUZZ_SEED")) seed = std::strtoull(env, nullptr, 10);
  g_seed = seed;
  Rng rng(seed);

  int curve_iters = 500;
  int surface_iters = 300;
  int boolean_iters = 300;
  int ssx_iters = 200;
  if (const char* env = std::getenv("DINO8_FUZZ_ITERS")) {
    const int n = std::atoi(env);
    if (n > 0) { curve_iters = n; surface_iters = n * 3 / 5; boolean_iters = n * 3 / 5; ssx_iters = n * 2 / 5; }
  }

  std::printf("dino8_test_fuzz_geometry: seed=%llu curve=%d surface=%d boolean=%d ssx=%d\n",
              static_cast<unsigned long long>(seed), curve_iters, surface_iters, boolean_iters, ssx_iters);

  // --- 1. Random NURBS curves --------------------------------------------
  for (int i = 0; i < curve_iters; ++i) {
    RunCase(i, "random curve", 5, [&] {
      int degree = 0, cv_count = 0;
      NurbsCurve c = RandomCurve(rng, &degree, &cv_count);
      const auto dom = c.Domain();
      Check(Finite(dom.min) && Finite(dom.max) && dom.min <= dom.max, "curve domain finite and ordered");
      const int samples = 12;
      for (int s = 0; s <= samples; ++s) {
        const double t = dom.min + (dom.max - dom.min) * (static_cast<double>(s) / samples);
        Check(Finite(c.PointAt(t)), "curve PointAt finite");
        Check(Finite(c.TangentAt(t)), "curve TangentAt finite");
        Check(Finite(c.CurvatureAt(t)), "curve CurvatureAt finite");
      }
      const double len = c.Length(200);
      Check(Finite(len) && len >= 0.0, "curve Length finite and non-negative");
      const Point3d q(rng.Uniform(-30, 30), rng.Uniform(-30, 30), rng.Uniform(-30, 30));
      Check(Finite(c.ClosestPoint(q, 64)), "curve ClosestPoint finite");

      // Knot insertion at a random interior parameter, if the domain has
      // a non-degenerate interior.
      if (dom.max - dom.min > 1e-9) {
        const double kv = rng.Uniform(dom.min + 1e-6 * (dom.max - dom.min), dom.max - 1e-6 * (dom.max - dom.min));
        const int mult = rng.UniformInt(1, std::max(1, degree));
        NurbsCurve c2 = c;
        c2.InsertKnotAt(kv, mult);
        Check(Finite(c2.PointAt(c2.Domain().min)) && Finite(c2.PointAt(c2.Domain().max)), "curve post-InsertKnotAt endpoints finite");
      }

      // Degree elevation preserves shape; just check it stays finite.
      NurbsCurve c3 = c;
      c3.ElevateDegree(degree + 1 + rng.UniformInt(0, 2));
      Check(Finite(c3.PointAt(c3.Domain().min)), "curve post-ElevateDegree finite");

      // Rational round-trip.
      NurbsCurve c4 = c;
      c4.MakeRational();
      c4.MakeNonRational();
      Check(Finite(c4.PointAt(c4.Domain().min)), "curve post-rational-round-trip finite");

      // Reverse, Trim, Split.
      NurbsCurve c5 = c;
      c5.Reverse();
      Check(Finite(c5.PointAt(c5.Domain().min)), "curve post-Reverse finite");
      if (dom.max - dom.min > 1e-6) {
        const double ta = dom.min + (dom.max - dom.min) * 0.25;
        const double tb = dom.min + (dom.max - dom.min) * 0.75;
        NurbsCurve c6 = c;
        c6.Trim(ta, tb);
        Check(Finite(c6.PointAt(c6.Domain().min)) && Finite(c6.PointAt(c6.Domain().max)), "curve post-Trim finite");
        NurbsCurve left, right;
        const double tsplit = dom.min + (dom.max - dom.min) * rng.Uniform(0.1, 0.9);
        c.Split(tsplit, left, right);
        Check(Finite(left.PointAt(left.Domain().min)) && Finite(right.PointAt(right.Domain().max)), "curve post-Split finite");
      }
    });
  }

  // --- 2. Random NURBS surfaces -------------------------------------------
  for (int i = 0; i < surface_iters; ++i) {
    RunCase(curve_iters + i, "random surface", 5, [&] {
      NurbsSurface s = RandomSurface(rng);
      const auto du = s.Domain(0);
      const auto dv = s.Domain(1);
      Check(Finite(du.min) && Finite(du.max) && Finite(dv.min) && Finite(dv.max), "surface domain finite");
      const int grid = 6;
      for (int a = 0; a <= grid; ++a) {
        for (int b = 0; b <= grid; ++b) {
          const double u = du.min + (du.max - du.min) * (static_cast<double>(a) / grid);
          const double v = dv.min + (dv.max - dv.min) * (static_cast<double>(b) / grid);
          Check(Finite(s.PointAt(u, v)), "surface PointAt finite");
          const Vector3d n = s.NormalAt(u, v);
          Check(Finite(n), "surface NormalAt finite");
        }
      }
      const Point3d q(rng.Uniform(-30, 30), rng.Uniform(-30, 30), rng.Uniform(-30, 30));
      Check(Finite(s.ClosestPoint(q)), "surface ClosestPoint finite");
    });
  }

  // --- 3. Random primitive-mesh pairs + boolean ops -----------------------
  const BooleanOp ops[] = {BooleanOp::Union, BooleanOp::Intersection, BooleanOp::Difference, BooleanOp::SymmetricDifference};
  const char* op_names[] = {"Union", "Intersection", "Difference", "SymmetricDifference"};
  for (int i = 0; i < boolean_iters; ++i) {
    RunCase(curve_iters + surface_iters + i, "random boolean", 10, [&] {
      std::string desc_a, desc_b;
      Mesh a = RandomPrimitiveMesh(rng, &desc_a);
      // Adversarial case: identical geometry (coincident, exactly
      // overlapping boundaries) rather than always two independently
      // random shapes - a classic degenerate boolean input.
      Mesh b = rng.Bool(0.15) ? a : RandomPrimitiveMesh(rng, &desc_b);
      // Every primitive is built with this kernel's own documented
      // outward-normal convention, so a closed, correctly-oriented
      // primitive's own volume must be positive - a real, checkable
      // invariant independent of the (seed-driven) shape/size/pose.
      if (a.IsClosedManifold() && a.FaceCount() > 0) Check(a.Volume() > 0.0, "primitive A volume positive");
      if (b.IsClosedManifold() && b.FaceCount() > 0) Check(b.Volume() > 0.0, "primitive B volume positive");
      CheckMeshFinite(a, "primitive A");
      CheckMeshFinite(b, "primitive B");

      const int op_idx = rng.UniformInt(0, 3);
      Mesh result = dino8::kernel::BooleanCombine(a, b, ops[op_idx]);
      char label[64];
      std::snprintf(label, sizeof(label), "boolean(%s) result", op_names[op_idx]);
      CheckMeshFinite(result, label);
      // BooleanCombine's own contract (boolean.h) is that it either
      // throws or returns a valid closed/watertight mesh - never a
      // non-manifold "solid".
      if (result.FaceCount() > 0) {
        char m[96];
        std::snprintf(m, sizeof(m), "boolean(%s) result is closed manifold", op_names[op_idx]);
        Check(result.IsClosedManifold(), m);
        std::snprintf(m, sizeof(m), "boolean(%s) result volume finite", op_names[op_idx]);
        Check(Finite(result.Volume()), m);
      }
    });
  }

  // --- 4. Random surface/surface intersection (the core fillet primitive) -
  for (int i = 0; i < ssx_iters; ++i) {
    RunCase(curve_iters + surface_iters + boolean_iters + i, "random SSX", 8, [&] {
      NurbsSurface sa = RandomSurface(rng);
      NurbsSurface sb = RandomSurface(rng);
      // Nudge B near A so intersections (the interesting, exercising case)
      // are common rather than the trivially-empty "shapes never touch"
      // case dominating every iteration.
      for (int i2 = 0; i2 < sb.CVCountU(); ++i2)
        for (int j2 = 0; j2 < sb.CVCountV(); ++j2) {
          const Point3d p = sb.ControlPointAt(i2, j2);
          sb.SetControlPointAt(i2, j2, p + Vector3d(rng.Uniform(-8, 8), rng.Uniform(-8, 8), rng.Uniform(-8, 8)));
        }
      IntersectOptions opt;
      opt.tolerance = rng.Uniform(1e-4, 1e-2);
      opt.mesh_tolerance = rng.Uniform(0.01, 0.2);
      opt.max_mesh_divisions = rng.UniformInt(8, 40);
      opt.min_mesh_divisions = 4;
      auto curves = IntersectSurfaces(sa.raw(), sb.raw(), opt);
      for (const auto& ic : curves) {
        for (const auto& p : ic.points) Check(Finite(p), "SSX curve point finite");
        for (const auto& uv : ic.uv_a) Check(Finite(uv), "SSX curve uv_a finite");
        for (const auto& uv : ic.uv_b) Check(Finite(uv), "SSX curve uv_b finite");
        Check(Finite(ic.max_error) && ic.max_error >= 0.0, "SSX curve max_error finite and non-negative");
      }
    });
  }

  std::printf("dino8_test_fuzz_geometry: %d checks, %d failures (seed=%llu)\n", g_checks, g_failures,
              static_cast<unsigned long long>(seed));
  std::printf("%s\n", g_failures == 0 ? "ALL PASSED" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}
