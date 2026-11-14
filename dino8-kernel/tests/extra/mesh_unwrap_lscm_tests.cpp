// ComputeLscmUVs (mesh_unwrap.h): the least-squares conformal unwrap that Mesh's fixed projections (box / planar / cylindrical /
// spherical) are not.
//
// Every oracle below is computed from the mesh's own 3D geometry or from a closed form, never from the unwrap's internals:
//   * similarity residual: the largest relative disagreement between every mesh edge's 3D length and its UV length after
//     fitting ONE global scale. Zero exactly when the UVs are an isometry of the mesh up to a similarity - which is what a
//     developable mesh (flat patch, cylinder strip, cone band) must unroll to;
//   * closed form: an r = 2, h = 3 cylinder strip of N chord facets unrolls to a rectangle N * 2 r sin(pi a / (N * 360)) wide
//     (a = arc in degrees) and h tall, in the ratio the generators fix;
//   * angle distortion: |3D angle - UV angle| over every triangle corner of a hemisphere cap (not developable, so the
//     distortion cannot be zero; what is checked is that it is several times smaller than any projection's).
// The existing mappings are run through the SAME oracles as negative controls: they fail them by orders of magnitude, so the
// oracle does discriminate (these were the failing-first measurements before ComputeLscmUVs existed: planar 18.5, box 13.3,
// cylindrical 4.9 similarity residual on the cylinder strip, against LSCM's 1.9e-13).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/mesh_unwrap.h"
#include "test_registry.h"

namespace {

using dino8::kernel::ComputeLscmUVs;
using dino8::kernel::LscmReport;
using dino8::kernel::Mesh;
using dino8::kernel::Point2d;

ON_3dPoint V(const Mesh& m, int i) { return ON_3dPoint(m.raw().m_V[i]); }

std::string Sci(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.3e", v);
  return buf;
}

void AddTri(Mesh& m, int a, int b, int c) {
  ON_MeshFace f;
  f.vi[0] = a;
  f.vi[1] = b;
  f.vi[2] = c;
  f.vi[3] = c;
  m.raw().m_F.Append(f);
}
void AddQuad(Mesh& m, int a, int b, int c, int d) {
  ON_MeshFace f;
  f.vi[0] = a;
  f.vi[1] = b;
  f.vi[2] = c;
  f.vi[3] = d;
  m.raw().m_F.Append(f);
}

// Largest relative disagreement between 3D and UV edge lengths after one global least-squares scale; also reports the scale.
double SimilarityResidual(const Mesh& m, const std::vector<Point2d>& uv, double* scale = nullptr) {
  const ON_Mesh& r = m.raw();
  std::vector<std::pair<double, double>> e;
  double num = 0.0, den = 0.0;
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) {
      const int a = f.vi[k], b = f.vi[(k + 1) % n];
      const double l3 = V(m, a).DistanceTo(V(m, b));
      const double lu = std::hypot(uv[static_cast<size_t>(a)].x - uv[static_cast<size_t>(b)].x, uv[static_cast<size_t>(a)].y - uv[static_cast<size_t>(b)].y);
      e.push_back({l3, lu});
      num += l3 * lu;
      den += l3 * l3;
    }
  }
  const double s = num / den;
  double worst = 0.0;
  for (const auto& p : e) worst = std::max(worst, std::fabs(s * p.first - p.second) / (s * p.first));
  if (scale) *scale = s;
  return worst;
}

