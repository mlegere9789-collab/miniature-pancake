// OffsetSolidPlanar: see offset_solid.h for the construction and the honest scope.
#include "dino8/kernel/offset_solid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

namespace {

using Vec = ON_3dVector;

std::string Num(double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

std::string Pt(const Point3d& p) { return "(" + Num(p.x) + ", " + Num(p.y) + ", " + Num(p.z) + ")"; }

// Area vector (Newell) of a polygon: its direction is the polygon's normal, its length twice the area.
Vec AreaVector(const std::vector<Point3d>& l) {
  Vec n(0, 0, 0);
  for (size_t i = 0; i < l.size(); ++i) {
    const Point3d& a = l[i];
    const Point3d& b = l[(i + 1) % l.size()];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  return n;
}

// Signed volume of outward polygons: the divergence theorem over each polygon's fan (exact for a planar polygon, convex or not).
double SignedVolume(const std::vector<std::vector<Point3d>>& polys) {
  double v = 0.0;
  for (const auto& l : polys) {
    for (size_t k = 1; k + 1 < l.size(); ++k) {
      const Vec a(l[0].x, l[0].y, l[0].z), b(l[k].x, l[k].y, l[k].z), c(l[k + 1].x, l[k + 1].y, l[k + 1].z);
      v += ON_DotProduct(a, ON_CrossProduct(b, c)) / 6.0;
    }
  }
  return v;
}

// Eigen-decomposition of a symmetric 3x3 matrix by cyclic Jacobi rotations: a = V diag(w) V^T (columns of V are eigenvectors).
void Jacobi3(double a[3][3], double w[3], double v[3][3]) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) v[i][j] = i == j ? 1.0 : 0.0;
  for (int sweep = 0; sweep < 60; ++sweep) {
    const double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
    if (off < 1e-300) break;
    for (int p = 0; p < 2; ++p) {
      for (int q = p + 1; q < 3; ++q) {
        if (std::fabs(a[p][q]) < 1e-300) continue;
        const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
        const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
        for (int k = 0; k < 3; ++k) {  // A <- A J
          const double akp = a[k][p], akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (int k = 0; k < 3; ++k) {  // A <- J^T A
          const double apk = a[p][k], aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
        for (int k = 0; k < 3; ++k) {  // V <- V J
          const double vkp = v[k][p], vkq = v[k][q];
          v[k][p] = c * vkp - s * vkq;
          v[k][q] = s * vkp + c * vkq;
        }
      }
    }
  }
  for (int i = 0; i < 3; ++i) w[i] = a[i][i];
}

bool SegmentsCross2D(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy, double eps) {
  auto orient = [](double px, double py, double qx, double qy, double rx, double ry) { return (qx - px) * (ry - py) - (qy - py) * (rx - px); };
  const double d1 = orient(ax, ay, bx, by, cx, cy), d2 = orient(ax, ay, bx, by, dx, dy);
  const double d3 = orient(cx, cy, dx, dy, ax, ay), d4 = orient(cx, cy, dx, dy, bx, by);
  return ((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) && ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps));
}


// The measured reason a solid is not planar-faced: how many faces are curved and what the first one is (type and its measured
// parameters), so the refusal says what would have to be handled rather than just "not planar".
std::string DescribeNonPlanar(const ON_Brep& b, const char* planar_faces_message) {
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double tol = 1e-6 * std::max(bb.Diagonal().Length(), 1e-9);
  int curved = 0, first = -1;
  std::string what;
  for (int fi = 0; fi < b.m_F.Count(); ++fi) {
    const ON_Surface* surf = b.m_F[fi].SurfaceOf();
    if (surf == nullptr) continue;
    ON_Plane pl;
    if (surf->IsPlanar(&pl, tol)) continue;
    ++curved;
    if (first >= 0) continue;
    first = fi;
    ON_Cylinder cyl;
    ON_Sphere sph;
    ON_Cone cone;
    ON_Torus tor;
    if (surf->IsCylinder(&cyl, tol)) {
      what = "a cylinder of radius " + Num(cyl.circle.Radius()) + " about the axis through " + Pt(cyl.circle.Center()) + " along (" + Num(cyl.Axis().x) + ", " +
             Num(cyl.Axis().y) + ", " + Num(cyl.Axis().z) + ")";
    } else if (surf->IsSphere(&sph, tol)) {
      what = "a sphere of radius " + Num(sph.Radius()) + " about " + Pt(sph.Center());
    } else if (surf->IsCone(&cone, tol)) {
      what = "a cone";
    } else if (surf->IsTorus(&tor, tol)) {
      what = "a torus (major radius " + Num(tor.major_radius) + ", minor radius " + Num(tor.minor_radius) + ")";
    } else {
      what = "a freeform surface";
    }
  }
  if (first < 0) {
    return std::string("a face is not planar or cannot be read as a planar polygon (Brep::PlanarFaces: ") + planar_faces_message +
           "); only planar-faced solids are offset here";
  }
  return "a face is not planar: " + std::to_string(curved) + " of " + std::to_string(b.m_F.Count()) + " faces are curved; the first, face " + std::to_string(first) +
         ", is " + what + ". Only planar-faced solids are offset here: a curved face's offset is exact only where its planar neighbours meet it in "
         "circles or lines that the OffsetSrf constructions for a capped cylinder, a coaxial revolved profile or a right prism over a profile with arcs "
         "already build, and the general case needs trimmed-face reconstruction that is not implemented";
}
}  // namespace

Result OffsetSolidPlanar(const Brep& solid, double distance, Brep& out, PlanarSolidOffsetReport* report) {
  PlanarSolidOffsetReport local;
  PlanarSolidOffsetReport& R = report ? *report : local;
  R = PlanarSolidOffsetReport();
  auto refuse = [&](const std::string& why) {
    R.refusal_reason = why;
    return Result::Failed;
  };

  std::vector<Brep::PlanarFace> faces;
  try {
    faces = solid.PlanarFaces();
  } catch (const std::exception& e) {
    return refuse(DescribeNonPlanar(solid.raw(), e.what()));
  }
  const int nf = static_cast<int>(faces.size());
  R.faces = nf;
  if (nf < 4) return refuse("a closed solid needs at least 4 planar faces; this has " + std::to_string(nf));

  // Scale and weld tolerance.
  ON_BoundingBox bb;
  for (const auto& f : faces)
    for (const Point3d& p : f.loop) bb.Set(p, true);
  const double scale = std::max(bb.Diagonal().Length(), 1e-9);
  const double wtol = 1e-8 * scale;

  // Local frame: everything below (the divergence-theorem volumes, the corner solve, and above all the tessellation whose float
  // vertices feed the global overlap check) runs on coordinates re-centred near the origin; the result is shifted back at the end.
  // The shift is the bounding-box centre rounded to a multiple of a power of two 2^-20 of the model size (so it is within a millionth of
  // the model of the centre, which a fan volume about it needs, and subtracting it is exact).
  // Without it a solid far from the origin (a 1 x 1 x 1 cube at x = 1e6) is refused as "inward-oriented" or gets a wrong volume, and a
  // box 1e5 away is refused for spurious self-intersections of its float-precision tessellation.
  const double quantum = std::ldexp(1.0, std::ilogb(scale) - 20);
  const ON_3dPoint centre = bb.Center();
  const Point3d shift(std::round(centre.x / quantum) * quantum, std::round(centre.y / quantum) * quantum, std::round(centre.z / quantum) * quantum);
  const bool shifted = shift.x != 0.0 || shift.y != 0.0 || shift.z != 0.0;
  if (shifted) {
    for (auto& f : faces)
      for (Point3d& p : f.loop) p = Point3d(p.x - shift.x, p.y - shift.y, p.z - shift.z);
  }
  auto PtWorld = [&](const Point3d& p) { return Pt(Point3d(p.x + shift.x, p.y + shift.y, p.z + shift.z)); };

  // Weld the loops' points into vertices.
  std::vector<Point3d> V;
  std::vector<std::vector<int>> idx(nf);
  for (int f = 0; f < nf; ++f) {
    for (const Point3d& p : faces[f].loop) {
      int found = -1;
      for (int v = 0; v < static_cast<int>(V.size()) && found < 0; ++v)
        if (std::fabs(V[v].x - p.x) <= wtol && std::fabs(V[v].y - p.y) <= wtol && std::fabs(V[v].z - p.z) <= wtol) found = v;
      if (found < 0) {
        found = static_cast<int>(V.size());
        V.push_back(p);
      }
      if (idx[f].empty() || idx[f].back() != found) idx[f].push_back(found);
    }
    while (idx[f].size() > 1 && idx[f].front() == idx[f].back()) idx[f].pop_back();
    if (idx[f].size() < 3) return refuse("face " + std::to_string(f) + " has fewer than 3 distinct vertices");
  }
  const int nv = static_cast<int>(V.size());
  R.vertices = nv;

  // Closedness: every directed edge needs exactly one reverse partner.
  std::map<std::pair<int, int>, int> directed;
  for (int f = 0; f < nf; ++f)
    for (size_t k = 0; k < idx[f].size(); ++k) ++directed[{idx[f][k], idx[f][(k + 1) % idx[f].size()]}];
  int unpaired = 0;
  std::string first_unpaired;
  for (const auto& kv : directed) {
    const auto rev = directed.find({kv.first.second, kv.first.first});
    if (kv.second != 1 || rev == directed.end() || rev->second != 1) {
      if (unpaired++ == 0) first_unpaired = " between " + PtWorld(V[kv.first.first]) + " and " + PtWorld(V[kv.first.second]);
    }
  }
  if (unpaired > 0)
    return refuse(std::to_string(unpaired) + " unpaired or non-manifold directed edge(s) (the faces do not close into a solid), the first" + first_unpaired);
  std::vector<std::pair<int, int>> edges;
  for (const auto& kv : directed)
    if (kv.first.first < kv.first.second) edges.push_back(kv.first);
  R.edges = static_cast<int>(edges.size());

  // Face planes: normal from the welded loop, planarity measured.
  std::vector<Vec> normal(nf);
  std::vector<std::vector<Point3d>> old_polys(nf);
  for (int f = 0; f < nf; ++f) {
    for (int v : idx[f]) old_polys[f].push_back(V[v]);
    Vec a = AreaVector(old_polys[f]);
    const double len = a.Length();
    if (!(len > 1e-12 * scale * scale)) return refuse("face " + std::to_string(f) + " has zero area");
    a.Unitize();
    normal[f] = a;
    double dev = 0.0;
    for (const Point3d& p : old_polys[f]) dev = std::max(dev, std::fabs(ON_DotProduct(a, Vec(p.x - old_polys[f][0].x, p.y - old_polys[f][0].y, p.z - old_polys[f][0].z))));
    if (dev > 1e-7 * scale)
      return refuse("face " + std::to_string(f) + " is not planar: its vertices deviate " + Num(dev) + " from their best plane (tolerance " + Num(1e-7 * scale) + ")");
  }
  R.volume_before = SignedVolume(old_polys);
  if (!(R.volume_before > 0.0)) return refuse("the solid is inward-oriented or degenerate: signed volume " + Num(R.volume_before));

  if (distance == 0.0) {
    out = solid;
    R.volume_after = R.volume_before;
    R.min_edge_length_ratio = 1.0;
    R.self_intersections = 0;
    return Result::Ok;
  }

  // Corner reconstruction: the minimum-norm displacement delta of each vertex that puts it on every incident offset plane,
  // n_i . delta = d, i.e. M delta = d sum n_i with M = sum n_i n_i^T (pseudo-inverse by eigen-decomposition: rank 3 is the
  // unique intersection of three independent planes, lower rank the minimum-norm point of a straight edge or flat region).
  std::vector<std::vector<int>> incident(nv);
  for (int f = 0; f < nf; ++f)
    for (int v : idx[f]) {
      auto& l = incident[v];
      if (std::find(l.begin(), l.end(), f) == l.end()) l.push_back(f);
    }
  std::vector<Vec> delta(nv);
  const double tol_conc = 1e-8 * (scale + std::fabs(distance));
  double worst_resid = 0.0;
  int worst_vertex = -1;
  for (int v = 0; v < nv; ++v) {
    double M[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    Vec sum(0, 0, 0);
    for (int f : incident[v]) {
      const Vec& n = normal[f];
      const double c[3] = {n.x, n.y, n.z};
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) M[i][j] += c[i] * c[j];
      sum += n;
    }
    double w[3], E[3][3];
    Jacobi3(M, w, E);
    const double wmax = std::max({w[0], w[1], w[2], 1e-300});
    int rank = 0;
    Vec dv(0, 0, 0);
    const double rhs[3] = {distance * sum.x, distance * sum.y, distance * sum.z};
    for (int k = 0; k < 3; ++k) {
      if (w[k] <= 1e-10 * wmax) continue;
      ++rank;
      const double coef = (E[0][k] * rhs[0] + E[1][k] * rhs[1] + E[2][k] * rhs[2]) / w[k];
      dv += Vec(E[0][k], E[1][k], E[2][k]) * coef;
    }
    delta[v] = dv;
    if (incident[v].size() > 3) ++R.vertices_over_three_faces;
    if (rank < 3) ++R.rank_deficient_vertices;
    for (int f : incident[v]) {
      const double resid = std::fabs(ON_DotProduct(normal[f], dv) - distance);
      if (resid > worst_resid) {
        worst_resid = resid;
        worst_vertex = v;
      }
    }
  }
  R.max_plane_residual = worst_resid;
  if (worst_resid > tol_conc)
    return refuse("the offset planes of the " + std::to_string(incident[worst_vertex].size()) + " faces meeting at vertex " + PtWorld(V[worst_vertex]) +
                  " are not concurrent: worst |n . delta - d| = " + Num(worst_resid) + " (tolerance " + Num(tol_conc) +
                  "); a mitre corner needs the planes to meet in one point");

  std::vector<Point3d> W(nv);
  for (int v = 0; v < nv; ++v) W[v] = Point3d(V[v].x + delta[v].x, V[v].y + delta[v].y, V[v].z + delta[v].z);

  // Edges keep their direction (they are the intersection of two translated planes) and must not shrink to nothing or reverse.
  double min_ratio = 1e300;
  int min_edge = -1;
  for (size_t e = 0; e < edges.size(); ++e) {
    const Vec w0(V[edges[e].second].x - V[edges[e].first].x, V[edges[e].second].y - V[edges[e].first].y, V[edges[e].second].z - V[edges[e].first].z);
    const Vec w1(W[edges[e].second].x - W[edges[e].first].x, W[edges[e].second].y - W[edges[e].first].y, W[edges[e].second].z - W[edges[e].first].z);
    const double l2 = w0.Length() * w0.Length();
    if (!(l2 > 0.0)) return refuse("an edge of the input has zero length at " + PtWorld(V[edges[e].first]));
    const double ratio = ON_DotProduct(w0, w1) / l2;
    if (ratio < min_ratio) {
      min_ratio = ratio;
      min_edge = static_cast<int>(e);
    }
  }
  R.min_edge_length_ratio = min_ratio;
  if (min_ratio <= 1e-9) {
    const auto& e = edges[min_edge];
    return refuse("the edge between " + PtWorld(V[e.first]) + " and " + PtWorld(V[e.second]) + " " + (min_ratio < 0 ? "reverses" : "collapses") +
                  " under this offset: new length / old length = " + Num(min_ratio) + " (a wall or gap closes; the topology would have to change)");
  }

  // Faces: still planar by construction; each must stay a simple polygon (a bowtie means a wall has swept through another).
  std::vector<std::vector<Point3d>> new_polys(nf);
  for (int f = 0; f < nf; ++f) {
    for (int v : idx[f]) new_polys[f].push_back(W[v]);
    // Simple-polygon test in the plane (drop the dominant normal axis).
    const double ax = std::fabs(normal[f].x), ay = std::fabs(normal[f].y), az = std::fabs(normal[f].z);
    const int drop = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
    auto u = [&](const Point3d& p) { return drop == 0 ? p.y : p.x; };
    auto vv = [&](const Point3d& p) { return drop == 2 ? p.y : p.z; };
    const auto& P = new_polys[f];
    const size_t m = P.size();
    for (size_t i = 0; i < m; ++i) {
      for (size_t j = i + 2; j < m; ++j) {
        if (i == 0 && j == m - 1) continue;
        if (SegmentsCross2D(u(P[i]), vv(P[i]), u(P[(i + 1) % m]), vv(P[(i + 1) % m]), u(P[j]), vv(P[j]), u(P[(j + 1) % m]), vv(P[(j + 1) % m]), 1e-12 * scale * scale))
          return refuse("face " + std::to_string(f) + " crosses itself under this offset (edges " + std::to_string(i) + " and " + std::to_string(j) + " of its boundary intersect)");
      }
    }
  }
  R.volume_after = SignedVolume(new_polys);
  if (!(R.volume_after > 0.0)) return refuse("the offset solid has non-positive volume " + Num(R.volume_after));

  // Assemble the result.
  std::vector<Brep::PlanarFace> out_faces(nf);
  for (int f = 0; f < nf; ++f) {
    out_faces[f].loop = new_polys[f];
    out_faces[f].plane = ON_Plane(new_polys[f][0], normal[f]);
  }
  Brep built;
  try {
    built = Brep::FromPlanarFaces(out_faces);
  } catch (const std::exception& e) {
    return refuse(std::string("the offset faces could not be assembled into a B-rep: ") + e.what());
  }

  // Global overlap: tessellate and look for crossings of non-adjacent faces (two arms closing on each other, a slot narrower than
  // twice the distance) that no single edge or face shows.
  Mesh mesh;
  bool closed = false;
  int open_edges = 0;
  for (int div : {4, 8, 16}) {
    mesh = built.TessellateToClosedMesh(div, div);
    closed = mesh.IsClosedManifold();
    if (closed) break;
  }
  // An open tessellation is itself the usual signature of overlapping faces (their grid cells no longer weld), so the crossing
  // search runs on it regardless: a crossing found is a measured refusal; none found on an open mesh is inconclusive and refused.
  const std::vector<std::pair<int, int>> crossings = mesh.FindSelfIntersections(std::max(1e-9 * scale, 1e-12));
  R.self_intersections = static_cast<int>(crossings.size());
  if (!crossings.empty())
    return refuse("the offset solid self-intersects: " + std::to_string(crossings.size()) +
                  " crossing triangle pair(s) between non-adjacent faces (first: triangles " + std::to_string(crossings[0].first) + " and " +
                  std::to_string(crossings[0].second) + "); no single edge or face shows it" +
                  (closed ? std::string() : std::string(" (its tessellation also does not close, as overlapping faces do not weld)")));
  if (!closed) {
    const ON_Mesh& raw = mesh.raw();
    std::map<std::pair<int, int>, int> use;
    for (int f = 0; f < raw.FaceCount(); ++f) {
      const ON_MeshFace& mf = raw.m_F[f];
      const int c = mf.IsQuad() ? 4 : 3;
      for (int k = 0; k < c; ++k) {
        int a = mf.vi[k], b = mf.vi[(k + 1) % c];
        if (a > b) std::swap(a, b);
        ++use[{a, b}];
      }
    }
    for (const auto& kv : use)
      if (kv.second != 2) ++open_edges;
    return refuse("the offset solid's tessellation is not a closed manifold (" + std::to_string(open_edges) +
                  " edge(s) not shared by exactly two triangles) and no crossing was found on it, so the global overlap check is inconclusive; "
                  "the offset is refused rather than returned unchecked");
  }

  if (shifted) {
    // Back to the input's own frame: the same faces, each point moved by the shift.
    for (int f = 0; f < nf; ++f) {
      for (Point3d& p : out_faces[f].loop) p = Point3d(p.x + shift.x, p.y + shift.y, p.z + shift.z);
      out_faces[f].plane = ON_Plane(out_faces[f].loop[0], normal[f]);
    }
    try {
      built = Brep::FromPlanarFaces(out_faces);
    } catch (const std::exception& e) {
      return refuse(std::string("the offset faces could not be assembled into a B-rep: ") + e.what());
    }
  }
  out = std::move(built);
  return Result::Ok;
}

}  // namespace dino8::kernel
