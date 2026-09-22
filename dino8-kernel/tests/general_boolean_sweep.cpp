// Standalone measurement harness (NOT part of the dino8_kernel_tests
// suite): sweeps BooleanCombineGeneral (boolean_general.h) over a fixed
// set of operand configurations and prints, for every case x op, the
// face count, ON_Brep::IsValid(), the tessellated volume at
// TessellateGeneralBooleanClosedMesh(32, 128) (boolean_general.h's own
// closed-manifold-oriented tessellator for this engine's results - NOT
// plain Brep::TessellateToClosedMesh(), which this engine's dense-polyline
// edges were never Mesh::IsClosedManifold() under, at any resolution; see
// boolean_general.h's own doc comment), the expected volume (closed-form
// where one exists, otherwise a labelled reference), the absolute error,
// and a verdict. It fixes nothing about BooleanCombineGeneral's own
// geometry - only measures it - so the next fixes to the general engine
// can be chosen from evidence rather than guesswork; the `closedmesh`
// column, though, measures a real, separate property of the CHOSEN
// tessellator above, not of BooleanCombineGeneral's own topology, and a
// session fixing that tessellator's own bugs (a pre-existing grid-clip
// degenerate-sliver artifact stranding its neighbor's edge when dropped
// after, not before, T-junction stitching - see TessellateGeneralBoolean-
// ClosedMesh's own doc comment) took this column from 0/76 to 14/76.
// The remaining 62 are every case with a curved operand (cylinder, cone,
// sphere, torus) meeting another face along a curved/skew intersection:
// each side's own grid-clip tessellation approximates that shared curve
// with an independently-sampled dense polyline (not shared sample
// points), a materially larger, still-open gap needing genuine curve/
// edge-topology-conforming tessellation (as TessellateToClosedMeshConforming
// already does for BooleanCombineMixed/Planar), not local point-matching.
//
// Every operand is built from the kernel's own primitives: Brep::Box(),
// Brep::Sphere(), Brep::FromPlanarFaces() (a rotated box),
// Brep::FromMixedFaces() (a closed cylinder = CylindricalFace + two disk
// PlanarFace caps welded via notch_begin/notch_count, exactly
// scratch_test.cpp's MakeCylinderZ generalized to an arbitrary axis; a
// closed frustum = ConicalFace + two disk caps the same way), and
// Brep::FromSurface() of ON_Torus::GetNurbForm (a torus - the kernel has
// no Brep torus factory, see the case's own comment).
//
// References. "closed-form" means an exact analytic volume (derivation in
// each case's comment). Where none exists the harness falls back, in
// order, to: BooleanCombinePlanar (exact planar engine, planar-only
// operands), BooleanCombineMixed (the special-cased plane/cylinder
// engine), or a Manifold mesh boolean (BooleanCombine on each operand's
// own high-resolution tessellation - trust bounded by that tessellation's
// own deficit, printed per case as a calibration line). A Manifold
// cross-check column is printed for every case where both operand meshes
// are closed manifolds, regardless of whether a closed form exists.
//
// Verdict rule: EXCEPTION if BooleanCombineGeneral itself threw;
// TESSELLATION-EXCEPTION if the B-rep was built but tessellating it (via
// TessellateGeneralBooleanClosedMesh(), which calls the shared
// Brep::Tessellate() under the hood) threw (each self-intersecting (u, v)
// trim loop is then located and printed - see ReportNonSimpleTrims); INVALID if the
// result is not ON_Brep::IsValid() (each zero-length 2D trim is printed -
// see ReportZeroLengthTrims; an EMPTY result has no topology and is judged
// by volume alone); WRONG-VOLUME if |measured - expected| exceeds 2% of
// the two operands' summed volume (the general engine's own proven cases
// measure ~1.4% at this tessellation on box+cylinder - see
// TestBooleanCombineGeneralBoxCylinder's tolerance note); else OK. The
// `nonsimple` column counts self-intersecting trim loops on EVERY result,
// including ones that tessellated (a face with holes takes the unchecked
// whole-cell path). DINO8_SWEEP_VALIDLOG=1 additionally prints
// ON_Brep::IsValid's own text log for the operands and any invalid result;
// DINO8_BOOL_DEBUG=1 is the engine's own per-face chain/fragment trace.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"

using namespace dino8::kernel;

