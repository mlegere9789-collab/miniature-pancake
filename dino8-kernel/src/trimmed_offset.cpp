// OffsetTrimmedFace(): see trimmed_offset.h for the contract.
//
// Construction: the face's surface is replaced by NurbsSurface::Offset() of it
// (same (u, v) domain and parametrization, out(u,v) = S + d N), every 2D trim
// curve is kept AS IS - so the offset face is exactly the image of the same
// (u, v) region - and everything 3D that hangs off the trims is rebuilt from
// the offset surface: each edge becomes a cubic interpolating the offset
// surface along that edge's own 2D trim, each vertex the offset surface at its
// trim end. Nothing is trusted: every rebuilt edge is measured against the
// offset surface and against the exact locus S + d N of the original, and the
// whole body is checked with ON_Brep::IsValid() before it is returned.

#include "dino8/kernel/trimmed_offset.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"

namespace dino8::kernel {

namespace {

constexpr int kStartPoints = 32;
constexpr int kMaxPoints = 2048;

struct EdgeFit {
  ON_NurbsCurve curve;
  double deviation = 0.0;        // to the offset surface along the trim
  double locus_deviation = 0.0;  // to S + d N of the original along the trim
  bool ok = false;
};

}  // namespace

Result OffsetTrimmedFace(const Brep& sheet, double distance, Brep& out, double tolerance,
                         TrimmedOffsetReport* report) {
  const char* caller = "dino8::kernel::OffsetTrimmedFace";
  TrimmedOffsetReport local;
  TrimmedOffsetReport& rep = report ? *report : local;
  rep = TrimmedOffsetReport();
  auto refuse = [&](const std::string& why) {
    rep.refusal_reason = why;
    return Result::Failed;
  };
  if (!std::isfinite(distance)) throw std::invalid_argument(std::string(caller) + ": distance must be finite");
  if (!std::isfinite(tolerance)) throw std::invalid_argument(std::string(caller) + ": tolerance must be finite");
  const ON_Brep& src = sheet.raw();
  if (src.m_F.Count() != 1) {
    throw std::invalid_argument(std::string(caller) +
                                ": sheet must be a single-face body (offsetting several faces needs corner "
                                "reconstruction between the offset neighbours, which is not done here)");
  }
  const ON_BrepFace& face0 = src.m_F[0];
  const ON_Surface* raw_surface = face0.SurfaceOf();
  if (raw_surface == nullptr) throw std::invalid_argument(std::string(caller) + ": the face has no surface");
  if (distance == 0.0) {
    out = sheet;
    rep.tolerance_used = tolerance > 0.0 ? tolerance : 0.0;
    return Result::Ok;
  }

  NurbsSurface base;
  if (!raw_surface->GetNurbForm(base.raw()) || !base.raw().IsValid()) {
    return refuse("the face's surface has no exact NURBS form");
  }
  const double d = face0.m_bRev ? -distance : distance;  // along the face's OUTWARD normal

  NurbsSurface off;
  NurbsSurface::OffsetInfo info;
  if (base.Offset(d, off, tolerance, &info) != Result::Ok) {
    return refuse("the surface offset was refused: " + info.refusal_reason);
  }
  rep.surface_exact = info.exact;
  rep.surface_deviation = info.achieved_deviation;
  rep.tolerance_used = info.tolerance_used;
  const double tol = info.tolerance_used;
  for (int dir = 0; dir < 2; ++dir) {
    const Interval a = base.Domain(dir), b = off.Domain(dir);
    const double scale = std::max(1.0, std::max(std::fabs(a.max), std::fabs(a.min)));
    if (std::fabs(a.min - b.min) > 1e-9 * scale || std::fabs(a.max - b.max) > 1e-9 * scale) {
      return refuse("the offset surface does not keep the input's (u, v) domain, so the trims cannot be carried over");
    }
  }

  // A plane offsets by a rigid translation of itself. Detected from the two surfaces (not from the type), so a
  // recognised-plane NURBS and a freeform patch that happens to be flat both qualify: every edge is then translated as it
  // is - an exact circle stays an exact circle - instead of being re-fitted.
  bool translation_ok = false;
  ON_3dVector translation(0, 0, 0);
  {
    const Interval du = base.Domain(0), dv = base.Domain(1);
    auto at = [&](double fu, double fv) { return std::make_pair(du.min + (du.max - du.min) * fu, dv.min + (dv.max - dv.min) * fv); };
    const auto c = at(0.5, 0.5);
    translation = off.PointAt(c.first, c.second) - base.PointAt(c.first, c.second);
    translation_ok = translation.Length() > 0.0;
    const double scale = std::max(1.0, translation.Length());
    for (int i = 0; i < 5 && translation_ok; ++i) {
      for (int j = 0; j < 5 && translation_ok; ++j) {
        const auto q = at(0.1 + 0.2 * i, 0.1 + 0.2 * j);
        translation_ok = (off.PointAt(q.first, q.second) - base.PointAt(q.first, q.second) - translation).Length() <= 1e-9 * scale;
      }
    }
  }

  Brep result = sheet;
  ON_Brep& b = result.raw();
  ON_BrepFace& face = b.m_F[0];

  // Surface first (in place: this face is its only user).
  {
    auto* ns = new ON_NurbsSurface(off.raw());
    int si = face.m_si;
    if (si >= 0 && si < b.m_S.Count() && b.SurfaceUseCount(si, 2) <= 1) {
      delete b.m_S[si];
      b.m_S[si] = ns;
    } else {
      si = b.AddSurface(ns);
    }
    face.m_si = si;
    face.SetProxySurface(b.m_S[si]);
    face.DestroyRuntimeCache(true);
  }
  const ON_Surface* new_surface = face.SurfaceOf();

  auto locus_at = [&](double u, double v) {
    const Point3d p = base.PointAt(u, v);
    const Vector3d n = base.NormalAt(u, v);
    return ON_3dPoint(p.x + d * n.x, p.y + d * n.y, p.z + d * n.z);
  };

  // Vertices: the offset surface at the trim end each one sits on.
  std::vector<char> vertex_done(static_cast<size_t>(b.m_V.Count()), 0);
  for (int ti = 0; ti < b.m_T.Count(); ++ti) {
    const ON_BrepTrim& trim = b.m_T[ti];
    for (int end = 0; end < 2; ++end) {
      const int vi = trim.m_vi[end];
      if (vi < 0 || vi >= b.m_V.Count()) continue;
      const ON_3dPoint uv = trim.PointAt(trim.Domain().ParameterAt(end == 0 ? 0.0 : 1.0));
      b.m_V[vi].point = new_surface->PointAt(uv.x, uv.y);
      if (!vertex_done[static_cast<size_t>(vi)]) {
        vertex_done[static_cast<size_t>(vi)] = 1;
        ++rep.vertices_moved;
      }
    }
  }

  // Edges: a cubic through the offset surface along each edge's own trim.
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    ON_BrepEdge& edge = b.m_E[ei];
    if (edge.m_ti.Count() == 0) continue;
    if (edge.m_ti.Count() != 1) {
      return refuse("edge " + std::to_string(ei) + " is used by " + std::to_string(edge.m_ti.Count()) +
                    " trims of the face (a seam of a periodic surface): a seam has no single boundary to follow");
    }
    const ON_BrepTrim& trim = b.m_T[edge.m_ti[0]];
    const ON_Curve* c2 = trim.TrimCurveOf();
    if (c2 == nullptr) return refuse("edge " + std::to_string(ei) + " has a trim without a 2D curve");
    {
      double kink = 0.0;
      const ON_Interval td = trim.ProxyCurveDomain();
      if (c2->GetNextDiscontinuity(ON::continuity::G1_continuous, td.Min(), td.Max(), &kink) &&
          kink > td.Min() + 1e-9 * td.Length() && kink < td.Max() - 1e-9 * td.Length()) {
        return refuse("the trim of edge " + std::to_string(ei) + " has a tangent break at t = " + std::to_string(kink) +
                      " inside the edge; a cubic cannot follow a kink - split the edge at its corners first");
      }
    }
    const ON_Interval ed = edge.Domain();
    const bool rev = trim.m_bRev3d;
    auto uv_at = [&](double f) { return trim.PointAt(trim.Domain().ParameterAt(rev ? 1.0 - f : f)); };

    // A closed edge whose trim is smooth through the seam is fitted periodically; otherwise open.
    bool periodic = false;
    if (edge.m_vi[0] == edge.m_vi[1]) {
      const ON_3dPoint a = new_surface->PointAt(uv_at(0.0).x, uv_at(0.0).y);
      const ON_3dPoint z = new_surface->PointAt(uv_at(1.0).x, uv_at(1.0).y);
      const ON_3dVector t0 = trim.TangentAt(trim.Domain().ParameterAt(0.0));
      const ON_3dVector t1 = trim.TangentAt(trim.Domain().ParameterAt(1.0));
      periodic = a.DistanceTo(z) <= 1e-9 * std::max(1.0, a.MaximumCoordinate()) && ON_DotProduct(t0, t1) > 1.0 - 1e-9;
    }

    // Rigid translation: the edge as it is, moved. Measured against the offset surface along the trim like any fit; a
    // misoriented or sub-domain proxy would show as a deviation and fall through to the fit below.
    if (translation_ok) {
      std::unique_ptr<ON_Curve> moved(edge.DuplicateCurve());
      if (moved && moved->SetDomain(ed.Min(), ed.Max()) && moved->Translate(translation)) {
        double dev = 0.0, locus = 0.0;
        const int m = 512;
        for (int k = 0; k < m; ++k) {
          const double f = (k + 0.5) / m;
          const ON_3dPoint uv = uv_at(f);
          const ON_3dPoint q = moved->PointAt(ed.ParameterAt(f));
          dev = std::max(dev, q.DistanceTo(new_surface->PointAt(uv.x, uv.y)));
          locus = std::max(locus, q.DistanceTo(locus_at(uv.x, uv.y)));
        }
        if (dev <= tol) {
          const int c3i = b.AddEdgeCurve(moved.release());
          if (!edge.ChangeEdgeCurve(c3i)) return refuse("ON_BrepEdge::ChangeEdgeCurve failed for edge " + std::to_string(ei));
          edge.m_tolerance = dev;
          rep.max_edge_deviation = std::max(rep.max_edge_deviation, dev);
          rep.max_edge_locus_deviation = std::max(rep.max_edge_locus_deviation, locus);
          ++rep.edges_rebuilt;
          ++rep.edges_translated;
          continue;
        }
      }
    }

    EdgeFit fit;
    for (int n = kStartPoints; n <= kMaxPoints && !fit.ok; n *= 2) {
      std::vector<ON_3dPoint> pts;
      std::vector<double> params;
      const int count = periodic ? n : n + 1;
      for (int k = 0; k < count; ++k) {
        const double f = static_cast<double>(k) / n;
        const ON_3dPoint uv = uv_at(f);
        pts.push_back(new_surface->PointAt(uv.x, uv.y));
        params.push_back(ed.ParameterAt(f));
      }
      if (periodic) params.push_back(ed.Max());
      ON_NurbsCurve curve = periodic ? InterpolateCubicPeriodic(pts, params, 3) : InterpolateCubic(pts, params, false, 3);
      if (!curve.IsValid()) continue;
      double dev = 0.0, locus = 0.0;
      const int m = 8 * n;
      for (int k = 0; k < m; ++k) {
        const double f = (k + 0.5) / m;
        const ON_3dPoint uv = uv_at(f);
        const ON_3dPoint q = curve.PointAt(ed.ParameterAt(f));
        dev = std::max(dev, q.DistanceTo(new_surface->PointAt(uv.x, uv.y)));
        locus = std::max(locus, q.DistanceTo(locus_at(uv.x, uv.y)));
      }
      if (dev <= tol) {
        fit.curve = curve;
        fit.deviation = dev;
        fit.locus_deviation = locus;
        fit.ok = true;
      }
    }
    if (!fit.ok) {
      return refuse("edge " + std::to_string(ei) + " cannot be fitted within the tolerance " + std::to_string(tol) +
                    " by a cubic through " + std::to_string(kMaxPoints) + " points of the offset surface");
    }
    auto* nc = new ON_NurbsCurve(fit.curve);
    const int c3i = b.AddEdgeCurve(nc);
    if (!edge.ChangeEdgeCurve(c3i)) return refuse("ON_BrepEdge::ChangeEdgeCurve failed for edge " + std::to_string(ei));
    edge.m_tolerance = fit.deviation;
    rep.max_edge_deviation = std::max(rep.max_edge_deviation, fit.deviation);
    rep.max_edge_locus_deviation = std::max(rep.max_edge_locus_deviation, fit.locus_deviation);
    ++rep.edges_rebuilt;
  }

  for (int vi = 0; vi < b.m_V.Count(); ++vi) b.SetVertexTolerance(b.m_V[vi]);
  for (int ti = 0; ti < b.m_T.Count(); ++ti) b.SetTrimTolerance(b.m_T[ti]);
  b.DestroyRuntimeCache(true);
  b.Compact();

  ON_wString text;
  ON_TextLog log(text);
  if (!b.IsValid(&log)) {
    return refuse(std::string("the assembled body fails ON_Brep::IsValid(): ") + ON_String(text).Array());
  }
  out = std::move(result);
  return Result::Ok;
}

}  // namespace dino8::kernel