// Largest and mean |3D angle - UV angle| (degrees) over every corner of every triangle (a quad counts as its two triangles).
void AngleError(const Mesh& m, const std::vector<Point2d>& uv, double& worst, double& mean) {
  const ON_Mesh& r = m.raw();
  double sum = 0.0;
  int cnt = 0;
  worst = 0.0;
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    const int idx[2][3] = {{f.vi[0], f.vi[1], f.vi[2]}, {f.vi[0], f.vi[2], f.vi[3]}};
    for (int t = 0; t < (f.IsQuad() ? 2 : 1); ++t) {
      for (int c = 0; c < 3; ++c) {
        const int a = idx[t][c], b = idx[t][(c + 1) % 3], d = idx[t][(c + 2) % 3];
        const ON_3dVector u3 = V(m, b) - V(m, a), v3 = V(m, d) - V(m, a);
        const double a3 = std::acos(std::clamp((u3 * v3) / (u3.Length() * v3.Length()), -1.0, 1.0));
        const double ux = uv[static_cast<size_t>(b)].x - uv[static_cast<size_t>(a)].x, uy = uv[static_cast<size_t>(b)].y - uv[static_cast<size_t>(a)].y;
        const double vx = uv[static_cast<size_t>(d)].x - uv[static_cast<size_t>(a)].x, vy = uv[static_cast<size_t>(d)].y - uv[static_cast<size_t>(a)].y;
        const double a2 = std::acos(std::clamp((ux * vx + uy * vy) / (std::hypot(ux, uy) * std::hypot(vx, vy)), -1.0, 1.0));
        const double e = std::fabs(a3 - a2) * 180.0 / ON_PI;
        worst = std::max(worst, e);
        sum += e;
        ++cnt;
      }
    }
  }
  mean = sum / cnt;
}

// Number of UV triangles wound against the mesh (a fold-over): LSCM keeps the mesh's own orientation.
int FlippedTriangles(const Mesh& m, const std::vector<Point2d>& uv) {
  int flipped = 0;
  const ON_Mesh& r = m.raw();
  for (int i = 0; i < r.m_F.Count(); ++i) {
    const ON_MeshFace& f = r.m_F[i];
    const int idx[2][3] = {{f.vi[0], f.vi[1], f.vi[2]}, {f.vi[0], f.vi[2], f.vi[3]}};
    for (int t = 0; t < (f.IsQuad() ? 2 : 1); ++t) {
      const Point2d &a = uv[static_cast<size_t>(idx[t][0])], &b = uv[static_cast<size_t>(idx[t][1])], &c = uv[static_cast<size_t>(idx[t][2])];
      if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0.0) ++flipped;
    }
  }
  return flipped;
}

// A (n+1) x (n+1) grid of vertices on the plane through `origin` spanned by `ex`, `ey`, every interior vertex displaced by a
// deterministic jitter (multiples of 1/64, so every coordinate is exact in float) and the cells split along alternating
// diagonals - an irregular triangulation, not a lattice.
Mesh IrregularPatch(int n, const ON_3dVector& ex, const ON_3dVector& ey, const ON_3dPoint& origin) {
  Mesh m;
  unsigned s = 12345u;
  auto jit = [&s]() {
    s = s * 1103515245u + 12345u;
    return (static_cast<int>((s >> 16) % 25) - 12) / 64.0;  // +-0.1875
  };
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) {
      const bool edge = i == 0 || j == 0 || i == n || j == n;
      const double x = i + (edge ? 0.0 : jit()), y = j + (edge ? 0.0 : jit());
      const ON_3dPoint p = origin + ex * x + ey * y;
      m.raw().m_V.Append(ON_3fPoint(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)));
    }
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const int a = j * (n + 1) + i, b = a + 1, c = a + n + 2, d = a + n + 1;
      if ((i + j) % 2 == 0) {
        AddTri(m, a, b, c);
        AddTri(m, a, c, d);
      } else {
        AddTri(m, a, b, d);
        AddTri(m, b, c, d);
      }
    }
  return m;
}

// Largest spread of |uv_i - uv_j| / |p_i - p_j| over EVERY pair of vertices (0 for an exact similarity).
double PairwiseRatioSpread(const Mesh& m, const std::vector<Point2d>& uv) {
  const int n = m.VertexCount();
  double lo = 1e300, hi = 0.0;
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j) {
      const double l3 = V(m, i).DistanceTo(V(m, j));
      const double lu = std::hypot(uv[static_cast<size_t>(i)].x - uv[static_cast<size_t>(j)].x, uv[static_cast<size_t>(i)].y - uv[static_cast<size_t>(j)].y);
      const double q = lu / l3;
      lo = std::min(lo, q);
      hi = std::max(hi, q);
    }
  return hi / lo - 1.0;
}