namespace {

constexpr int kNU = 32, kNV = 128;       // result tessellation, as the task specifies
constexpr int kRefNU = 96, kRefNV = 384;  // operand tessellation feeding the Manifold reference

ON_Plane FrameFromAxis(const Point3d& origin, Vector3d axis) {
  axis.Unitize();
  Vector3d seed = std::fabs(axis.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d x = ON_CrossProduct(seed, axis);
  x.Unitize();
  Vector3d y = ON_CrossProduct(axis, x);
  y.Unitize();
  ON_Plane f;
  f.origin = origin;
  f.xaxis = x;
  f.yaxis = y;
  f.zaxis = axis;
  f.UpdateEquation();
  return f;
}

// A disk cap in `frame`'s xy plane at axial height `h`, radius `r`, sampled
// at exactly the angles the curved face's own rim uses (angle 0 at
// frame.xaxis), marked as one circular-arc notch run so FromMixedFaces()
// welds it to the curved face's cap edge. `flip` reverses the loop and the
// normal (the v=0 end's outward normal is -axis).
Brep::PlanarFace DiskCap(const ON_Plane& frame, double h, double r, bool flip, int n = 128) {
  Brep::PlanarFace pf;
  const Point3d c = frame.origin + frame.zaxis * h;
  pf.plane = ON_Plane(c, flip ? -frame.zaxis : frame.zaxis);
  for (int i = 0; i < n; ++i) {
    const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
    pf.loop.push_back(c + frame.xaxis * (r * std::cos(a)) + frame.yaxis * (r * std::sin(a)));
  }
  pf.notch_begin = 0;
  pf.notch_count = static_cast<int>(pf.loop.size());
  return pf;
}

// Closed finite cylinder: base centre `base`, unit `axis`, radius `r`,
// extending `length` along the axis.
Brep MakeCylinder(const Point3d& base, const Vector3d& axis, double r, double length) {
  Brep::CylindricalFace cf;
  cf.frame = FrameFromAxis(base, axis);
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({DiskCap(cf.frame, 0.0, r, true), DiskCap(cf.frame, length, r, false)}, {cf});
}

// Closed frustum: radius r0 at `base`, r1 at base + length*axis. A
// ConicalFace's frame.origin is the APEX (see brep.h), which sits
// r0/tan(half) behind the base along the axis.
Brep MakeFrustum(const Point3d& base, const Vector3d& axis, double r0, double r1, double length) {
  Brep::ConicalFace cf;
  const double tan_half = (r1 - r0) / length;
  const ON_Plane base_frame = FrameFromAxis(base, axis);
  cf.frame = base_frame;
  cf.frame.origin = base - base_frame.zaxis * (r0 / tan_half);
  cf.frame.UpdateEquation();
  cf.radius0 = r0;
  cf.radius1 = r1;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({DiskCap(base_frame, 0.0, r0, true), DiskCap(base_frame, length, r1, false)}, {},
                              {cf});
}

// Axis-aligned box [lo, hi] transformed by `x`, as six trimmed planar faces.
Brep MakeBoxXform(const Point3d& lo, const Point3d& hi, const ON_Xform& x) {
  auto P = [&](int cx, int cy, int cz) {
    Point3d p(cx ? hi.x : lo.x, cy ? hi.y : lo.y, cz ? hi.z : lo.z);
    return x * p;
  };
  struct F {
    Vector3d n;
    int c[4][3];
  };
  // Each loop CCW as seen from outside (normal pointing out).
  const F faces[6] = {
      {Vector3d(-1, 0, 0), {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
      {Vector3d(1, 0, 0), {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
      {Vector3d(0, -1, 0), {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
      {Vector3d(0, 1, 0), {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
      {Vector3d(0, 0, -1), {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}},
      {Vector3d(0, 0, 1), {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
  };
  std::vector<Brep::PlanarFace> pfs;
  for (const F& f : faces) {
    Brep::PlanarFace pf;
    for (int k = 0; k < 4; ++k) pf.loop.push_back(P(f.c[k][0], f.c[k][1], f.c[k][2]));
    Vector3d n = f.n;
    n.Transform(x);
    n.Unitize();
    pf.plane = ON_Plane(pf.loop[0], n);
    pfs.push_back(pf);
  }
  return Brep::FromPlanarFaces(pfs);
}

// Torus via Brep::FromSurface of ON_Torus::GetNurbForm - the kernel has no
// Brep torus factory (Mesh::Torus is mesh-only). Orientation is checked at
// run time (signed tessellated volume) and the single face is flipped if
// it points inward.
Brep MakeTorus(const Point3d& c, double R, double r) {
  const ON_Torus t(ON_Plane(c, ON_3dVector(0, 0, 1)), R, r);
  ON_NurbsSurface ns;
  if (!t.GetNurbForm(ns)) throw std::runtime_error("ON_Torus::GetNurbForm failed");
  NurbsSurface s;
  s.raw() = ns;
  Brep b = Brep::FromSurface(s);
  if (b.TessellateToClosedMesh(64, 32).Volume() < 0) b.raw().m_F[0].m_bRev = !b.raw().m_F[0].m_bRev;
  return b;
}

// Simpson quadrature on [a, b].
double Simpson(const std::function<double(double)>& f, double a, double b, int n = 4000) {
  const double h = (b - a) / n;
  double s = f(a) + f(b);
  for (int i = 1; i < n; ++i) s += (i % 2 ? 4.0 : 2.0) * f(a + i * h);
  return s * h / 3.0;
}

const char* OpName(BooleanOp op) {
  switch (op) {
    case BooleanOp::Union: return "Union";
    case BooleanOp::Intersection: return "Intersection";
    case BooleanOp::Difference: return "Difference";
    default: return "?";
  }
}

struct Expect {
  double u = NAN, i = NAN, ab = NAN, ba = NAN;  // Union, Intersection, A-B, B-A
  std::string source;                           // how the expectation was derived
};

Expect FromIntersection(double vol_a, double vol_b, double inter, const std::string& how) {
  Expect e;
  e.i = inter;
  e.u = vol_a + vol_b - inter;
  e.ab = vol_a - inter;
  e.ba = vol_b - inter;
  e.source = how;
  return e;
}

// Reference meshes for the Manifold cross-check. A case may supply its own
// (built from the kernel's Mesh primitives - RevolveProfile()/Torus() -
// whose polygonal error at kRefSegs is ~(2 pi / n)^2 / 6 = 1e-4 relative);
// otherwise the operand Brep's own TessellateToClosedMesh(kRefNU, kRefNV)
// is used. Either way the mesh must be a closed manifold or Manifold
// refuses it, in which case the cross-check column reads nan.
constexpr int kRefSegs = 256;
Mesh MeshCyl(const Point3d& base, const Vector3d& axis, double r, double len) {
  return Mesh::RevolveProfile({Point2d(0, 0), Point2d(r, 0), Point2d(r, len), Point2d(0, len)}, base, axis, kRefSegs);
}
Mesh MeshFrustum(const Point3d& base, const Vector3d& axis, double r0, double r1, double len) {
  return Mesh::RevolveProfile({Point2d(0, 0), Point2d(r0, 0), Point2d(r1, len), Point2d(0, len)}, base, axis, kRefSegs);
}
Mesh MeshSphere(const Point3d& c, double R) {
  std::vector<Point2d> prof;
  const int n = kRefSegs / 2;
  for (int i = 0; i <= n; ++i) {
    const double phi = ON_PI * i / n;
    prof.emplace_back(R * std::sin(phi), -R * std::cos(phi));
  }
  return Mesh::RevolveProfile(prof, c, Vector3d(0, 0, 1), kRefSegs);
}
Mesh MeshTorus(const Point3d& c, double R, double r) { return Mesh::Torus(c, Vector3d(0, 0, 1), R, r, kRefSegs, kRefSegs / 2); }
Mesh MeshBox(double x0, double y0, double z0, double x1, double y1, double z1) {
  return Brep::Box(x0, y0, z0, x1, y1, z1).TessellateToClosedMesh(2, 2);
}

struct MeshRef {
  Mesh ma, mb;
  bool ok = false;
  double vol_a = NAN, vol_b = NAN;
  const char* how = "";
};
MeshRef BuildMeshRef(const Brep& a, const Brep& b, const std::function<Mesh()>& ma, const std::function<Mesh()>& mb) {
  MeshRef r;
  r.ma = ma ? ma() : a.TessellateToClosedMesh(kRefNU, kRefNV);
  r.mb = mb ? mb() : b.TessellateToClosedMesh(kRefNU, kRefNV);
  r.how = (ma || mb) ? "Mesh primitives (RevolveProfile/Torus/Box)" : "operand Brep tessellation";
  r.ok = r.ma.IsClosedManifold() && r.mb.IsClosedManifold();
  r.vol_a = r.ma.Volume();
  r.vol_b = r.mb.Volume();
  return r;
}
double MeshRefVolume(const MeshRef& r, const Mesh& a, const Mesh& b, BooleanOp op) {
  if (!r.ok) return NAN;
  try {
    Mesh m = BooleanCombine(a, b, op);
    return m.Volume();
  } catch (...) {
    return NAN;
  }
}

// Diagnostic for a result whose B-rep is built (and possibly even
// IsValid()) but whose tessellation throws "trim_polygon must be simple":
// walks every face's loops as the (u, v) polygon of its trims' own 2D line
// endpoints (BuildLoop in boolean_general.cpp builds one ON_LineCurve trim
// per fragment-boundary segment), finds the first pair of non-adjacent
// segments that cross, and prints which face/loop it is on and where.
bool SegsCross(const ON_2dPoint& a, const ON_2dPoint& b, const ON_2dPoint& c, const ON_2dPoint& d) {
  auto orient = [](const ON_2dPoint& p, const ON_2dPoint& q, const ON_2dPoint& r) {
    return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
  };
  const double o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
  return ((o1 > 0) != (o2 > 0)) && ((o3 > 0) != (o4 > 0)) && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}
int ReportNonSimpleTrims(const Brep& res, bool verbose) {
  int count = 0;
  const ON_Brep& b = res.raw();
  for (int fi = 0; fi < b.m_F.Count(); ++fi) {
    const ON_BrepFace& f = b.m_F[fi];
    const ON_Surface* s = f.SurfaceOf();
    const char* kind = s->IsPlanar() ? "planar" : s->IsCylinder() ? "cylinder" : s->IsCone() ? "cone" : s->IsSphere() ? "sphere" : s->IsTorus() ? "torus" : "other";
    for (int li = 0; li < f.m_li.Count(); ++li) {
      const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
      std::vector<ON_2dPoint> poly;
      int dup = 0;
      for (int ti = 0; ti < loop.m_ti.Count(); ++ti) {
        const ON_BrepTrim& t = b.m_T[loop.m_ti[ti]];
        const ON_Curve* c2 = t.TrimCurveOf();
        if (!c2) continue;
        const ON_2dPoint p = c2->PointAt(t.Domain().Min());
        if (!poly.empty() && poly.back().DistanceTo(p) < 1e-12) ++dup;
        poly.push_back(p);
      }
      const size_t n = poly.size();
      bool reported = false;
      for (size_t i = 0; i < n && !reported; ++i) {
        for (size_t j = i + 2; j < n; ++j) {
          if (i == 0 && j == n - 1) continue;
          if (SegsCross(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n])) {
            ++count;
            if (verbose) std::printf("    NON-SIMPLE trim: face %d (%s, m_bRev=%d) loop %d (%s) n=%zu dup=%d: seg %zu (%.5f,%.5f)-(%.5f,%.5f) crosses seg %zu (%.5f,%.5f)-(%.5f,%.5f)\n",
                        fi, kind, (int)f.m_bRev, li, loop.m_type == ON_BrepLoop::outer ? "outer" : "inner", n, dup, i,
                        poly[i].x, poly[i].y, poly[(i + 1) % n].x, poly[(i + 1) % n].y, j, poly[j].x, poly[j].y,
                        poly[(j + 1) % n].x, poly[(j + 1) % n].y);
            reported = true;
            break;
          }
        }
      }
    }
  }
  return count;
}

// Diagnostic for an INVALID result: every trim whose own 2D curve has
// coincident endpoints (ON_Brep::IsValid's "Line points are coincident"),
// with the (u, v), the edge's two vertex ids and their 3D distance.
void ReportZeroLengthTrims(const Brep& res) {
  const ON_Brep& b = res.raw();
  for (int ti = 0; ti < b.m_T.Count(); ++ti) {
    const ON_BrepTrim& t = b.m_T[ti];
    const ON_Curve* c2 = t.TrimCurveOf();
    if (!c2) continue;
    const ON_2dPoint p0 = c2->PointAt(t.Domain().Min()), p1 = c2->PointAt(t.Domain().Max());
    if (p0.DistanceTo(p1) > 1e-12) continue;
    const ON_BrepFace* f = t.Face();
    const ON_Surface* s = f ? f->SurfaceOf() : nullptr;
    const char* kind = !s ? "?" : s->IsPlanar() ? "planar" : s->IsCylinder() ? "cylinder" : s->IsCone() ? "cone" : s->IsSphere() ? "sphere" : "other";
    if (t.m_ei >= 0) {
      const ON_BrepEdge& e = b.m_E[t.m_ei];
      std::printf("    ZERO-LENGTH 2D trim m_T[%d] on face %d (%s) type=%d: uv=(%.6f,%.6f) edge %d vertices %d,%d 3D-distance %.3e\n",
                  ti, f ? f->m_face_index : -1, kind, (int)t.m_type, p0.x, p0.y, t.m_ei, e.m_vi[0], e.m_vi[1],
                  b.m_V[e.m_vi[0]].point.DistanceTo(b.m_V[e.m_vi[1]].point));
    } else {
      std::printf("    ZERO-LENGTH 2D trim m_T[%d] on face %d (%s) type=%d (singular, no edge): uv=(%.6f,%.6f)\n", ti,
                  f ? f->m_face_index : -1, kind, (int)t.m_type, p0.x, p0.y);
    }
  }
}

struct Case {
  std::string name;
  std::function<Brep()> make_a, make_b;
  std::function<Expect(const Brep&, const Brep&, const MeshRef&)> expect;
  std::function<Mesh()> mesh_a, mesh_b;  // optional reference meshes (see BuildMeshRef)
};

void RunCase(const Case& c) {
  std::printf("\n=== %s ===\n", c.name.c_str());
  Brep a, b;
  try {
    a = c.make_a();
    b = c.make_b();
  } catch (const std::exception& e) {
    std::printf("  CANNOT CONSTRUCT OPERANDS: %s\n", e.what());
    return;
  }
  const MeshRef ref = BuildMeshRef(a, b, c.mesh_a, c.mesh_b);
  const Mesh a32 = a.TessellateToClosedMesh(kNU, kNV), b32 = b.TessellateToClosedMesh(kNU, kNV);
  std::printf("  operands: A faces=%d valid=%d vol@32x128=%.4f | B faces=%d valid=%d vol@32x128=%.4f | manifold-ref "
              "meshes from %s: volA=%.4f volB=%.4f %s\n",
              a.FaceCount(), (int)a.raw().IsValid(), a32.Volume(), b.FaceCount(), (int)b.raw().IsValid(), b32.Volume(),
              ref.how, ref.vol_a, ref.vol_b, ref.ok ? "(available)" : "(UNAVAILABLE: a reference mesh is not a closed manifold)");
  if (std::getenv("DINO8_SWEEP_VALIDLOG")) {
    ON_TextLog log;
    std::printf("  --- ON_Brep::IsValid(log) of operand A:\n");
    a.raw().IsValid(&log);
    std::printf("  --- ON_Brep::IsValid(log) of operand B:\n");
    b.raw().IsValid(&log);
  }
  Expect ex;
  try {
    ex = c.expect(a, b, ref);
  } catch (const std::exception& e) {
    ex.source = std::string("expectation failed: ") + e.what();
  }
  std::printf("  expected-from: %s\n", ex.source.c_str());
  const double vol_sum = std::fabs(ref.vol_a) + std::fabs(ref.vol_b);
  const double tol = 0.02 * vol_sum;

  struct Run {
    const char* label;
    const Brep* x;
    const Brep* y;
    BooleanOp op;
    double expect;
    const Mesh* mx;
    const Mesh* my;
  };
  const Run runs[4] = {
      {"Union       ", &a, &b, BooleanOp::Union, ex.u, &ref.ma, &ref.mb},
      {"Intersection", &a, &b, BooleanOp::Intersection, ex.i, &ref.ma, &ref.mb},
      {"A-B         ", &a, &b, BooleanOp::Difference, ex.ab, &ref.ma, &ref.mb},
      {"B-A         ", &b, &a, BooleanOp::Difference, ex.ba, &ref.mb, &ref.ma},
  };
  for (const Run& r : runs) {
    const double mref = MeshRefVolume(ref, *r.mx, *r.my, r.op);
    Brep res;
    try {
      res = BooleanCombineGeneral(*r.x, *r.y, r.op);
    } catch (const std::exception& e) {
      std::printf("  %s | %-13s | EXCEPTION: %s | expect=%.4f manifold-ref=%.4f\n", c.name.c_str(), r.label, e.what(),
                  r.expect, mref);
      continue;
    }
    const int faces = res.FaceCount();
    double vol = 0.0;
    bool valid = true;  // an EMPTY result has no topology to validate; judged by volume alone
    bool closed = false;
    int nonsimple = 0;  // loops whose (u, v) trim polygon self-intersects
    std::string tess_error;
    if (faces > 0) {
      valid = res.raw().IsValid();
      try {
        const Mesh m = TessellateGeneralBooleanClosedMesh(res, kNU, kNV);
        vol = m.Volume();
        closed = m.IsClosedManifold();
      } catch (const std::exception& e) {
        tess_error = e.what();
        vol = NAN;
      }
      nonsimple = ReportNonSimpleTrims(res, /*verbose=*/!tess_error.empty() || std::getenv("DINO8_SWEEP_VALIDLOG"));
      if (!valid) ReportZeroLengthTrims(res);
      if (!valid && std::getenv("DINO8_SWEEP_VALIDLOG")) {
        ON_TextLog log;
        std::printf("  --- ON_Brep::IsValid(log) of the %s result:\n", r.label);
        res.raw().IsValid(&log);
      }
    }
    const double err = (std::isnan(r.expect) || std::isnan(vol)) ? NAN : std::fabs(vol - r.expect);
    const char* verdict = !tess_error.empty() ? "TESSELLATION-EXCEPTION"
                          : !valid            ? "INVALID"
                          : std::isnan(err)   ? "NO-EXPECTATION"
                          : err <= tol        ? "OK"
                                              : "WRONG-VOLUME";
    std::printf("  %s | %-13s | faces=%3d valid=%d closedmesh=%d nonsimple=%d | vol=%11.4f expect=%11.4f err=%9.4f (tol %.3f) | manifold-ref=%11.4f | %s%s%s%s\n",
                c.name.c_str(), r.label, faces, (int)valid, (int)closed, nonsimple, vol, r.expect, err, tol, mref, verdict,
                faces == 0 ? " (EMPTY result)" : "", tess_error.empty() ? "" : ": ", tess_error.c_str());
  }
}

double SphereVol(double r) { return 4.0 / 3.0 * ON_PI * r * r * r; }
double CylVol(double r, double h) { return ON_PI * r * r * h; }

}  // namespace

// Usage: dino8_general_boolean_sweep [case-name-substring ...]
// With arguments, only cases whose name contains one of them are run (for
// re-running a single case under DINO8_BOOL_DEBUG=1 / DINO8_SWEEP_VALIDLOG=1).
int main(int argc, char** argv) {
  ON::Begin();
  std::vector<Case> cases;

  // 0. proven baseline: two overlapping axis-aligned boxes.
  cases.push_back({"00 box+box axis-aligned (baseline)", [] { return Brep::Box(0, 0, 0, 2, 2, 2); },
                   [] { return Brep::Box(1, 1, 1, 3, 3, 3); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(8, 8, 1, "closed-form: overlap is the unit cube [1,2]^3");
                   },
                   [] { return MeshBox(0, 0, 0, 2, 2, 2); },
                   [] { return MeshBox(1, 1, 1, 3, 3, 3); }});

  // 1. proven baseline: 4x4x2 box fully pierced by a perpendicular r=1 cylinder.
  cases.push_back({"01 box+cyl perpendicular piercing (baseline)", [] { return Brep::Box(-2, -2, -1, 2, 2, 1); },
                   [] { return MakeCylinder(Point3d(0, 0, -2), Vector3d(0, 0, 1), 1.0, 4.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(32, CylVol(1, 4), CylVol(1, 2), "closed-form: pi r^2 * box height");
                   },
                   [] { return MeshBox(-2, -2, -1, 2, 2, 1); },
                   [] { return MeshCyl(Point3d(0, 0, -2), Vector3d(0, 0, 1), 1.0, 4.0); }});

  // 2. blind hole / boss: cylinder z in [0,3] ends inside the box z in [-1,1].
  cases.push_back({"02 box+cyl perpendicular blind (cap inside box)", [] { return Brep::Box(-2, -2, -1, 2, 2, 1); },
                   [] { return MakeCylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 3.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(32, CylVol(1, 3), CylVol(1, 1),
                                             "closed-form: pi r^2 * (embedded length 1 = z in [0,1])");
                   },
                   [] { return MeshBox(-2, -2, -1, 2, 2, 1); },
                   [] { return MeshCyl(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 3.0); }});

  // 3. oblique: axis = Ry(20deg) Rx(30deg) z, through the origin, r=1, length 12
  //    (centred), box [-3,3]^2 x [-1,1]. The cylinder crosses only the top/
  //    bottom faces (its elliptical footprint stays well inside +-3), so
  //    the intersection is an oblique slab of the cylinder: pi r^2 * h / cos(theta),
  //    theta = angle between the axis and z.
  {
    Vector3d d(0, -std::sin(ON_PI / 6), std::cos(ON_PI / 6));  // Rx(30) z
    const double cy = std::cos(ON_PI / 9), sy = std::sin(ON_PI / 9);
    d = Vector3d(d.x * cy + d.z * sy, d.y, -d.x * sy + d.z * cy);  // Ry(20)
    d.Unitize();
    const double cos_theta = d.z;
    cases.push_back({"03 box+cyl OBLIQUE axis (30x,20y) piercing",
                     [] { return Brep::Box(-3, -3, -1, 3, 3, 1); },
                     [d] { return MakeCylinder(Point3d(0, 0, 0) - d * 6.0, d, 1.0, 12.0); },
                     [cos_theta](const Brep&, const Brep&, const MeshRef&) {
                       char buf[160];
                       std::snprintf(buf, sizeof buf, "closed-form: pi r^2 * slab(2) / cos(theta), cos(theta)=%.4f",
                                     cos_theta);
                       return FromIntersection(72, CylVol(1, 12), CylVol(1, 2) / cos_theta, buf);
                     },
                   [] { return MeshBox(-3, -3, -1, 3, 3, 1); },
                   [d] { return MeshCyl(Point3d(0, 0, 0) - d * 6.0, d, 1.0, 12.0); }});
  }

  // 4. grazing tangency: cylinder axis parallel to the box face x=0, at
  //    distance exactly r from it, so the wall touches the face along a line.
  cases.push_back({"04 box+cyl axis parallel to face, tangent (grazing)", [] { return Brep::Box(0, 0, 0, 4, 4, 2); },
                   [] { return MakeCylinder(Point3d(-1, 2, -1), Vector3d(0, 0, 1), 1.0, 4.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(32, CylVol(1, 4), 0.0, "closed-form: tangent contact only, zero overlap");
                   },
                   [] { return MeshBox(0, 0, 0, 4, 4, 2); },
                   [] { return MeshCyl(Point3d(-1, 2, -1), Vector3d(0, 0, 1), 1.0, 4.0); }});

  // 5. cyl+cyl parallel axes, r=1 each, axes 1 apart, axial overlap z in [1,4].
  cases.push_back({"05 cyl+cyl parallel axes overlapping", [] { return MakeCylinder(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 4.0); },
                   [] { return MakeCylinder(Point3d(1, 0, 1), Vector3d(0, 0, 1), 1.0, 4.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     const double r = 1.0, dd = 1.0;
                     const double lens = 2 * r * r * std::acos(dd / (2 * r)) - 0.5 * dd * std::sqrt(4 * r * r - dd * dd);
                     return FromIntersection(CylVol(1, 4), CylVol(1, 4), lens * 3.0,
                                             "closed-form: circle-circle lens area (r=1,d=1) * axial overlap 3");
                   },
                   [] { return MeshCyl(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 4.0); },
                   [] { return MeshCyl(Point3d(1, 0, 1), Vector3d(0, 0, 1), 1.0, 4.0); }});

  // 6. Steinmetz: equal radii, perpendicular intersecting axes.
  cases.push_back({"06 cyl+cyl perpendicular equal radii (Steinmetz)", [] { return MakeCylinder(Point3d(0, 0, -3), Vector3d(0, 0, 1), 1.0, 6.0); },
                   [] { return MakeCylinder(Point3d(-3, 0, 0), Vector3d(1, 0, 0), 1.0, 6.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(CylVol(1, 6), CylVol(1, 6), 16.0 / 3.0, "closed-form: Steinmetz 16 r^3 / 3");
                   },
                   [] { return MeshCyl(Point3d(0, 0, -3), Vector3d(0, 0, 1), 1.0, 6.0); },
                   [] { return MeshCyl(Point3d(-3, 0, 0), Vector3d(1, 0, 0), 1.0, 6.0); }});

  // 7. unequal radii, perpendicular intersecting axes: ra=1 (z), rb=0.5 (x).
  cases.push_back({"07 cyl+cyl perpendicular unequal radii", [] { return MakeCylinder(Point3d(0, 0, -3), Vector3d(0, 0, 1), 1.0, 6.0); },
                   [] { return MakeCylinder(Point3d(-3, 0, 0), Vector3d(1, 0, 0), 0.5, 6.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     const double ra = 1.0, rb = 0.5;
                     const double v = Simpson([&](double y) { return 4 * std::sqrt(ra * ra - y * y) * std::sqrt(rb * rb - y * y); }, -rb, rb);
                     return FromIntersection(CylVol(1, 6), CylVol(0.5, 6), v,
                                             "closed-form 1-D quadrature: int_{-rb}^{rb} 4 sqrt(ra^2-y^2) sqrt(rb^2-y^2) dy");
                   },
                   [] { return MeshCyl(Point3d(0, 0, -3), Vector3d(0, 0, 1), 1.0, 6.0); },
                   [] { return MeshCyl(Point3d(-3, 0, 0), Vector3d(1, 0, 0), 0.5, 6.0); }});

  // 8. skew axes: A axis z (r=1), B axis x offset to y=0.3 (r=0.5); the
  //    axes are perpendicular but do not meet (closest distance 0.3).
  cases.push_back({"08 cyl+cyl SKEW perpendicular axes (offset 0.3)", [] { return MakeCylinder(Point3d(0, 0, -3), Vector3d(0, 0, 1), 1.0, 6.0); },
                   [] { return MakeCylinder(Point3d(-3, 0.3, 0), Vector3d(1, 0, 0), 0.5, 6.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     const double ra = 1.0, rb = 0.5, off = 0.3;
                     const double v = Simpson([&](double y) { return 4 * std::sqrt(ra * ra - y * y) * std::sqrt(rb * rb - (y - off) * (y - off)); }, off - rb, off + rb);
                     return FromIntersection(CylVol(1, 6), CylVol(0.5, 6), v,
                                             "closed-form 1-D quadrature: int 4 sqrt(ra^2-y^2) sqrt(rb^2-(y-0.3)^2) dy over y in [-0.2,0.8]");
                   },
                   [] { return MeshCyl(Point3d(0, 0, -3), Vector3d(0, 0, 1), 1.0, 6.0); },
                   [] { return MeshCyl(Point3d(-3, 0.3, 0), Vector3d(1, 0, 0), 0.5, 6.0); }});

  // 9. sphere centred on a box face centre (x=0 face of [0,10]x[-5,5]^2).
  cases.push_back({"09 sphere+box sphere centred on FACE (half in)", [] { return Brep::Sphere(Point3d(0, 0, 0), 2.0); },
                   [] { return Brep::Box(0, -5, -5, 10, 5, 5); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(SphereVol(2), 1000, SphereVol(2) / 2, "closed-form: half sphere");
                   },
                   [] { return MeshSphere(Point3d(0, 0, 0), 2.0); },
                   [] { return MeshBox(0, -5, -5, 10, 5, 5); }});

  // 10. sphere centred on a box edge midpoint (edge x=0,y=0 of [0,10]^2x[-5,5]).
  cases.push_back({"10 sphere+box sphere centred on EDGE (quarter in)", [] { return Brep::Sphere(Point3d(0, 0, 0), 2.0); },
                   [] { return Brep::Box(0, 0, -5, 10, 10, 5); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(SphereVol(2), 1000, SphereVol(2) / 4, "closed-form: quarter sphere");
                   },
                   [] { return MeshSphere(Point3d(0, 0, 0), 2.0); },
                   [] { return MeshBox(0, 0, -5, 10, 10, 5); }});

  // 11. sphere fully inside the box (no intersection curve).
  cases.push_back({"11 sphere+box sphere fully INSIDE box", [] { return Brep::Sphere(Point3d(0, 0, 0), 2.0); },
                   [] { return Brep::Box(-5, -5, -5, 5, 5, 5); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(SphereVol(2), 1000, SphereVol(2),
                                             "closed-form: containment (U=box, I=sphere, box-sphere=void, sphere-box=empty)");
                   },
                   [] { return MeshSphere(Point3d(0, 0, 0), 2.0); },
                   [] { return MeshBox(-5, -5, -5, 5, 5, 5); }});

  // 12. two overlapping spheres, unequal radii: R=2 at origin, r=1.5 at x=2.
  cases.push_back({"12 sphere+sphere overlapping unequal radii", [] { return Brep::Sphere(Point3d(0, 0, 0), 2.0); },
                   [] { return Brep::Sphere(Point3d(2, 0, 0), 1.5); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     const double R = 2.0, r = 1.5, d = 2.0;
                     const double lens = ON_PI * std::pow(R + r - d, 2) *
                                         (d * d + 2 * d * r - 3 * r * r + 2 * d * R + 6 * r * R - 3 * R * R) / (12 * d);
                     return FromIntersection(SphereVol(R), SphereVol(r), lens, "closed-form: sphere-sphere lens volume");
                   },
                   [] { return MeshSphere(Point3d(0, 0, 0), 2.0); },
                   [] { return MeshSphere(Point3d(2, 0, 0), 1.5); }});

  // 13. sphere pierced by a cylinder along a diameter: R=2, r=1, z in [-4,4].
  cases.push_back({"13 sphere+cyl axis through centre piercing", [] { return Brep::Sphere(Point3d(0, 0, 0), 2.0); },
                   [] { return MakeCylinder(Point3d(0, 0, -4), Vector3d(0, 0, 1), 1.0, 8.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     const double R = 2.0, r = 1.0;
                     const double v = 4.0 / 3.0 * ON_PI * (R * R * R - std::pow(R * R - r * r, 1.5));
                     return FromIntersection(SphereVol(R), CylVol(r, 8), v,
                                             "closed-form: sphere core within radius r of a diameter = 4/3 pi (R^3 - (R^2-r^2)^1.5)");
                   },
                   [] { return MeshSphere(Point3d(0, 0, 0), 2.0); },
                   [] { return MeshCyl(Point3d(0, 0, -4), Vector3d(0, 0, 1), 1.0, 8.0); }});

  // 14. box + box rotated 30 degrees about z (about its own centre), overlapping.
  cases.push_back({"14 box+box second box rotated 30deg about z", [] { return Brep::Box(0, 0, 0, 2, 2, 2); },
                   [] {
                     ON_Xform x;
                     x.Rotation(ON_PI / 6, ON_3dVector(0, 0, 1), ON_3dPoint(2, 2, 2));
                     return MakeBoxXform(Point3d(1, 1, 1), Point3d(3, 3, 3), x);
                   },
                   [](const Brep& a, const Brep& b, const MeshRef&) {
                     const Brep i = BooleanCombinePlanar(a, b, BooleanOp::Intersection);
                     const double vi = i.TessellateToClosedMesh(8, 8).Volume();
                     return FromIntersection(8, 8, vi, "reference: BooleanCombinePlanar (exact planar engine) intersection volume");
                   },
                   [] { return MeshBox(0, 0, 0, 2, 2, 2); },
                   [] {
                     ON_Xform x;
                     x.Rotation(ON_PI / 6, ON_3dVector(0, 0, 1), ON_3dPoint(2, 2, 2));
                     return MeshBox(1, 1, 1, 3, 3, 3).Transform(x);
                   }});

  // 15. box + cone (frustum r0=0.5 at z=0 to r1=1.5 at z=3, axis z), box slab z in [1,2].
  cases.push_back({"15 box+cone (frustum) perpendicular piercing", [] { return Brep::Box(-3, -3, 1, 3, 3, 2); },
                   [] { return MakeFrustum(Point3d(0, 0, 0), Vector3d(0, 0, 1), 0.5, 1.5, 3.0); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     const double k = 1.0 / 3.0;  // dr/dz
                     const double r1 = 0.5 + k * 1.0, r2 = 0.5 + k * 2.0;
                     const double vi = ON_PI * (r2 * r2 * r2 - r1 * r1 * r1) / (3 * k);
                     const double vf = ON_PI * 3.0 * (0.25 + 0.75 + 2.25) / 3.0;
                     return FromIntersection(36, vf, vi, "closed-form: pi int_1^2 r(z)^2 dz, r(z)=0.5+z/3");
                   },
                   [] { return MeshBox(-3, -3, 1, 3, 3, 2); },
                   [] { return MeshFrustum(Point3d(0, 0, 0), Vector3d(0, 0, 1), 0.5, 1.5, 3.0); }});

  // 16. torus (R=3, r=1, axis z) half inside a box (x >= 0 half-space box).
  cases.push_back({"16 torus+box half torus in box", [] { return MakeTorus(Point3d(0, 0, 0), 3.0, 1.0); },
                   [] { return Brep::Box(0, -10, -10, 10, 10, 10); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     const double vt = 2 * ON_PI * ON_PI * 3.0 * 1.0;
                     return FromIntersection(vt, 4000, vt / 2, "closed-form: half of 2 pi^2 R r^2");
                   },
                   [] { return MeshTorus(Point3d(0, 0, 0), 3.0, 1.0); },
                   [] { return MeshBox(0, -10, -10, 10, 10, 10); }});

  // 17. coplanar shared face: two unit-ish boxes touching along x=2.
  cases.push_back({"17 box+box touching along a coplanar FACE", [] { return Brep::Box(0, 0, 0, 2, 2, 2); },
                   [] { return Brep::Box(2, 0, 0, 4, 2, 2); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(8, 8, 0.0, "closed-form: face contact only, zero overlap");
                   },
                   [] { return MeshBox(0, 0, 0, 2, 2, 2); },
                   [] { return MeshBox(2, 0, 0, 4, 2, 2); }});

  // 18. disjoint boxes.
  cases.push_back({"18 box+box DISJOINT", [] { return Brep::Box(0, 0, 0, 2, 2, 2); },
                   [] { return Brep::Box(3, 3, 3, 5, 5, 5); },
                   [](const Brep&, const Brep&, const MeshRef&) {
                     return FromIntersection(8, 8, 0.0, "closed-form: no contact");
                   },
                   [] { return MeshBox(0, 0, 0, 2, 2, 2); },
                   [] { return MeshBox(3, 3, 3, 5, 5, 5); }});

  size_t ran = 0;
  for (const Case& c : cases) {
    bool selected = argc < 2;
    for (int i = 1; i < argc && !selected; ++i) selected = c.name.find(argv[i]) != std::string::npos;
    if (!selected) continue;
    RunCase(c);
    ++ran;
  }
  std::printf("\nDone: %zu cases x 4 ops.\n", ran);
  return 0;
}