Mesh CylinderStrip(double r, double h, double arc_deg, int cols, int rows) {
  Mesh m;
  const double arc = arc_deg * ON_PI / 180.0;
  for (int j = 0; j <= rows; ++j)
    for (int i = 0; i <= cols; ++i) {
      const double t = arc * i / cols;
      m.raw().m_V.Append(ON_3fPoint(static_cast<float>(r * std::cos(t)), static_cast<float>(r * std::sin(t)), static_cast<float>(h * j / rows)));
    }
  for (int j = 0; j < rows; ++j)
    for (int i = 0; i < cols; ++i) {
      const int a = j * (cols + 1) + i;
      AddQuad(m, a, a + 1, a + cols + 2, a + cols + 1);
    }
  return m;
}

}  // namespace

// A planar patch with an irregular triangulation unwraps to itself up to a similarity: EVERY vertex pair keeps the same
// UV/3D distance ratio. Axis-aligned planes use exact float coordinates (tolerance 1e-9); the tilted plane is rounded to float
// by construction, so its coordinates are coplanar only to ~6e-8 (tolerance 2e-5).
D8_EXTRA_TEST(LscmFlatIrregularPatchIsASimilarity) {
  struct Case {
    const char* label;
    ON_3dVector ex, ey;
    ON_3dPoint origin;
    double tol;
  };
  const double c30 = std::cos(ON_PI / 6.0), s30 = std::sin(ON_PI / 6.0);
  const Case cases[] = {
      {"z = 2 plane", ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0), ON_3dPoint(0, 0, 2), 1e-9},
      {"x = 3 plane (cyclic axis permutation)", ON_3dVector(0, 1, 0), ON_3dVector(0, 0, 1), ON_3dPoint(3, 0, 0), 1e-9},
      {"plane tilted 30 degrees about x, offset", ON_3dVector(1, 0, 0), ON_3dVector(0, c30, s30), ON_3dPoint(5, -2, 4), 2e-5},
  };
  for (const Case& c : cases) {
    const std::string l = c.label;
    const Mesh m = IrregularPatch(9, c.ex, c.ey, c.origin);
    LscmReport rep;
    const std::vector<Point2d> uv = ComputeLscmUVs(m, &rep);
    d8test::Check(rep.converged && rep.residual < 1e-12, (l + ": the solve converged (normal-equation residual < 1e-12)").c_str());
    d8test::Check(static_cast<int>(uv.size()) == m.VertexCount(), (l + ": one UV per vertex").c_str());
    d8test::Check(PairwiseRatioSpread(m, uv) < c.tol,
                  (l + ": every vertex pair keeps one UV/3D distance ratio (spread " + Sci(PairwiseRatioSpread(m, uv)) + " < " + Sci(c.tol) + ")").c_str());
    d8test::Check(FlippedTriangles(m, uv) == 0, (l + ": no UV triangle is wound against the mesh").c_str());
    // the two pinned vertices are exactly where the documentation says: first at the origin, second on +u at their 3D distance
    d8test::Check(std::fabs(uv[static_cast<size_t>(rep.pinned_a)].x) + std::fabs(uv[static_cast<size_t>(rep.pinned_a)].y) == 0.0 &&
                      std::fabs(uv[static_cast<size_t>(rep.pinned_b)].x - V(m, rep.pinned_a).DistanceTo(V(m, rep.pinned_b))) < 1e-12 && uv[static_cast<size_t>(rep.pinned_b)].y == 0.0,
                  (l + ": the pinned boundary pair sits at (0, 0) and (|P_a P_b|, 0)").c_str());
  }
}

// A cylinder strip (quads) is developable: LSCM unrolls it to the true flat rectangle. Closed form for the unrolled width.
D8_EXTRA_TEST(LscmUnrollsACylinderStripExactly) {
  const double r = 2.0, h = 3.0, arc_deg = 240.0;
  const int N = 48, R = 4;
  const Mesh m = CylinderStrip(r, h, arc_deg, N, R);
  LscmReport rep;
  const std::vector<Point2d> uv = ComputeLscmUVs(m, &rep);
  d8test::Check(rep.converged, "cylinder strip: the solve converged");
  double scale = 0.0;
  const double sim = SimilarityResidual(m, uv, &scale);
  d8test::Check(sim < 1e-5, ("cylinder strip: every edge keeps its 3D length up to one global scale (relative deviation " + Sci(sim) + " < 1e-5; float vertex coordinates bound it at ~1e-7)").c_str());
  d8test::Check(FlippedTriangles(m, uv) == 0, "cylinder strip: no UV triangle is wound against the mesh");

  // closed form: the unrolled strip is a rectangle of (N chords) x h. Measure it along the generator direction fixed by a vertical edge.
  const Point2d g(uv[static_cast<size_t>(N + 1)].x - uv[0].x, uv[static_cast<size_t>(N + 1)].y - uv[0].y);
  const double glen = std::hypot(g.x, g.y);
  const double s = glen / (h / R);  // UV units per 3D unit, from one generator edge
  const double dx = -g.y / glen, dy = g.x / glen;  // unit vector perpendicular to the generators
  double lo = 1e300, hi = -1e300;
  for (const Point2d& p : uv) {
    const double t = p.x * dx + p.y * dy;
    lo = std::min(lo, t);
    hi = std::max(hi, t);
  }
  const double chord = 2.0 * r * std::sin((arc_deg * ON_PI / 180.0) / N / 2.0);
  d8test::Check(std::fabs((hi - lo) - s * N * chord) < 1e-5 * s * N * chord,
                ("cylinder strip: unrolled width " + std::to_string(hi - lo) + " = scale * N * 2 r sin(arc / 2N) = " + std::to_string(s * N * chord)).c_str());
  d8test::Check(std::fabs(scale - s) < 1e-5 * s, "cylinder strip: the global edge scale equals the scale the generators alone give");
  // the strip is h tall: every generator is straight and parallel in UV
  double worst_par = 0.0;
  for (int i = 0; i <= N; ++i) {
    const Point2d a = uv[static_cast<size_t>(i)], b = uv[static_cast<size_t>(R * (N + 1) + i)];
    const double len = std::hypot(b.x - a.x, b.y - a.y);
    worst_par = std::max(worst_par, std::fabs((b.x - a.x) * g.y - (b.y - a.y) * g.x) / (len * glen));
  }
  d8test::Check(worst_par < 1e-5, ("cylinder strip: all " + std::to_string(N + 1) + " generators are parallel in UV (max sin of angle " + Sci(worst_par) + ")").c_str());

  // negative controls: the fixed projections fail the same oracle by orders of magnitude (the failing-first measurements)
  d8test::Check(SimilarityResidual(m, m.ComputePlanarMappingUVs(1.0)) > 1.0, "negative control: the planar projection of the strip is NOT a similarity (residual > 1)");
  d8test::Check(SimilarityResidual(m, m.ComputeBoxMappingUVs(1.0)) > 1.0, "negative control: the box projection of the strip is NOT a similarity (residual > 1)");
  d8test::Check(SimilarityResidual(m, m.ComputeCylindricalMappingUVs(1.0)) > 1.0, "negative control: the cylindrical projection (wraps u as an angle) is NOT a similarity (residual > 1)");
}

// A cone band (apex excluded) is developable too, with edge lengths that vary along the strip: a harder exact case than a cylinder.
D8_EXTRA_TEST(LscmUnrollsAConeBandExactly) {
  Mesh m;
  const int N = 40, R = 5;
  const double arc = 300.0 * ON_PI / 180.0;
  for (int j = 0; j <= R; ++j) {
    const double t = 1.0 + 3.0 * j / R;  // distance from the apex along each ray: 1 .. 4
    for (int i = 0; i <= N; ++i) {
      const double a = arc * i / N;
      m.raw().m_V.Append(ON_3fPoint(static_cast<float>(t * 0.6 * std::cos(a)), static_cast<float>(t * 0.6 * std::sin(a)), static_cast<float>(t * 0.8)));
    }
  }
  for (int j = 0; j < R; ++j)
    for (int i = 0; i < N; ++i) {
      const int a = j * (N + 1) + i;
      AddQuad(m, a, a + 1, a + N + 2, a + N + 1);
    }
  const std::vector<Point2d> uv = ComputeLscmUVs(m);
  const double sim = SimilarityResidual(m, uv);
  d8test::Check(sim < 1e-5, ("cone band: every edge keeps its 3D length up to one global scale (relative deviation " + Sci(sim) + " < 1e-5)").c_str());
  d8test::Check(FlippedTriangles(m, uv) == 0, "cone band: no UV triangle is wound against the mesh");
  d8test::Check(SimilarityResidual(m, m.ComputePlanarMappingUVs(1.0)) > 0.5, "negative control: the planar projection of the cone band is NOT a similarity");
}

// A hemisphere cap is not developable, so the angle distortion is positive - but LSCM makes it the smallest any map can: several
// times below every projection the kernel had.
D8_EXTRA_TEST(LscmHemisphereAngleDistortionBeatsEveryProjection) {
  const int K = 12, M = 48;
  Mesh m;
  m.raw().m_V.Append(ON_3fPoint(0, 0, 1));
  for (int k = 1; k <= K; ++k) {
    const double th = (ON_PI / 2.0) * k / K;
    for (int i = 0; i < M; ++i) {
      const double ph = 2.0 * ON_PI * i / M;
      m.raw().m_V.Append(ON_3fPoint(static_cast<float>(std::sin(th) * std::cos(ph)), static_cast<float>(std::sin(th) * std::sin(ph)), static_cast<float>(std::cos(th))));
    }
  }
  for (int i = 0; i < M; ++i) AddTri(m, 0, 1 + i, 1 + (i + 1) % M);
  for (int k = 0; k + 1 < K; ++k)
    for (int i = 0; i < M; ++i) AddQuad(m, 1 + k * M + i, 1 + (k + 1) * M + i, 1 + (k + 1) * M + (i + 1) % M, 1 + k * M + (i + 1) % M);
  LscmReport rep;
  const std::vector<Point2d> uv = ComputeLscmUVs(m, &rep);
  d8test::Check(rep.converged, "hemisphere: the solve converged");
  double worst = 0.0, mean = 0.0;
  AngleError(m, uv, worst, mean);
  double pw = 0.0, pm = 0.0, sw = 0.0, sm = 0.0;
  AngleError(m, m.ComputePlanarMappingUVs(1.0), pw, pm);
  AngleError(m, m.ComputeSphericalMappingUVs(), sw, sm);
  d8test::Check(mean < 1.5 && worst < 8.0, ("hemisphere: LSCM mean angle error " + std::to_string(mean) + " deg < 1.5 and worst " + std::to_string(worst) + " deg < 8").c_str());
  d8test::Check(mean < 0.2 * pm && worst < 0.25 * pw, ("hemisphere: LSCM is at least 5x better in the mean and 4x in the worst corner than the planar projection (" + std::to_string(pm) + " / " + std::to_string(pw) + " deg)").c_str());
  d8test::Check(mean < 0.2 * sm && worst < 0.1 * sw, ("hemisphere: ... and than the spherical mapping (" + std::to_string(sm) + " / " + std::to_string(sw) + " deg)").c_str());
  d8test::Check(FlippedTriangles(m, uv) == 0, "hemisphere: no UV triangle is wound against the mesh");
}

// Deterministic and rigid-motion invariant: moving the mesh by an exact rigid motion (cyclic axis permutation + an integer
// translation, both exact in float) gives the same UVs bit-for-bit up to solver round-off.
D8_EXTRA_TEST(LscmIsDeterministicAndRigidMotionInvariant) {
  const Mesh m = IrregularPatch(7, ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0), ON_3dPoint(0, 0, 0));
  Mesh moved;
  for (int i = 0; i < m.VertexCount(); ++i) {
    const ON_3dPoint p = V(m, i);
    moved.raw().m_V.Append(ON_3fPoint(static_cast<float>(p.z + 100.0), static_cast<float>(p.x - 7.0), static_cast<float>(p.y + 3.0)));  // (x, y, z) -> (z, x, y): a rotation
  }
  for (int i = 0; i < m.raw().m_F.Count(); ++i) moved.raw().m_F.Append(m.raw().m_F[i]);
  const std::vector<Point2d> a = ComputeLscmUVs(m), a2 = ComputeLscmUVs(m), b = ComputeLscmUVs(moved);
  double same = 0.0, rigid = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    same = std::max(same, std::hypot(a[i].x - a2[i].x, a[i].y - a2[i].y));
    rigid = std::max(rigid, std::hypot(a[i].x - b[i].x, a[i].y - b[i].y));
  }
  d8test::Check(same == 0.0, "the same mesh unwraps to bit-identical UVs twice");
  d8test::Check(rigid < 1e-9, ("a rigidly moved copy unwraps to the same UVs (max difference " + Sci(rigid) + " < 1e-9)").c_str());
}

// The UVs are directly usable: they go straight into SetTextureCoordinates().
D8_EXTRA_TEST(LscmUVsFeedSetTextureCoordinates) {
  Mesh m = CylinderStrip(2.0, 3.0, 240.0, 12, 2);
  d8test::Check(!m.HasTextureCoordinates(), "control: the strip starts with no texture coordinates");
  d8test::Check(m.SetTextureCoordinates(ComputeLscmUVs(m)) == dino8::kernel::Result::Ok, "SetTextureCoordinates accepts the LSCM UVs");
  d8test::Check(m.HasTextureCoordinates(), "the mesh now has texture coordinates");
}

// LSCM is only defined on an open, connected, consistently oriented, non-degenerate mesh; everything else is refused by name.
D8_EXTRA_TEST(LscmRefusesMeshesItIsNotDefinedOn) {
  auto refuses = [](const Mesh& m, const char* label) {
    bool threw = false, right_type = false;
    try {
      ComputeLscmUVs(m);
    } catch (const std::invalid_argument&) {
      threw = right_type = true;
    } catch (...) {
      threw = true;
    }
    d8test::Check(threw && right_type, (std::string(label) + ": refused with std::invalid_argument").c_str());
  };
  // positive control for every case below: the same builder, valid, unwraps
  {
    bool ok = true;
    try {
      ComputeLscmUVs(CylinderStrip(2.0, 3.0, 240.0, 8, 2));
    } catch (...) {
      ok = false;
    }
    d8test::Check(ok, "control: a valid strip is accepted");
  }
  refuses(Mesh(), "empty mesh");
  {  // closed: a tetrahedron
    Mesh m;
    m.raw().m_V.Append(ON_3fPoint(0, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(1, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(0, 1, 0));
    m.raw().m_V.Append(ON_3fPoint(0, 0, 1));
    AddTri(m, 0, 2, 1);
    AddTri(m, 0, 1, 3);
    AddTri(m, 1, 2, 3);
    AddTri(m, 2, 0, 3);
    refuses(m, "closed tetrahedron (no boundary)");
  }
  {  // two disconnected triangles
    Mesh m;
    for (int k = 0; k < 2; ++k) {
      m.raw().m_V.Append(ON_3fPoint(5.0f * k, 0, 0));
      m.raw().m_V.Append(ON_3fPoint(5.0f * k + 1, 0, 0));
      m.raw().m_V.Append(ON_3fPoint(5.0f * k, 1, 0));
      AddTri(m, 3 * k, 3 * k + 1, 3 * k + 2);
    }
    refuses(m, "two connected components");
  }
  {  // a collinear (zero-area) triangle
    Mesh m;
    m.raw().m_V.Append(ON_3fPoint(0, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(1, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(2, 0, 0));
    AddTri(m, 0, 1, 2);
    refuses(m, "zero-area triangle");
  }
  {  // two triangles sharing an edge with opposite-sense winding conflict
    Mesh m;
    m.raw().m_V.Append(ON_3fPoint(0, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(1, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(1, 1, 0));
    m.raw().m_V.Append(ON_3fPoint(0, 1, 0));
    AddTri(m, 0, 1, 2);
    AddTri(m, 0, 1, 3);  // traverses 0->1 again: inconsistent orientation
    refuses(m, "inconsistently oriented neighbours");
  }
  {  // a vertex no face uses
    Mesh m = CylinderStrip(2.0, 3.0, 240.0, 8, 2);
    m.raw().m_V.Append(ON_3fPoint(9, 9, 9));
    refuses(m, "an unreferenced vertex");
  }
  {  // three faces on one edge
    Mesh m;
    m.raw().m_V.Append(ON_3fPoint(0, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(1, 0, 0));
    m.raw().m_V.Append(ON_3fPoint(0, 1, 0));
    m.raw().m_V.Append(ON_3fPoint(0, -1, 0));
    m.raw().m_V.Append(ON_3fPoint(0, 0, 1));
    AddTri(m, 0, 1, 2);
    AddTri(m, 1, 0, 3);
    AddTri(m, 0, 1, 4);  // third use of edge 0-1 (and the same direction as the first)
    refuses(m, "three faces on one edge");
  }
}
