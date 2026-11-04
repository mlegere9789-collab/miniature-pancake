// Surface, solid, mesh and SubD creation commands.
#include "commands/cmd_common.h"
#include "commands/continuity_request.h"
#include "commands/history_rebuild.h"
#include "commands/prism_solid.h"
#include "commands/section_fit.h"

#include <array>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace dino8::app {

namespace {
// Local copy of the anonymous-namespace WrapBrep below, needed here because
// the free Rebuild* functions have external linkage (declared in
// history_rebuild.h for cmd_history.cpp to call) and so can't see a
// same-TU anonymous-namespace symbol defined later in the file.
kernel::Brep WrapBrepForHistory(ON_Brep* b) {
  kernel::Brep k;
  if (b) { k.raw() = *b; delete b; }
  return k;
}

// A closed extrusion built by ON_BrepExtrudeFace takes its orientation from
// the profile's winding, so a counter-clockwise rectangle came back inside out
// (its display mesh had NEGATIVE volume: a boolean of it with a box returned the
// wrong solid - the union of a 1000 prism and a 1000 box measured 250). Flip a
// closed result whose mesh volume is negative so every extruded solid has
// outward normals, as ExtrudeCrvTapered already does. Open results are left alone.
void OrientOutward(kernel::Brep& k) {
  if (!k.raw().IsSolid()) return;
  BrepMeshOptions opt;
  opt.chord_tolerance = 0.05;
  if (MeshBrepClosed(k.raw(), opt).Volume() < 0) k.raw().Flip();
}
}  // namespace

void RecordHistoryIfEnabled(CommandContext& ctx, ObjectId new_id, const std::string& command,
                             std::vector<ObjectId> sources, std::map<std::string, double> num, bool straight) {
  if (!ctx.App().State().history_recording) return;
  HistoryRecord r;
  r.command = command;
  r.sources = std::move(sources);
  r.num = std::move(num);
  r.straight = straight;
  ctx.Doc().SetHistoryRecord(new_id, std::move(r));
}

// Shared by ExtrudeCommand::ExtrudeCurve (Kind::Curve) below and
// UpdateHistory (cmd_history.cpp) - see history_rebuild.h's comment for why
// this is the single copy of the construction logic both call.
std::optional<SceneObject> RebuildExtrude(CommandContext& ctx, const kernel::NurbsCurve& kc, const HistoryRecord& rec) {
  auto num = [&](const char* k) { auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const Vector3d v(num("vx"), num("vy"), num("vz"));
  const Point3d shift(num("shiftx"), num("shifty"), num("shiftz"));
  const bool solid = num("solid") != 0.0;
  if (v.Length() <= 0) return std::nullopt;
  ON_NurbsCurve c = kc.raw();
  if (shift != Point3d(0, 0, 0)) c.Translate(shift);
  ON_Plane plane;
  const bool closed = c.IsClosed();
  kernel::NurbsCurve for_check;
  const bool self_intersects = CurveFromON(c, for_check) && CurveSelfIntersects(for_check, std::max(ctx.Settings().absolute_tolerance, 1e-9));
  if (!self_intersects && closed && solid && c.IsPlanar(&plane, ctx.Settings().absolute_tolerance)) {
    ON_Brep* b = ON_BrepTrimmedPlane(plane, c);
    if (b) {
      ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
      if (ON_BrepExtrudeFace(*b, 0, path, true) >= 0) { kernel::Brep k = WrapBrepForHistory(b); OrientOutward(k); return SceneObject::MakeBrep(k); }
      delete b;
    }
  }
  ON_SumSurface ss;
  if (!ss.Create(c, v)) return std::nullopt;
  kernel::NurbsSurface k;
  if (!SurfaceFromON(ss, k)) return std::nullopt;
  return SceneObject::MakeSurface(k);
}

// ExtrudeCrvToPoint's construction - shared the same way as RebuildExtrude
// above. A closed, planar, non-self-intersecting profile with Solid=Yes
// now cones to a genuine capped B-rep solid via kernel::Brep::ExtrudeToPoint
// (brep.h's own header comment on that function has the star-shaped-cap
// exactness argument) instead of always falling back to the old
// centroid-collapsed ruled-surface approximation, which never produced a
// real solid (or even a valid cap) at all. `ExtrudeToPoint` throws
// std::invalid_argument when the apex lies in the profile's own plane or
// (non-star-shaped base) the flat cap can't close - either way this falls
// back to the exact same ruled surface the command has always built, so no
// existing result regresses.
std::optional<SceneObject> RebuildExtrudeToPoint(CommandContext& ctx, const kernel::NurbsCurve& kc, const HistoryRecord& rec) {
  auto num = [&](const char* k) { auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const Point3d apex(num("ax"), num("ay"), num("az"));
  const bool solid = num("solid") != 0.0;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-9);
  if (solid && kc.IsClosed() && kc.IsPlanar(tol) && !CurveSelfIntersects(kc, tol)) {
    try {
      return SceneObject::MakeBrep(kernel::Brep::ExtrudeToPoint(kc, apex, true));
    } catch (const std::invalid_argument&) {
      // Apex coplanar with the profile, or a non-star-shaped base can't
      // take a flat cap - fall through to the ruled surface below.
    }
  }
  ON_NurbsCurve a = kc.raw();
  ON_NurbsCurve b = a;
  for (int i = 0; i < b.CVCount(); ++i) b.SetCV(i, apex);
  ON_NurbsSurface s;
  if (!s.CreateRuledSurface(a, b)) return std::nullopt;
  kernel::NurbsSurface k;
  k.raw() = s;
  return SceneObject::MakeSurface(k);
}

// Revolve's construction - shared the same way as RebuildExtrude above.
// Reads num: angle/start_angle (radians; angle missing means the
// pre-existing full 2*pi default, not 0 - RevolveCommand always records
// both explicitly, so this only matters for a HistoryRecord saved before
// this pair of options existed). A genuinely partial angle and/or nonzero
// start_angle routes through kernel::Brep::Revolve (brep.h), which has
// supported both since before the app exposed either; a plain full
// 0..2*pi revolve (angle default, start_angle 0 - the overwhelmingly
// common case and the only one this command has ever built) is left on
// the exact same pre-existing ON_BrepRevSurface construction, untouched.
std::optional<SceneObject> RebuildRevolve(CommandContext&, const kernel::NurbsCurve& kc, const HistoryRecord& rec) {
  auto num = [&](const char* k) { auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const Point3d a(num("ax"), num("ay"), num("az")), b(num("bx"), num("by"), num("bz"));
  if ((b - a).Length() <= 0) return std::nullopt;
  const double angle = rec.num.count("angle") ? num("angle") : 2.0 * ON_PI;
  const double start_angle = num("start_angle");
  if (std::fabs(angle - 2.0 * ON_PI) > 1e-12 || start_angle != 0.0) {
    Vector3d axis = b - a;
    if (!axis.Unitize()) return std::nullopt;
    try {
      return SceneObject::MakeBrep(kernel::Brep::Revolve(kc, a, axis, angle, /*cap=*/true, start_angle));
    } catch (const std::invalid_argument&) {
      // The kernel refuses this combination (e.g. a closed profile
      // touching the axis along more than one sub-arc) - fail honestly
      // rather than silently substituting an unrequested full revolve.
      return std::nullopt;
    }
  }
  ON_RevSurface* rs = ON_RevSurface::New();
  rs->m_curve = new ON_NurbsCurve(kc.raw());
  rs->m_axis = ON_Line(a, b);
  rs->m_angle = ON_Interval(0, 2 * ON_PI);
  rs->m_t = rs->m_curve->Domain();
  ON_Brep* brep = ON_BrepRevSurface(rs, true, true);
  if (!brep) return std::nullopt;
  return SceneObject::MakeBrep(WrapBrepForHistory(brep));
}

// ExtrudeSrf's construction for ONE surface or polysurface - shared by the
// live command and UpdateHistory the same way RebuildExtrude is. A surface is
// wrapped into a one-face Brep and its face extruded; a polysurface has every
// face extruded (so a box extrudes into the union of its face prisms exactly as
// the command always has).
std::optional<SceneObject> RebuildExtrudeSrf(CommandContext&, const SceneObject& src, const HistoryRecord& rec) {
  auto num = [&](const char* k) { auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const Vector3d v(num("vx"), num("vy"), num("vz"));
  const Point3d shift(num("shiftx"), num("shifty"), num("shiftz"));
  const bool solid = num("solid") != 0.0;
  if (v.Length() <= 0) return std::nullopt;
  ON_Brep* b = nullptr;
  if (src.kind == ObjectKind::Surface && src.surface) {
    b = ON_Brep::New();
    ON_NurbsSurface* srf = new ON_NurbsSurface(src.surface->raw());
    b->Create(srf);
  } else if (src.kind == ObjectKind::Brep && src.brep) {
    b = new ON_Brep(src.brep->raw());
  } else {
    return std::nullopt;
  }
  if (shift != Point3d(0, 0, 0)) b->Translate(shift);
  ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
  const int nf = src.kind == ObjectKind::Surface ? 1 : b->m_F.Count();
  bool ok = nf > 0;
  for (int f = 0; f < nf && ok; ++f) ok = ON_BrepExtrudeFace(*b, f, path, solid) >= 0;
  if (!ok) { delete b; return std::nullopt; }
  kernel::Brep k = WrapBrepForHistory(b);
  OrientOutward(k);
  return SceneObject::MakeBrep(k);
}

// PlanarSrf's construction for ONE curve: the trimmed plane bounded by a
// closed, planar, non-self-intersecting curve. nullopt for anything else.
std::optional<SceneObject> RebuildPlanarSrf(CommandContext& ctx, const kernel::NurbsCurve& curve, const HistoryRecord&) {
  ON_Plane pl;
  if (!curve.raw().IsClosed() || !curve.raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance)) return std::nullopt;
  if (CurveSelfIntersects(curve, std::max(ctx.Settings().absolute_tolerance, 1e-9))) return std::nullopt;
  ON_Brep* b = ON_BrepTrimmedPlane(pl, curve.raw());
  if (!b) return std::nullopt;
  return SceneObject::MakeBrep(WrapBrepForHistory(b));
}

// ---------------------------------------------------------------------------
// Loft through NURBS curves: the kernel's exact interpolating Brep::Loft.
//
// The app's original Loft sampled every section at 24 points and handed the
// samples to NurbsSurface::FromControlGrid as CONTROL points - a fit, not an
// interpolation (the surface passes through the first and last section only
// at their ends, never through an intermediate section), a closed first curve
// came back as a MESH (Mesh::LoftClosedRings - capped, so Volume worked, but
// not a B-rep and not through the sections), no section was reversed or
// re-seamed to match its neighbour (circles with different seams or
// directions twisted the surface), a mixed open/closed list divided the
// samples wrongly, and even the control grid itself was handed over in the
// wrong index order (see LoftControlGridFit below). Brep::Loft interpolates: every section lies
// exactly on the surface (brep.h's doc comment on Loft). What it does NOT do
// is orient or seam-align its inputs ("this does not re-orient or re-seam
// them"), so this does, before the call:
//   - an open section runs the way its predecessor does (reversed when that
//     brings its two ends closer to the predecessor's ends),
//   - a closed section circulates the way its predecessor does (reversed when
//     its loop normal opposes the predecessor's) and its seam is moved to the
//     point nearest the predecessor's start,
//   - a list mixing open and closed curves is REFUSED with the reason printed
//     (a closed and an open curve have no point-for-point correspondence).
// Style=Straight is degree 1 in the loft direction (a ruled loft, sharp at
// every section); Normal is cubic (degree - 1 for fewer sections). Closed
// sections are capped (Cap=Yes, the default, like Pipe's Cap and Extrude's
// Solid) into a polysurface solid when the end curves are planar, or left as
// a periodic tube surface (Cap=No, or caps the kernel cannot make - printed).
// The result is then MEASURED against every input section (the isocurve of
// the surface at that section's station vs the section) and a result that
// misses any section by more than 1e-6 of its size is discarded, not
// returned. Any kernel refusal keeps the older control-grid construction,
// with the reason printed. SubDLoft is untouched: it still lofts through the
// same control grid/rings (cmd_solids.cpp's SubD output kind has no use for
// an exact NURBS surface).
// ---------------------------------------------------------------------------
namespace {

// Direction of travel of a closed curve, as the Newell normal of its sampled
// polygon (sign flips when the curve is reversed).
Vector3d LoopNormal(const kernel::NurbsCurve& c) {
  const std::vector<Point3d> p = sectionfit::SamplePoints(c, 48);
  Vector3d n(0, 0, 0);
  for (size_t i = 0; i < p.size(); ++i) {
    const Point3d& a = p[i];
    const Point3d& b = p[(i + 1) % p.size()];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  return n;
}

struct AlignedSections {
  std::vector<kernel::NurbsCurve> curves;
  int reversed = 0, reseamed = 0;
  std::string refusal;
};

AlignedSections AlignLoftSections(const std::vector<const kernel::NurbsCurve*>& in) {
  AlignedSections out;
  const bool first_closed = in.front()->IsClosed();
  for (size_t k = 0; k < in.size(); ++k) {
    if (in[k]->IsClosed() != first_closed) {
      out.refusal = "curve " + std::to_string(k + 1) + " is " + (first_closed ? "open" : "closed") + " but curve 1 is " + (first_closed ? "closed" : "open") +
                    " - a loft needs all open or all closed curves (an open and a closed curve have no point-for-point correspondence)";
      return out;
    }
  }
  for (size_t k = 0; k < in.size(); ++k) {
    kernel::NurbsCurve c = *in[k];
    if (k > 0) {
      const kernel::NurbsCurve& prev = out.curves.back();
      const double size = std::max(sectionfit::CurveSize(c), 1e-12);
      if (!first_closed) {
        const Point3d ps = prev.PointAt(prev.Domain().min), pe = prev.PointAt(prev.Domain().max);
        const Point3d cs = c.PointAt(c.Domain().min), ce = c.PointAt(c.Domain().max);
        if (ps.DistanceTo(ce) + pe.DistanceTo(cs) < ps.DistanceTo(cs) + pe.DistanceTo(ce) - 1e-9 * size && c.Reverse() == kernel::Result::Ok) ++out.reversed;
      } else {
        const Vector3d np = LoopNormal(prev), nc = LoopNormal(c);
        if (np.Length() > 1e-12 * size * size && nc.Length() > 1e-12 * size * size && ON_DotProduct(np, nc) < 0.0 && c.Reverse() == kernel::Result::Ok) ++out.reversed;
        const double t = sectionfit::SnapToKnot(c, c.ClosestPointParameter(prev.PointAt(prev.Domain().min), 400));
        if (c.PointAt(t).DistanceTo(c.PointAt(c.Domain().min)) > 1e-9 * size && c.ChangeSeam(t) == kernel::Result::Ok) ++out.reseamed;
      }
    }
    out.curves.push_back(std::move(c));
  }
  return out;
}

// The loft surface of the built Brep (face 0 is always the wall; capped
// results append their cap faces after it).
std::optional<kernel::NurbsSurface> LoftWall(const kernel::Brep& b) {
  if (b.raw().m_F.Count() < 1) return std::nullopt;
  const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(b.raw().m_F[0].SurfaceOf());
  if (!ns) return std::nullopt;
  kernel::NurbsSurface k;
  k.raw() = *ns;
  return k;
}

// Largest deviation of any section from the isocurve of `wall` at that
// section's own station. The station (v) is read off the surface by closest
// point from several points of the section and must agree between them.
double MeasureLoftSections(const kernel::NurbsSurface& wall, const std::vector<kernel::NurbsCurve>& sections, bool& stations_agree) {
  stations_agree = true;
  double worst = 0.0;
  for (const kernel::NurbsCurve& s : sections) {
    const std::vector<Point3d> probes = sectionfit::SamplePoints(s, 5);
    std::vector<double> vs;
    for (const Point3d& p : probes) vs.push_back(wall.ClosestPointParameter(p).y);
    std::sort(vs.begin(), vs.end());
    const double v = vs[vs.size() / 2];
    const kernel::Interval dv = wall.Domain(1);
    if (vs.back() - vs.front() > 1e-6 * (dv.max - dv.min)) stations_agree = false;
    worst = std::max(worst, sectionfit::CurveDeviation(s, wall.IsoCurve(0, std::min(std::max(v, dv.min), dv.max))));
  }
  return worst;
}

// The original construction: each section sampled at 24 points and used as a
// row of CONTROL points (a fit through the samples' control polygon, NOT an
// interpolation). Kept as the fail-open path for Loft and as SubDLoft's
// construction. `closed` selects the ring form (closed first curve: 24
// samples around the loop, which feeds Mesh::LoftClosedRings).
//
// The control grid is handed to FromControlGrid in its documented index order
// (control_grid[u * v_count + v], u along a section, v along the loft). The
// original appended curve after curve (curve-major), which that layout reads
// as u running THROUGH the sections and v hopping 3 samples at a time along
// whichever section it lands on: a thin ribbon winding along the three
// sections' samples in turn, not a surface spanning them (measured on three
// 10-long lines whose true sheet has area 147.9: the old result had area 6.7
// and missed the lines' ends by 0.88). Row-major here is the same fit, laid
// out correctly.
std::optional<SceneObject> LoftControlGridFit(const std::vector<const kernel::NurbsCurve*>& curves, const HistoryRecord& rec) {
  const bool subd = rec.command == "SubDLoft";
  const int n = 24;
  const int count = static_cast<int>(curves.size());
  std::vector<std::vector<Point3d>> samples;
  const bool closed = curves.front()->IsClosed();
  for (const kernel::NurbsCurve* c : curves) {
    kernel::Interval d = c->Domain();
    std::vector<Point3d> row;
    for (int i = 0; i < n; ++i) row.push_back(c->PointAt(d.min + (d.max - d.min) * i / (closed ? n : n - 1.0)));
    samples.push_back(std::move(row));
  }
  if (closed) {
    kernel::Mesh m = kernel::Mesh::LoftClosedRings(samples);
    if (subd) return SceneObject::MakeSubD(kernel::SubD::FromControlMesh(m));
    return SceneObject::MakeMesh(m);
  }
  std::vector<Point3d> grid(static_cast<size_t>(n) * static_cast<size_t>(count));
  for (int k = 0; k < count; ++k)
    for (int i = 0; i < n; ++i) grid[static_cast<size_t>(i) * static_cast<size_t>(count) + static_cast<size_t>(k)] = samples[static_cast<size_t>(k)][static_cast<size_t>(i)];
  const int vdeg = rec.straight ? 1 : std::min(3, count - 1);
  return SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, n, count, 3, vdeg));
}

}  // namespace

// Loft/SubDLoft's construction - shared the same way as RebuildExtrude
// above. `rec.command == "SubDLoft"` selects the SubD/mesh output kind;
// `rec.straight` is Style=Straight; `rec.num["cap"]` is Cap=Yes/No (1/0;
// absent means Yes, the command's default). A Loft through NURBS curves
// calls the kernel's exact interpolating Brep::Loft (see the block comment
// above). nullopt (with a warning printed) means the curves cannot be
// lofted at all - a mix of open and closed ones; any kernel refusal, or a
// result that misses a section, instead falls through (reason printed) to the
// control-grid fit, so a built object is returned in every other case.
std::optional<SceneObject> RebuildLoft(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& curves, const HistoryRecord& rec) {
  if (curves.size() < 2) return std::nullopt;
  if (rec.command == "SubDLoft") return LoftControlGridFit(curves, rec);

  AlignedSections al = AlignLoftSections(curves);
  if (!al.refusal.empty()) {
    ctx.Warn("Loft: " + al.refusal);
    return std::nullopt;
  }
  const bool want_cap = rec.num.count("cap") ? rec.num.at("cap") != 0.0 : true;
  const bool closed_sections = al.curves.front().IsClosed();
  const int degree = rec.straight ? 1 : std::min(3, static_cast<int>(curves.size()) - 1);
  try {
    kernel::Brep b;
    bool capped = false;
    std::string cap_note;
    if (closed_sections && want_cap) {
      try {
        b = kernel::Brep::Loft(al.curves, degree, /*closed=*/false, /*cap=*/true);
        capped = true;
      } catch (const std::invalid_argument& ex) {
        cap_note = std::string(" (caps not made - ") + ex.what() + ")";
        b = kernel::Brep::Loft(al.curves, degree, /*closed=*/false, /*cap=*/false);
      }
    } else {
      b = kernel::Brep::Loft(al.curves, degree, /*closed=*/false, /*cap=*/false);
    }
    std::optional<kernel::NurbsSurface> wall = LoftWall(b);
    if (!wall) throw std::runtime_error("the kernel loft has no surface face");
    bool agree = true;
    const double dev = MeasureLoftSections(*wall, al.curves, agree);
    double biggest = 0.0;
    for (const kernel::NurbsCurve& s : al.curves) biggest = std::max(biggest, sectionfit::CurveSize(s));
    const double limit = 1e-6 * std::max(biggest, 1e-9);
    if (!agree || !(dev <= limit)) {
      throw std::runtime_error(std::string("the lofted surface misses an input section by ") + sectionfit::Sci(dev) + " (limit " + sectionfit::Sci(limit) + ")" +
                               (agree ? "" : ", or a section's station on it is ambiguous"));
    }
    std::string msg = "Loft: exact interpolating kernel loft through " + std::to_string(curves.size()) + " curves (degree " + std::to_string(degree) + " in the loft direction); every section reproduced, max deviation " +
                      sectionfit::Sci(dev);
    if (al.reversed || al.reseamed) msg += "; aligned first: " + std::to_string(al.reversed) + " curve(s) reversed, " + std::to_string(al.reseamed) + " seam(s) moved";
    if (closed_sections) msg += capped ? "; end caps added (Cap=Yes), a closed polysurface" : (want_cap ? "" : "; uncapped (Cap=No)");
    ctx.Print(msg + cap_note);
    if (capped) {
      SceneObject solid = SceneObject::MakeBrep(std::move(b));
      // The capped B-rep is exact, but Volume/booleans measure its display
      // mesh, and the app's mesher does not always close the kernel's fan
      // caps (a square profile's cap fails 9 CDT constraints: Volume reads 0
      // and the solid is reported "not closed" - the same pre-existing gap
      // extrude_to_point_solid_script.txt documents for ExtrudeCrvToPoint).
      // Say so rather than leave it a surprise.
      const std::optional<kernel::Mesh> display = MeshOf(solid, 0.005);
      if (!display || !display->IsClosedManifold()) {
        ctx.Print("Loft: note - the capped solid is an exact closed B-rep, but this app's display mesher does not close this profile's caps, so Volume and boolean tools will treat it as open");
      }
      return solid;
    }
    return SceneObject::MakeSurface(*wall);
  } catch (const std::exception& ex) {
    ctx.Print(std::string("Loft: exact kernel loft not used (") + ex.what() + "); using the older control-point fit instead");
    return LoftControlGridFit(curves, rec);
  }
}

namespace {

kernel::Brep WrapBrep(ON_Brep* b) {
  kernel::Brep k;
  if (b) {
    k.raw() = *b;
    delete b;
  }
  return k;
}

void AddBrep(CommandContext& ctx, ON_Brep* b, const char* label) {
  if (!b) { ctx.Warn(std::string(label) + " failed"); return; }
  AddObject(ctx, SceneObject::MakeBrep(WrapBrep(b)), label);
}

kernel::Mesh CubeMesh(Point3d a, Point3d b) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  const double x[2] = {std::min(a.x, b.x), std::max(a.x, b.x)}, y[2] = {std::min(a.y, b.y), std::max(a.y, b.y)}, z[2] = {std::min(a.z, b.z), std::max(a.z, b.z)};
  for (int k = 0; k < 2; ++k) for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) r.SetVertex(k * 4 + j * 2 + i, ON_3dPoint(x[i], y[j], z[k]));
  const int f[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (int i = 0; i < 6; ++i) r.SetQuad(i, f[i][0], f[i][1], f[i][2], f[i][3]);
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

// Box: two corners on the CPlane, then height.
class BoxCommand : public Command {
 public:
  enum class Out { Brep, Mesh, SubD };
  explicit BoxCommand(Out out) : out_(out) {}
  void Begin(CommandContext&) override { WantPoint("First corner of base"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) WantPoint("Other corner of base");
    else if (pts_.size() == 2) WantPoint("Height. Press Enter for a cube");
    else Build(ctx, HeightFrom(ctx, p));
  }
  void OnNumber(CommandContext& ctx, double v) override { if (pts_.size() == 2) Build(ctx, v); }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  // A typed height is a literal number (this->OnNumber above), never a
  // distance-toward-the-cursor point to re-derive height from by
  // projection - see the comment on Command::NumberIsLiteralValue().
  bool NumberIsLiteralValue() const override { return true; }
  void OnEnter(CommandContext& ctx) override {
    if (pts_.size() == 2) Build(ctx, std::min((pts_[1] - pts_[0]).Length(), std::fabs(pts_[1].x - pts_[0].x) > 0 ? std::fabs(pts_[1].x - pts_[0].x) : (pts_[1] - pts_[0]).Length()));
  }
  double HeightFrom(CommandContext& ctx, Point3d p) { return ON_DotProduct(p - pts_[1], ActiveNormal(ctx)); }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    if (pts_.size() == 1) PreviewBase(ctx, pts_[0], h, 0);
    else if (pts_.size() == 2) PreviewBase(ctx, pts_[0], pts_[1], HeightFrom(ctx, h));
  }
  void PreviewBase(CommandContext& ctx, Point3d a, Point3d b, double h) {
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(a, &u0, &v0);
    pl.ClosestPointTo(b, &u1, &v1);
    std::vector<Point3d> base = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)};
    ctx.AddPreviewPolyline(base, true);
    if (h != 0) {
      std::vector<Point3d> top;
      for (const Point3d& p : base) { top.push_back(p + pl.zaxis * h); ctx.AddPreviewLine(p, p + pl.zaxis * h); }
      ctx.AddPreviewPolyline(top, true);
    }
  }
  void Build(CommandContext& ctx, double h) {
    ctx.ClearPreview();
    if (h == 0) { ctx.Warn("Height must be non-zero"); Finish(); return; }
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts_[0], &u0, &v0);
    pl.ClosestPointTo(pts_[1], &u1, &v1);
    if (u0 > u1) std::swap(u0, u1);
    if (v0 > v1) std::swap(v0, v1);
    if (h < 0) { pl.SetOrigin(pl.origin + pl.zaxis * h); h = -h; }
    if (out_ == Out::Brep && pl.zaxis.IsParallelTo(ON_zaxis) != 0 && std::fabs(pl.origin.z) >= 0) {
      Point3d c0 = pl.PointAt(u0, v0), c1 = pl.PointAt(u1, v1) + pl.zaxis * h;
      ON_3dPoint corners[8] = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1),
                               pl.PointAt(u0, v0) + pl.zaxis * h, pl.PointAt(u1, v0) + pl.zaxis * h, pl.PointAt(u1, v1) + pl.zaxis * h, pl.PointAt(u0, v1) + pl.zaxis * h};
      (void)c0; (void)c1;
      AddBrep(ctx, ON_BrepBox(corners), "Box");
    } else if (out_ == Out::Brep) {
      ON_3dPoint corners[8] = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1),
                               pl.PointAt(u0, v0) + pl.zaxis * h, pl.PointAt(u1, v0) + pl.zaxis * h, pl.PointAt(u1, v1) + pl.zaxis * h, pl.PointAt(u0, v1) + pl.zaxis * h};
      AddBrep(ctx, ON_BrepBox(corners), "Box");
    } else {
      kernel::Mesh m = CubeMesh(pl.PointAt(u0, v0), pl.PointAt(u1, v1) + pl.zaxis * h);
      if (out_ == Out::Mesh) AddObject(ctx, SceneObject::MakeMesh(m), "MeshBox");
      else AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(m)), "SubDBox");
    }
    Finish();
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Out out_;
  std::vector<Point3d> pts_;
};

// Centre + radius commands (sphere family).
CommandFactory SphereLike(const char* label, std::function<void(CommandContext&, Point3d, double)> build) {
  return [=]() -> std::unique_ptr<Command> {
    auto c = std::make_unique<PointThenDistanceCommand>("Center of sphere", "Radius",
                                                        [=](CommandContext& ctx, Point3d c, double r, Point3d) { if (r > 0) build(ctx, c, r); else ctx.Warn(std::string(label) + ": radius must be positive"); }, 10.0);
    c->preview_ = [](CommandContext& ctx, Point3d c, double r, Point3d) {
      for (int axis = 0; axis < 3; ++axis) {
        ON_Plane pl(c, axis == 0 ? ON_zaxis : axis == 1 ? ON_xaxis : ON_yaxis);
        ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, r), 48));
      }
    };
    return c;
  };
}

// Ellipsoid: center, then the three semi-axis end points, matching Rhino's
// own Center/End of first axis/End of second axis/End of third axis flow
// (real axis picking, not a fixed 1:0.7:0.5 ratio). The second axis point is
// projected perpendicular to the first so the three axes stay an orthogonal
// frame; the third axis length is the picked point's distance along
// axis1 x axis2 (its position off that plane doesn't otherwise matter).
class EllipsoidCommand : public Command {
 public:
  explicit EllipsoidCommand(bool to_subd) : to_subd_(to_subd) {}
  void Begin(CommandContext&) override { WantPoint("Center of ellipsoid"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (stage_ == 0) { center_ = p; stage_ = 1; WantPoint("End of first axis"); return; }
    if (stage_ == 1) {
      dir1_ = p - center_;
      ra_ = dir1_.Length();
      if (ra_ <= 0 || !dir1_.Unitize()) { ctx.Warn("Ellipsoid: axis must have length"); Finish(); return; }
      stage_ = 2;
      WantPoint("End of second axis");
      return;
    }
    if (stage_ == 2) {
      Vector3d v = p - center_;
      v = v - dir1_ * ON_DotProduct(v, dir1_);  // component perpendicular to axis 1
      rb_ = v.Length();
      if (rb_ <= 0 || !v.Unitize()) { ctx.Warn("Ellipsoid: second axis must be off the first"); Finish(); return; }
      dir2_ = v;
      dir3_ = ON_CrossProduct(dir1_, dir2_);
      dir3_.Unitize();
      stage_ = 3;
      WantPoint("End of third axis");
      return;
    }
    rc_ = std::fabs(ON_DotProduct(p - center_, dir3_));
    Build(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double v) override { if (stage_ == 3) { rc_ = std::fabs(v); Build(ctx); } }
  void Build(CommandContext& ctx) {
    if (rc_ <= 0) { ctx.Warn("Ellipsoid: third axis must have length"); Finish(); return; }
    ON_Xform xf = ON_Xform::IdentityTransformation;
    for (int r = 0; r < 3; ++r) {
      xf.m_xform[r][0] = ra_ * dir1_[r];
      xf.m_xform[r][1] = rb_ * dir2_[r];
      xf.m_xform[r][2] = rc_ * dir3_[r];
      xf.m_xform[r][3] = center_[r];
    }
    if (to_subd_) {
      kernel::Mesh m = kernel::Brep::Sphere(ON_3dPoint::Origin, 1).TessellateToClosedMesh(8, 4);
      m = m.Transform(xf);
      AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(m)), "SubDEllipsoid");
    } else if (ON_Brep* b = ON_BrepSphere(ON_Sphere(ON_3dPoint::Origin, 1))) {
      b->Transform(xf);
      AddBrep(ctx, b, "Ellipsoid");
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    if (stage_ == 1) { ctx.AddPreviewLine(center_, h); return; }
    if (stage_ >= 2) ctx.AddPreviewLine(center_, center_ + dir1_ * ra_);
    if (stage_ == 2) ctx.AddPreviewLine(center_, h);
    if (stage_ == 3) ctx.AddPreviewLine(center_, center_ + dir2_ * rb_);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  bool to_subd_;
  int stage_ = 0;
  Point3d center_;
  Vector3d dir1_, dir2_, dir3_;
  double ra_ = 0, rb_ = 0, rc_ = 0;
};

// Base circle then height (cylinder / cone / tube...).
class CylinderCommand : public Command {
 public:
  enum class Kind { Cylinder, Cone, TCone, Tube, Pyramid, MeshCylinder, MeshCone, SubDCylinder, SubDCone };
  explicit CylinderCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override {
    if (kind_ == Kind::Pyramid) options = {{"NumSides", std::to_string(sides_), {}, true, false}};
    WantPoint("Base of " + Name());
  }
  std::string Name() const {
    switch (kind_) { case Kind::Cylinder: return "cylinder"; case Kind::Cone: return "cone"; case Kind::TCone: return "truncated cone"; case Kind::Tube: return "tube"; case Kind::Pyramid: return "pyramid"; case Kind::MeshCylinder: return "mesh cylinder"; case Kind::MeshCone: return "mesh cone"; case Kind::SubDCylinder: return "SubD cylinder"; default: return "SubD cone"; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (stage_ == 0) { base_ = p; stage_ = 1; WantPoint("Radius"); }
    else if (stage_ == 1) { OnNumber(ctx, (p - base_).Length()); }
    else if (stage_ == 2 && kind_ == Kind::Tube) { OnNumber(ctx, (p - base_).Length()); }
    else { OnNumber(ctx, ON_DotProduct(p - base_, ActiveNormal(ctx))); }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n != "NumSides") return;
    int s = std::atoi(v.c_str());
    if (s < 3) { ctx.Warn("NumSides must be at least 3"); return; }
    sides_ = s;
    options[0].value = std::to_string(sides_);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (stage_ == 1) { radius_ = std::fabs(v); if (radius_ <= 0) return; stage_ = 2; WantPoint(kind_ == Kind::Tube ? "Second radius" : "End of " + Name()); }
    else if (stage_ == 2 && kind_ == Kind::Tube) { radius2_ = std::fabs(v); stage_ = 3; WantPoint("End of tube"); }
    else if (stage_ == 2 && kind_ == Kind::TCone) { height_ = v; stage_ = 3; WantPoint("Radius at end"); }
    else if (stage_ == 3 && kind_ == Kind::TCone) { radius2_ = std::fabs(v); Build(ctx); }
    else { height_ = v; Build(ctx); }
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(base_);
    if (stage_ == 1) { ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, (h - base_).Length()))); ctx.AddPreviewLine(base_, h); }
    else if (stage_ >= 2) {
      double hh = ON_DotProduct(h - base_, pl.zaxis);
      ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, radius_)));
      ON_Plane top = pl; top.SetOrigin(base_ + pl.zaxis * hh);
      double rt = (kind_ == Kind::Cone || kind_ == Kind::MeshCone || kind_ == Kind::SubDCone || kind_ == Kind::Pyramid) ? 0.0 : radius_;
      if (rt > 0) ctx.AddPreviewPolyline(CirclePoints(ON_Circle(top, rt)));
      for (int i = 0; i < 4; ++i) { double a = i * ON_PI / 2; ctx.AddPreviewLine(pl.PointAt(radius_ * std::cos(a), radius_ * std::sin(a)), top.PointAt(rt * std::cos(a), rt * std::sin(a))); }
    }
  }
  void Build(CommandContext& ctx) {
    ctx.ClearPreview();
    if (height_ == 0) { ctx.Warn("Height must be non-zero"); Finish(); return; }
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(base_);
    Vector3d axis = pl.zaxis;
    double h = height_;
    if (h < 0) { pl = ON_Plane(base_, -pl.zaxis); axis = pl.zaxis; h = -h; }
    switch (kind_) {
      case Kind::Cylinder: {
        ON_Cylinder cyl(ON_Circle(pl, radius_), h);
        AddBrep(ctx, ON_BrepCylinder(cyl, true, true), "Cylinder");
        break;
      }
      case Kind::Cone: {
        ON_Cone cone;
        cone.Create(pl, h, radius_);
        AddBrep(ctx, ON_BrepCone(cone, true), "Cone");
        break;
      }
      case Kind::TCone: {
        // Revolve a trapezoid profile.
        ON_Plane axis_plane(base_, pl.xaxis, pl.zaxis);
        ON_Polyline profile;
        profile.Append(base_ + pl.xaxis * radius_);
        profile.Append(base_ + pl.xaxis * radius2_ + pl.zaxis * h);
        ON_PolylineCurve* pc = new ON_PolylineCurve(profile);
        ON_RevSurface* rs = ON_RevSurface::New();
        rs->m_curve = pc;
        rs->m_axis = ON_Line(base_, base_ + pl.zaxis);
        rs->m_angle = ON_Interval(0, 2 * ON_PI);
        rs->m_t = pc->Domain();
        AddBrep(ctx, ON_BrepRevSurface(rs, true, true), "TCone");
        break;
      }
      case Kind::Tube: {
        double r_out = std::max(radius_, radius2_), r_in = std::min(radius_, radius2_);
        if (r_in <= 0 || r_in >= r_out) { ctx.Warn("Tube needs two different positive radii"); break; }
        ON_Polyline profile;
        profile.Append(base_ + pl.xaxis * r_in);
        profile.Append(base_ + pl.xaxis * r_out);
        profile.Append(base_ + pl.xaxis * r_out + pl.zaxis * h);
        profile.Append(base_ + pl.xaxis * r_in + pl.zaxis * h);
        profile.Append(base_ + pl.xaxis * r_in);
        ON_PolylineCurve* pc = new ON_PolylineCurve(profile);
        ON_RevSurface* rs = ON_RevSurface::New();
        rs->m_curve = pc;
        rs->m_axis = ON_Line(base_, base_ + pl.zaxis);
        rs->m_angle = ON_Interval(0, 2 * ON_PI);
        rs->m_t = pc->Domain();
        AddBrep(ctx, ON_BrepRevSurface(rs, false, false), "Tube");
        break;
      }
      case Kind::Pyramid:
        AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Cone(base_, axis, radius_, h, sides_, 1)), "Pyramid");
        break;
      case Kind::MeshCylinder:
        AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Cylinder(base_, axis, radius_, h)), "MeshCylinder");
        break;
      case Kind::MeshCone:
        AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Cone(base_, axis, radius_, h)), "MeshCone");
        break;
      case Kind::SubDCylinder:
        AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Mesh::Cylinder(base_, axis, radius_, h, 16, 4))), "SubDCylinder");
        break;
      case Kind::SubDCone:
        AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Mesh::Cone(base_, axis, radius_, h, 16, 4))), "SubDCone");
        break;
    }
    Finish();
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  int stage_ = 0;
  int sides_ = 4;  // Pyramid only: NumSides= option
  Point3d base_;
  double radius_ = 0, radius2_ = 0, height_ = 0;
};

class TorusCommand : public Command {
 public:
  enum class Kind { Brep, Mesh, SubD };
  explicit TorusCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override { WantPoint("Center of torus"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (stage_ == 0) { center_ = p; stage_ = 1; WantPoint("Major radius"); }
    else OnNumber(ctx, stage_ == 1 ? (p - center_).Length() : std::fabs((p - center_).Length() - major_));
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (stage_ == 1) { major_ = std::fabs(v); stage_ = 2; WantPoint("Minor radius"); return; }
    double minor = std::fabs(v);
    ctx.ClearPreview();
    if (minor <= 0 || major_ <= 0) { ctx.Warn("Radii must be positive"); Finish(); return; }
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(center_);
    if (kind_ == Kind::Brep) {
      ON_Torus t(pl, major_, minor);
      AddBrep(ctx, ON_BrepTorus(t), "Torus");
    } else if (kind_ == Kind::Mesh) {
      AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Torus(center_, pl.zaxis, major_, minor)), "MeshTorus");
    } else {
      AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Mesh::Torus(center_, pl.zaxis, major_, minor, 16, 8))), "SubDTorus");
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(center_);
    if (stage_ == 1) ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, (h - center_).Length())));
    else if (stage_ == 2) {
      double minor = std::fabs((h - center_).Length() - major_);
      ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, major_ + minor)));
      ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, std::max(0.0, major_ - minor))));
    }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  int stage_ = 0;
  Point3d center_;
  double major_ = 0;
};

// Extrude selected curves along the CPlane normal by a typed or picked distance.
class ExtrudeCommand : public Command {
 public:
  enum class Kind { Curve, Surface, ToPoint };
  explicit ExtrudeCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override {
    WantObjects(kind_ == Kind::Surface ? "Select surfaces to extrude" : "Select curves to extrude");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (kind_ == Kind::Surface ? (o->kind == ObjectKind::Surface || o->kind == ObjectKind::Brep) : o->kind == ObjectKind::Curve) ids_.push_back(id);
    }
    if (ids_.empty()) { ctx.Warn("Nothing suitable selected"); Finish(); return; }
    ctx.Doc().BoundingBoxOf(ids_, bbox_);
    if (kind_ == Kind::ToPoint) WantPoint("Point to extrude to");
    else WantPoint("Extrusion distance");
    options = {{"BothSides", "No", {"Yes", "No"}, false, true}, {"Solid", "Yes", {"Yes", "No"}, false, true}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string&) override {
    if (n == "BothSides") { both_ = !both_; options[0].value = both_ ? "Yes" : "No"; }
    if (n == "Solid") { solid_ = !solid_; options[1].value = solid_ ? "Yes" : "No"; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (kind_ == Kind::ToPoint) { BuildToPoint(ctx, p); return; }
    Point3d c((bbox_.min.x + bbox_.max.x) / 2, (bbox_.min.y + bbox_.max.y) / 2, (bbox_.min.z + bbox_.max.z) / 2);
    OnNumber(ctx, ON_DotProduct(p - c, ActiveNormal(ctx)));
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double d) override {
    ctx.ClearPreview();
    if (d == 0) { ctx.Warn("Distance must be non-zero"); Finish(); return; }
    Vector3d n = ActiveNormal(ctx);
    ctx.Doc().BeginChange("Extrude");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      Vector3d v = n * d;
      Point3d shift(0, 0, 0);
      if (both_) { v = n * (2 * d); shift = Point3d(0, 0, 0) - n * d; }
      if (o->kind == ObjectKind::Curve) {
        if (ExtrudeCurve(ctx, id, *o->curve, v, shift)) ++made;
      } else if (o->kind == ObjectKind::Surface || o->kind == ObjectKind::Brep) {
        // Delegates to RebuildExtrudeSrf so UpdateHistory re-runs the same
        // construction when the source surface/polysurface changes.
        HistoryRecord rec;
        rec.command = "ExtrudeSrf";
        rec.num = {{"vx", v.x}, {"vy", v.y}, {"vz", v.z}, {"shiftx", shift.x}, {"shifty", shift.y}, {"shiftz", shift.z}, {"solid", solid_ ? 1.0 : 0.0}};
        std::optional<SceneObject> built = RebuildExtrudeSrf(ctx, *o, rec);
        if (built) {
          const ObjectId new_id = ctx.Doc().Add(std::move(*built));
          ctx.Doc().SetProvenance(new_id, id, ProvenanceKind::ExtrusionRail);
          RecordHistoryIfEnabled(ctx, new_id, "ExtrudeSrf", {id}, rec.num);
          ++made;
        }
      }
    }
    ctx.Print("Extruded " + std::to_string(made) + " object(s)");
    Finish();
  }
  // `source_id` is the profile curve's ObjectId, recorded as this
  // extrusion's provenance parent (doc/Document.h's ProvenanceInfo) on
  // whichever object actually gets built below, so SelExtrusion/SelParents/
  // SelChildren (cmd_select2.cpp) can find the real link - and, once the
  // source curve is deleted, gracefully find nothing instead of crashing.
  // Delegates the actual construction to the free RebuildExtrude function
  // (defined above RegisterSolidCommands' anonymous namespace) so
  // UpdateHistory (cmd_history.cpp) can later re-run the exact same
  // construction against the source curve's current geometry - see
  // history_rebuild.h's comment. `v`/`shift` are recorded as this
  // extrusion's HistoryRecord parameters (History On only) alongside
  // `source_id` as this extrusion's provenance parent (doc/Document.h's
  // ProvenanceInfo), so SelExtrusion/SelParents/SelChildren
  // (cmd_select2.cpp) can find the real link - and, once the source curve
  // is deleted, gracefully find nothing instead of crashing.
  bool ExtrudeCurve(CommandContext& ctx, ObjectId source_id, const kernel::NurbsCurve& kc, Vector3d v, Point3d shift) {
    kernel::NurbsCurve for_check;
    ON_NurbsCurve check_c = kc.raw();
    if (shift != Point3d(0, 0, 0)) check_c.Translate(shift);
    if (CurveFromON(check_c, for_check) && CurveSelfIntersects(for_check, std::max(ctx.Settings().absolute_tolerance, 1e-9)))
      ctx.Warn("Extrude: the selected curve crosses itself - building an open surface instead of a solid cap");
    HistoryRecord rec;
    rec.command = "Extrude";
    rec.num = {{"vx", v.x}, {"vy", v.y}, {"vz", v.z}, {"shiftx", shift.x}, {"shifty", shift.y}, {"shiftz", shift.z}, {"solid", solid_ ? 1.0 : 0.0}};
    std::optional<SceneObject> built = RebuildExtrude(ctx, kc, rec);
    if (!built) return false;
    const ObjectId new_id = ctx.Doc().Add(std::move(*built));
    ctx.Doc().SetProvenance(new_id, source_id, ProvenanceKind::ExtrusionRail);
    RecordHistoryIfEnabled(ctx, new_id, "Extrude", {source_id}, rec.num);
    return true;
  }
  void BuildToPoint(CommandContext& ctx, Point3d apex) {
    ctx.ClearPreview();
    ctx.Doc().BeginChange("ExtrudeCrvToPoint");
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Curve) continue;
      HistoryRecord rec;
      rec.command = "ExtrudeCrvToPoint";
      rec.num = {{"ax", apex.x}, {"ay", apex.y}, {"az", apex.z}, {"solid", solid_ ? 1.0 : 0.0}};
      std::optional<SceneObject> built = RebuildExtrudeToPoint(ctx, *o->curve, rec);
      if (!built) continue;
      const ObjectId new_id = ctx.Doc().Add(std::move(*built));
      ctx.Doc().SetProvenance(new_id, id, ProvenanceKind::ExtrusionRail);
      RecordHistoryIfEnabled(ctx, new_id, "ExtrudeCrvToPoint", {id}, rec.num);
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    Point3d c((bbox_.min.x + bbox_.max.x) / 2, (bbox_.min.y + bbox_.max.y) / 2, (bbox_.min.z + bbox_.max.z) / 2);
    Vector3d v = kind_ == Kind::ToPoint ? (h - c) : ActiveNormal(ctx) * ON_DotProduct(h - c, ActiveNormal(ctx));
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const DisplayCache& d = o->Display();
      for (size_t i = 0; i + 5 < d.lines.size(); i += 6) {
        Point3d a(d.lines[i], d.lines[i + 1], d.lines[i + 2]), b(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]);
        if (kind_ == Kind::ToPoint) ctx.AddPreviewLine(a, h);
        else { ctx.AddPreviewLine(a + v, b + v); ctx.AddPreviewLine(a, a + v); }
      }
    }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  std::vector<ObjectId> ids_;
  kernel::BoundingBox bbox_{};
  bool both_ = false, solid_ = true;
};

// Revolve selected curves around an axis defined by two points. `Output`
// (Surface/SubD, the command catalogue's own documented option - see
// PARITY_MAP.md's "ExtrudeCrv / Revolve producing a SubD object directly")
// defaults to Surface, matching every pre-existing script/test that invokes
// Revolve without it.
class RevolveCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to revolve"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) ids_.push_back(id); }
    if (ids_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
    WantPoint("Start of revolve axis");
    options = {{"Output", output_, {"Surface", "SubD"}, false, false},
               {"Angle", FormatNumber(angle_deg_), {}, true, false},
               {"StartAngle", FormatNumber(start_angle_deg_), {}, true, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "Output") { output_ = v; options[0].value = output_; }
    else if (n == "Angle") {
      char* e; double d = std::strtod(v.c_str(), &e);
      if (e && !*e && d > 0 && d <= 360) { angle_deg_ = d; options[1].value = FormatNumber(d); }
      else ctx.Warn("Angle must be between 0 (exclusive) and 360 degrees");
    } else if (n == "StartAngle") {
      char* e; double d = std::strtod(v.c_str(), &e);
      if (e && !*e) { start_angle_deg_ = d; options[2].value = FormatNumber(d); }
      else ctx.Warn("Start angle must be a number");
    }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("End of revolve axis"); return; }
    if ((p - *a_).Length() <= 0) return;
    ctx.Doc().BeginChange("Revolve");
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      // Delegates to the free RebuildRevolve function so UpdateHistory
      // (cmd_history.cpp) can re-run the exact same revolve against this
      // curve's current geometry later - see history_rebuild.h. The axis
      // (`a_`, `p`) is recorded as-picked and does NOT move on rebuild,
      // same as Rhino's own Revolve history. `angle`/`start_angle` are
      // recorded in degrees-to-radians so a partial-angle revolve (Angle
      // and/or StartAngle, both default to a plain full 0..360 revolve)
      // rebuilds through the same kernel::Brep::Revolve path RebuildRevolve
      // itself picks for a non-default combination - see that function's
      // own comment.
      HistoryRecord rec;
      rec.command = "Revolve";
      rec.num = {{"ax", a_->x}, {"ay", a_->y}, {"az", a_->z}, {"bx", p.x}, {"by", p.y}, {"bz", p.z},
                 {"angle", angle_deg_ * ON_PI / 180.0}, {"start_angle", start_angle_deg_ * ON_PI / 180.0}};
      std::optional<SceneObject> built = RebuildRevolve(ctx, *o->curve, rec);
      if (!built) continue;
      ObjectId new_id = ctx.Doc().Add(std::move(*built));
      if (output_ == "SubD") {
        // Tessellates the just-built Brep of revolution into a genuine SubD
        // control net - the same ConvertObjectToSubD construction
        // SubDRevolve (cmd_subd.cpp) already uses and tests. History is
        // deliberately not recorded for this branch: RebuildOneHistoryObject
        // (cmd_history.cpp)'s own "Revolve" case always calls RebuildRevolve,
        // which only ever returns a Brep - a later UpdateHistory would
        // silently rebuild this object back into a Brep, losing its SubD-
        // ness, the same honest scope limit SubDLoft's own Join option
        // already discloses for History.
        const ObjectId subd_id = ConvertObjectToSubD(ctx, new_id);
        if (subd_id != kNoObject) new_id = subd_id;
        continue;
      }
      RecordHistoryIfEnabled(ctx, new_id, "Revolve", {id}, rec.num);
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
  std::string output_ = "Surface";
  double angle_deg_ = 360.0, start_angle_deg_ = 0.0;
};

// Loft: surface through the selected curves (in selection order), built by
// RebuildLoft (above) - the kernel's exact interpolating Brep::Loft after the
// curves are oriented and seam-aligned. `straight` (Style=Straight) is degree
// 1 in the loft direction: a real ruled loft with a sharp (C0) edge at every
// section, instead of the cubic (degree - 1 for fewer than 4 sections) the
// default (Normal) style interpolates with. Loose/Tight aren't offered: both
// still interpolate every section like Normal and differ from it only in end-
// tangent/fairing detail; claiming them as distinct styles here would not be
// honest, so Straight is the only alternate style. `cap` (Cap=Yes, the default,
// like Pipe's Cap and Extrude's Solid) closes the ends of a loft through
// closed planar curves into a polysurface solid; Cap=No leaves the periodic
// tube surface.
void Loft(CommandContext& ctx, const std::vector<ObjectId>& ids, bool subd, bool straight = false, bool cap = true) {
  std::vector<ObjectId> curve_ids;
  std::vector<const kernel::NurbsCurve*> curves;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (o && o->kind == ObjectKind::Curve) { curve_ids.push_back(id); curves.push_back(o->curve.get()); }
  }
  if (curves.size() < 2) { ctx.Warn("Select at least two curves"); return; }
  ctx.Doc().BeginChange(subd ? "SubDLoft" : "Loft");
  HistoryRecord rec;
  rec.command = subd ? "SubDLoft" : "Loft";
  rec.straight = straight;
  if (!subd) rec.num = {{"cap", cap ? 1.0 : 0.0}};
  std::optional<SceneObject> built = RebuildLoft(ctx, curves, rec);
  if (!built) { ctx.Warn("Loft failed"); return; }
  const ObjectId new_id = ctx.Doc().Add(std::move(*built));
  RecordHistoryIfEnabled(ctx, new_id, rec.command, curve_ids, rec.num, straight);
}

// Pulls "Style=Straight"/"Style=Normal" and "Cap=Yes"/"Cap=No" off the
// command's remaining typed tokens (Loose/Tight aren't offered - see Loft()
// above). A later token of the same option wins.
struct LoftTokens { bool straight = false; bool cap = true; };
LoftTokens ParseLoftTokens(const std::vector<std::string>& tokens) {
  LoftTokens out;
  for (const std::string& t : tokens) {
    std::string s = t;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "style=straight") out.straight = true;
    else if (s == "style=normal") out.straight = false;
    else if (s == "cap=yes" || s == "cap=y") out.cap = true;
    else if (s == "cap=no" || s == "cap=n") out.cap = false;
  }
  return out;
}

// Loft with G0 / G1 / G2 continuity to an adjacent surface edge (Continuity= option, continuity_request.h): the kernel's
// NurbsSurface::LoftMatched on the aligned sections. The end that is matched is the one whose section lies nearer the
// adjacent surface (the first section for a tie). The result is the uncapped wall surface; it is MEASURED along the edge
// by the kernel and printed, and a refusal prints the kernel's reason and builds nothing.
void LoftContinuity(CommandContext& ctx, const std::vector<ObjectId>& ids, const ContinuityRequest& req, const AdjacentEdge& edge, bool straight) {
  std::vector<const kernel::NurbsCurve*> curves;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (o && o->kind == ObjectKind::Curve && o->curve) curves.push_back(o->curve.get());
  }
  const std::string head = std::string("Loft: ") + req.Name() + " continuity to " + edge.Describe();
  if (curves.size() < 2) { ctx.Warn("Loft: select at least two curves; nothing was built"); return; }
  AlignedSections al = AlignLoftSections(curves);
  if (!al.refusal.empty()) { ctx.Warn(head + " refused: " + al.refusal + "; nothing was built"); return; }
  auto worst_to_adjacent = [&](const kernel::NurbsCurve& c) {
    double w = 0.0;
    const kernel::Interval d = c.Domain();
    for (int k = 0; k <= 8; ++k) {
      const Point3d q = c.PointAt(d.min + (d.max - d.min) * k / 8.0);
      w = std::max(w, edge.surface.ClosestPoint(q, 40, 40).DistanceTo(q));
    }
    return w;
  };
  const double d_first = worst_to_adjacent(al.curves.front()), d_last = worst_to_adjacent(al.curves.back());
  const bool at_start = d_first <= d_last;
  const int degree = straight ? 1 : std::min(3, static_cast<int>(curves.size()) - 1);
  kernel::NurbsSurface::LoftEndMatch m;
  m.surface = &edge.surface;
  m.fixed_direction = edge.fixed_direction;
  m.at_min = edge.at_min;
  m.continuity = req.continuity;
  const double tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  kernel::NurbsSurface out;
  kernel::NurbsSurface::LoftMatchReport rep;
  kernel::Result r = kernel::Result::Failed;
  try {
    r = kernel::NurbsSurface::LoftMatched(al.curves, degree, at_start ? &m : nullptr, at_start ? nullptr : &m, tol, out, rep, req.angle_tol, req.curv_tol);
  } catch (const std::exception& e) {
    rep.reason = e.what();
  }
  const char* which = at_start ? "first" : "last";
  if (r != kernel::Result::Ok) {
    ctx.Warn(head + " refused by the kernel (matched end: the " + which + " section, " + Sig3(at_start ? d_first : d_last) + " from the adjacent surface): " +
             (rep.reason.empty() ? std::string("no reason given") : rep.reason) + "; nothing was built");
    return;
  }
  const kernel::NurbsSurface::EdgeContinuityMeasure& m_end = at_start ? rep.start : rep.end;
  ctx.Doc().BeginChange("Loft");
  ctx.Doc().Add(SceneObject::MakeSurface(out));
  ctx.Print(head + ": kernel LoftMatched through " + std::to_string(curves.size()) + " curves (degree " + std::to_string(degree) + " in the loft direction), the " + which +
            " section made identical to the edge and the next rows rewritten by MatchEdge as " + ContinuityWhat(req.continuity) + " continuity; " +
            ContinuityMeasuredNote(req, m_end) + "; cross-derivative scale " + Sig3(at_start ? rep.start_scale : rep.end_scale) +
            " searched to keep the sections on their data (MatchEdge's default scale would have moved a section by " + Sig3(at_start ? rep.start_default_deviation : rep.end_default_deviation) +
            "); worst distance of any section's samples from the finished surface " + Sig3(rep.section_deviation_max) +
            " (document tolerance " + FormatNumber(tol) + (al.reversed ? ", " + std::to_string(al.reversed) + " curve(s) reversed first" : std::string()) + "); uncapped surface");
  NoHistoryNote(ctx, "Loft", req);
}

struct SubDFidelityResult {
  kernel::SubD subd;
  double achieved_deviation = 0.0;
  bool verified = false;
  int rounds = 0;
  int fit_iterations = 0;       // interpolation iterations of the accepted round
  double fit_residual = 0.0;    // worst |cage vertex - limit point| after the fit
  int crease_edges = 0;         // feature edges tagged Crease on the cage
  int samples = 0;              // limit-surface points measured
  int cage_faces = 0;           // faces of the accepted cage
};

constexpr int kMaxFidelityRounds = 3;  // + the initial one: grid divisions 24, 48, 96, 192
constexpr int kMaxFidelitySamples = 1500;
constexpr int kMaxFidelityCageFaces = 300000;
// Cage edges whose two faces differ by more than this are feature edges
// (a cylinder's rim, a filleted box's flats) and become creases, so the
// limit surface keeps them sharp instead of rounding them away.
constexpr double kFidelityCreaseAngleDeg = 35.0;

// Even-odd point-in-face test in a Brep face's (u, v) domain: every loop
// (outer and holes) is sampled to a 2D polyline from its trim curves.
bool UvInsideFaceLoops(const ON_Brep& b, const ON_BrepFace& f, double u, double v) {
  int crossings = 0;
  for (int li = 0; li < f.m_li.Count(); ++li) {
    const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
    std::vector<ON_2dPoint> poly;
    for (int k = 0; k < loop.m_ti.Count(); ++k) {
      const ON_BrepTrim& t = b.m_T[loop.m_ti[k]];
      const ON_Interval d = t.Domain();
      constexpr int kSeg = 24;
      for (int j = 0; j < kSeg; ++j) {
        const ON_3dPoint q = t.PointAt(d.ParameterAt(static_cast<double>(j) / kSeg));
        poly.emplace_back(q.x, q.y);
      }
    }
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
      const ON_2dPoint& a = poly[i];
      const ON_2dPoint& c = poly[(i + 1) % n];
      if ((a.y > v) != (c.y > v)) {
        const double x = a.x + (v - a.y) * (c.x - a.x) / (c.y - a.y);
        if (x > u) ++crossings;
      }
    }
  }
  return (crossings & 1) != 0;
}

// Distance from points to the TRUE Brep surface (not a tessellation of it):
// closest point on each face's own NURBS surface
// (kernel::NurbsSurface::ClosestPointParameter, refined to floating-point
// precision), accepted only where that closest (u, v) lies inside the
// face's trim loops. nullopt if no face produced an accepted point (the
// caller falls back to a mesh distance for that sample).
struct TrueBrepProbe {
  const ON_Brep* brep = nullptr;
  std::vector<kernel::NurbsSurface> surfaces;
  std::vector<char> usable;
  explicit TrueBrepProbe(const ON_Brep& b) : brep(&b), surfaces(static_cast<size_t>(b.m_F.Count())), usable(static_cast<size_t>(b.m_F.Count()), 0) {
    for (int fi = 0; fi < b.m_F.Count(); ++fi) {
      const ON_Surface* srf = b.m_F[fi].SurfaceOf();
      if (srf != nullptr && SurfaceFromON(*srf, surfaces[static_cast<size_t>(fi)])) usable[static_cast<size_t>(fi)] = 1;
    }
  }
  // Closest point of the true surface to `p` (inside the trims), or nullopt.
  std::optional<ON_3dPoint> Closest(const ON_3dPoint& p, int divisions = 24) const {
    double best = std::numeric_limits<double>::infinity();
    ON_3dPoint best_pt = p;
    for (int fi = 0; fi < brep->m_F.Count(); ++fi) {
      if (!usable[static_cast<size_t>(fi)]) continue;
      const kernel::NurbsSurface& ns = surfaces[static_cast<size_t>(fi)];
      // The closest-point search is a grid scan; a coarse grid can lock onto
      // the wrong place near a degenerate pole (reported distances too
      // LARGE, never too small), so it is repeated on a finer grid and the
      // smaller distance wins.
      for (int div : {divisions, divisions * 3}) {
        const kernel::Point2d uv = ns.ClosestPointParameter(p, div, div);
        if (!UvInsideFaceLoops(*brep, brep->m_F[fi], uv.x, uv.y)) continue;
        const ON_3dPoint q = ns.PointAt(uv.x, uv.y);
        const double d = q.DistanceTo(p);
        if (d < best) { best = d; best_pt = q; }
      }
    }
    if (!std::isfinite(best)) return std::nullopt;
    return best_pt;
  }
  std::optional<double> Distance(const ON_3dPoint& p, int divisions = 24) const {
    const std::optional<ON_3dPoint> q = Closest(p, divisions);
    if (!q) return std::nullopt;
    return q->DistanceTo(p);
  }
};

// Builds the SubD for a Brep ToSubD cannot convert exactly (anything
// FromBrep refuses) and MEASURES how far its Catmull-Clark limit surface
// is from the Brep. Three steps per round (this replaces the earlier
// measure-and-disclose-only loop, whose sphere and box both missed):
//  1. tessellate the Brep at the round's chord tolerance into the control
//     cage (every cage vertex lies ON the Brep surface) and tag feature
//     edges as creases (SubD::SetCreasesByDihedral);
//  2. INTERPOLATE: SubD::InterpolateControlNet drives every cage vertex's
//     limit point onto the tessellation vertex it came from - Catmull-
//     Clark's smoothing no longer pulls the limit surface inside the
//     cage, which was the whole source of the old 0.0168 sphere miss;
//  3. measure the limit surface between and at the vertices: the limit
//     points of Subdivide(1) (every original vertex, edge midpoint and
//     face centre of the cage - exact limit evaluation, not facets)
//     against a tol/4 reference tessellation of the Brep (a proxy for the
//     true surface good to tol/4, which the reported number includes).
// If the deviation still exceeds `tol`, the chord tolerance is halved and
// the round repeated (bounded; plateau guard on the mesher's per-edge cap).
// The achieved bound is reported whether or not it converged.
std::optional<SubDFidelityResult> BuildSubDVerifiedAgainstBrep(const SceneObject& o, double tol) {
  // Cage candidates, finest last: the kernel's uniform parametric grid
  // tessellation (Brep::TessellateToClosedMesh(n, n): every vertex ON the
  // surface and, unlike the chord-tolerance mesher MeshOf() uses, no long
  // sliver triangles - on the default 0.001 sphere the mesher leaves
  // triangle centres up to 0.09 off the sphere) at n = 24, 48, 96, 192
  // divisions per face direction; when the grid tessellation is not a
  // closed manifold (a trimmed face it cannot weld) the round falls back to
  // MeshOf() at the round's chord tolerance, the previous behaviour.
  // Deviation is measured against a 384 x 384 grid tessellation of the same
  // Brep when that is a closed manifold; otherwise (a trimmed face the grid
  // cannot weld) against the TRUE surface (per-face NURBS closest point,
  // trim-aware, up to 64 faces) as an UPPER bound: the smaller of that
  // distance and the distance to the nearest cage vertex.
  std::optional<kernel::Mesh> reference = MeshOf(o, tol);
  if (!reference || reference->FaceCount() == 0) return std::nullopt;
  const kernel::MeshClosestPointIndex reference_index(*reference);
  // Preferred measuring stick: a 384 x 384-per-face grid tessellation of the
  // same Brep (about 3e-4 chord sag even on a 5-unit-radius sphere, every
  // vertex on the surface), queried through the closest-point index. It
  // avoids the pole failure of the per-face closest-point search below.
  std::optional<kernel::Mesh> fine;
  if (o.brep) {
    try {
      kernel::Mesh g = o.brep->TessellateToClosedMesh(384, 384);
      if (g.IsClosedManifold() && g.FaceCount() > 0) fine = std::move(g);
    } catch (const std::exception&) {}
  }
  std::optional<kernel::MeshClosestPointIndex> fine_index;
  if (fine) fine_index.emplace(*fine);
  const bool use_true = !fine && o.brep && o.brep->raw().m_F.Count() <= 64;
  std::optional<TrueBrepProbe> true_probe;
  if (use_true) true_probe.emplace(o.brep->raw());
  SubDFidelityResult best;
  bool have_best = false;
  double cage_tol = tol;
  int last_cage_faces = -1;
  for (int round = 0; round < kMaxFidelityRounds + 1; ++round) {
    std::optional<kernel::Mesh> cage;
    if (o.brep) {
      try {
        const int n = 24 << round;
        kernel::Mesh grid = o.brep->TessellateToClosedMesh(n, n);
        if (grid.IsClosedManifold() && grid.FaceCount() > 0) cage = std::move(grid);
      } catch (const std::exception&) {}
    }
    if (!cage) {
      cage = MeshOf(o, cage_tol);
      cage_tol *= 0.5;
    }
    if (!cage || cage->FaceCount() == 0 || cage->FaceCount() > kMaxFidelityCageFaces) break;
    if (round > 0 && cage->FaceCount() == last_cage_faces) break;
    last_cage_faces = cage->FaceCount();
    SubDFidelityResult cur;
    cur.rounds = round + 1;
    try {
      kernel::SubD sd = kernel::SubD::FromControlMesh(*cage);
      cur.crease_edges = sd.SetCreasesByDihedral(kFidelityCreaseAngleDeg);
      kernel::SubDInterpolationReport frep;
      const kernel::BoundingBox cbb = cage->GetBoundingBox();
      const double extent = (cbb.max - cbb.min).Length();
      cur.subd = sd.InterpolateControlNet(std::max(tol * 0.05, 2e-7 * std::max(extent, 1.0)), 150, &frep);
      cur.fit_iterations = frep.iterations;
      cur.fit_residual = frep.final_max_residual;
      kernel::SubD probe = cur.subd;
      probe.Subdivide(1);
      const std::vector<kernel::SubDLimitPoint> lps = probe.LimitPoints();
      std::vector<Point3d> cage_vertices;
      for (int i = 0; i < cage->VertexCount(); ++i) cage_vertices.emplace_back(cage->raw().m_V[i]);
      const size_t step = std::max<size_t>(1, lps.size() / static_cast<size_t>(kMaxFidelitySamples));
      double dev = 0.0;
      for (size_t i = 0; i < lps.size(); i += step) {
        const Point3d& q = lps[i].limit_point;
        double dd = 0.0;
        std::optional<double> td;
        if (true_probe) td = true_probe->Distance(q);
        if (fine_index) {
          dd = (fine_index->ClosestPoint(q) - q).Length();
        } else if (td) {
          double nv = std::numeric_limits<double>::infinity();
          for (const Point3d& rv : cage_vertices) nv = std::min(nv, rv.DistanceTo(q));
          dd = std::min(*td, nv);
        } else {
          dd = (reference_index.ClosestPoint(q) - q).Length();
        }
        dev = std::max(dev, dd);
        ++cur.samples;
      }
      cur.achieved_deviation = dev;
      cur.verified = dev <= tol;
      cur.cage_faces = cage->FaceCount();
    } catch (const std::exception&) {
      break;
    }
    best = cur;
    have_best = true;
    if (best.verified) break;
  }
  return have_best ? std::make_optional(best) : std::nullopt;
}

// Result of ExactCageForBrep(): the SubD::FromBrep cage with its sharp Brep
// edges creased, and the MEASURED distance of its limit surface from the Brep.
struct ExactCageResult {
  kernel::SubD subd;
  int crease_edges = 0;     // interior cage edges tagged Crease (dihedral above kFidelityCreaseAngleDeg)
  double deviation = 0.0;   // worst measured distance of a limit-surface sample from the Brep
  int samples = 0;
  bool verified = false;    // deviation <= tol
};

// ToSubD's exact path for a Brep SubD::FromBrep accepts (a solid or sheet of
// untrimmed planar quads). FromBrep only creases NAKED boundary edges, so a
// closed box would come out as an uncreased 6-face cage whose Catmull-Clark
// limit surface is a rounded blob (about a third of the box's volume): the
// cage is exact, the SHAPE is not. Edges between two faces whose normals
// differ by more than kFidelityCreaseAngleDeg (a box's 12 edges) are
// therefore creased with SubD::SetCreasesByDihedral, and the fidelity is
// then MEASURED rather than assumed: the limit points of Subdivide(2) of
// the creased cage (exact limit evaluation at every vertex, edge midpoint
// and face centre of two refinement rounds) against the Brep's planar
// faces (exact: see the tessellation comment below). nullopt if FromBrep
// refuses the Brep or the measurement itself fails (the caller then falls
// through to the tessellate-and-verify path).
std::optional<ExactCageResult> ExactCageForBrep(const SceneObject& o, double tol) {
  if (!o.brep) return std::nullopt;
  ExactCageResult r;
  try {
    r.subd = kernel::SubD::FromBrep(*o.brep, 1);
    r.crease_edges = r.subd.SetCreasesByDihedral(kFidelityCreaseAngleDeg);
    // FromBrep accepts only untrimmed PLANAR quad faces, so a 16 x 16 grid
    // tessellation of each face lies exactly ON the Brep: the distance to
    // it is the distance to the Brep itself, with no tessellation error
    // (and no per-face closest-point trim test, which is unreliable for a
    // limit point that lies on a shared edge).
    const kernel::Mesh reference = o.brep->TessellateToClosedMesh(16, 16);
    if (reference.FaceCount() == 0) return std::nullopt;
    const kernel::MeshClosestPointIndex index(reference);
    kernel::SubD refined = r.subd;
    refined.Subdivide(2);
    const std::vector<kernel::SubDLimitPoint> lps = refined.LimitPoints();
    if (lps.empty()) return std::nullopt;
    const size_t step = std::max<size_t>(1, lps.size() / static_cast<size_t>(kMaxFidelitySamples));
    for (size_t i = 0; i < lps.size(); i += step) {
      const Point3d& q = lps[i].limit_point;
      const double dd = (index.ClosestPoint(q) - q).Length();
      r.deviation = std::max(r.deviation, dd);
      ++r.samples;
    }
    r.verified = r.deviation <= tol;
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return r;
}

// ---------------------------------------------------------------------------
// Analytic cage for a revolved solid (Cylinder, Cone, TCone, a revolved
// profile with planar caps): ONE surface-of-revolution wall face plus planar
// cap faces. The kernel's grid tessellation cannot weld such a Brep into a
// closed manifold (the trimmed planar caps are meshed on their own lattice,
// which does not meet the wall's rim vertices), so ToSubD used to fall back
// to the chord-tolerance mesher: an 11894-face triangle cage whose limit
// surface was NOT verified. Here the cage is built straight from the Brep's
// own topology instead:
//  - the wall is a structured n_around x n_along quad grid evaluated on the
//    wall face's own analytic parametrisation (every vertex ON the Brep);
//    an end row that collapses to a point (a cone's apex) becomes one vertex
//    with a triangle fan;
//  - each planar cap is meshed from the wall's SHARED rim ring: an all-quad
//    O-grid (a square patch in the middle, one ring of wedge quads out to
//    the rim) or, for the measuring reference, a plain fan to the centroid;
//  - the result is a closed manifold by construction and is checked.
// nullopt for anything that is not that shape (a sphere has no cap, a torus
// has no open direction, a tube has two wall faces, a cap with a hole...).
struct RevolvedCage {
  kernel::Mesh mesh;
  double chord_sag = 0.0;  // largest |centre of a wall cell - surface at the cell's parameter centre|
  int caps = 0;
  std::vector<std::pair<ON_3dPoint, ON_3dPoint>> apex_spokes;  // edges into a collapsed end row's apex (float-rounded positions)
};

// Options of the curved-profile revolve cage (RevolveCurvedCageForBrep); the default leaves BuildRevolvedCage's
// behaviour for the straight-profile walls (Cylinder, Cone, TCone) exactly as it was.
struct RevolvedCageOpts {
  const std::vector<double>* along_fracs = nullptr;  // ring positions as fractions of the profile domain (first 0, last 1), replacing the uniform n_along ladder
  bool curved = false;  // accept a capless wall closed by two poles (a sphere) and leave a pole whose profile meets the axis square (a dome) un-creased
};

std::optional<RevolvedCage> BuildRevolvedCage(const ON_Brep& b, int n_around, int n_along, bool fan_caps, const RevolvedCageOpts& opts = RevolvedCageOpts()) {
  if (opts.along_fracs != nullptr) n_along = static_cast<int>(opts.along_fracs->size()) - 1;
  if (n_around < 8 || (!fan_caps && (n_around % 8) != 0) || n_along < 1) return std::nullopt;
  if (b.m_F.Count() < (opts.curved ? 1 : 2) || b.m_F.Count() > 3) return std::nullopt;
  const auto frac_at = [&](int j) { return opts.along_fracs != nullptr ? (*opts.along_fracs)[static_cast<size_t>(j)] : static_cast<double>(j) / n_along; };
  const ON_BoundingBox bbox = b.BoundingBox();
  const double scale = std::max(bbox.Diagonal().Length(), 1e-9);
  const double ptol = 1e-6 * scale;
  int wall = -1;
  std::vector<int> planar;
  for (int fi = 0; fi < b.m_F.Count(); ++fi) {
    const ON_Surface* s = b.m_F[fi].SurfaceOf();
    if (s == nullptr) return std::nullopt;
    ON_Plane pl;
    if (s->IsPlanar(&pl, ptol)) {
      if (b.m_F[fi].m_li.Count() != 1) return std::nullopt;  // a hole in a cap
      planar.push_back(fi);
    } else {
      if (wall >= 0) return std::nullopt;  // two curved faces: not a plain revolved wall
      wall = fi;
    }
  }
  if (wall < 0) return std::nullopt;
  const ON_Surface* srf = b.m_F[wall].SurfaceOf();
  const bool closed0 = srf->IsClosed(0) != 0, closed1 = srf->IsClosed(1) != 0;
  if (closed0 == closed1) return std::nullopt;  // torus-like (both closed) or not a revolution at all
  const int cdir = closed0 ? 0 : 1, odir = 1 - cdir;
  const ON_Interval dc = srf->Domain(cdir), dov = srf->Domain(odir);
  const auto eval = [&](double tc, double to) {
    double uv[2];
    uv[cdir] = tc;
    uv[odir] = to;
    return srf->PointAt(uv[0], uv[1]);
  };
  const int N = n_around, M = n_along;
  std::vector<ON_3dPoint> pos;
  std::vector<std::vector<int>> row(static_cast<size_t>(M) + 1);
  std::vector<char> degenerate(static_cast<size_t>(M) + 1, 0);
  for (int j = 0; j <= M; ++j) {
    const double to = dov.ParameterAt(frac_at(j));
    std::vector<ON_3dPoint> ring(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) ring[static_cast<size_t>(i)] = eval(dc.ParameterAt(static_cast<double>(i) / N), to);
    bool all_same = true;
    for (int i = 1; i < N; ++i) if (ring[static_cast<size_t>(i)].DistanceTo(ring[0]) > ptol) { all_same = false; break; }
    if ((j == 0 || j == M) && all_same) {
      degenerate[static_cast<size_t>(j)] = 1;
      row[static_cast<size_t>(j)].assign(static_cast<size_t>(N), static_cast<int>(pos.size()));
      pos.push_back(ring[0]);
    } else {
      for (int i = 0; i < N; ++i) {
        row[static_cast<size_t>(j)].push_back(static_cast<int>(pos.size()));
        pos.push_back(ring[static_cast<size_t>(i)]);
      }
    }
  }
  std::vector<std::array<int, 4>> faces;  // 4th == 3rd for a triangle
  const auto add_poly = [&](std::vector<int> v) {
    std::vector<int> u;
    for (size_t k = 0; k < v.size(); ++k) if (v[k] != v[(k + 1) % v.size()]) u.push_back(v[k]);
    if (u.size() == 3) faces.push_back({u[0], u[1], u[2], u[2]});
    else if (u.size() == 4) faces.push_back({u[0], u[1], u[2], u[3]});
  };
  RevolvedCage rc;
  const auto at = [&](int j, int i) { return row[static_cast<size_t>(j)][static_cast<size_t>(i)]; };
  for (int j = 0; j < M; ++j) {
    for (int i = 0; i < N; ++i) {
      const int i1 = (i + 1) % N;
      add_poly({at(j, i), at(j, i1), at(j + 1, i1), at(j + 1, i)});
      // Reference sag of this cell: its corner average against the surface at the cell's parameter centre.
      if (!degenerate[static_cast<size_t>(j)] && !degenerate[static_cast<size_t>(j + 1)]) {
        const ON_3dPoint& p00 = pos[static_cast<size_t>(at(j, i))];
        const ON_3dPoint& p10 = pos[static_cast<size_t>(at(j, i1))];
        const ON_3dPoint& p11 = pos[static_cast<size_t>(at(j + 1, i1))];
        const ON_3dPoint& p01 = pos[static_cast<size_t>(at(j + 1, i))];
        const ON_3dPoint avg((p00.x + p10.x + p11.x + p01.x) * 0.25, (p00.y + p10.y + p11.y + p01.y) * 0.25, (p00.z + p10.z + p11.z + p01.z) * 0.25);
        const ON_3dPoint mid = eval(dc.ParameterAt((i + 0.5) / N), dov.ParameterAt(0.5 * (frac_at(j) + frac_at(j + 1))));
        rc.chord_sag = std::max(rc.chord_sag, avg.DistanceTo(mid));
      }
    }
  }
  // Caps: one planar face per non-degenerate end ring, containing every ring point.
  int ends = 0;
  for (int e : {0, M}) {
    if (degenerate[static_cast<size_t>(e)]) continue;
    ++ends;
    bool matched = false;
    for (int fi : planar) {
      ON_Plane pl;
      b.m_F[fi].SurfaceOf()->IsPlanar(&pl, ptol);
      bool all_on = true;
      for (int i = 0; i < N && all_on; ++i) all_on = std::fabs(pl.DistanceTo(pos[static_cast<size_t>(at(e, i))])) <= 1e-5 * scale;
      if (all_on) { matched = true; break; }
    }
    if (!matched) return std::nullopt;
    // The cap polygon is the shared ring, traversed against the wall at the j = 0 end, with it at the j = M end.
    const bool forward = (e == M);
    const std::vector<int>& ring = row[static_cast<size_t>(e)];
    ON_3dPoint c(0, 0, 0);
    for (int i = 0; i < N; ++i) { const ON_3dPoint& p = pos[static_cast<size_t>(ring[static_cast<size_t>(i)])]; c.x += p.x / N; c.y += p.y / N; c.z += p.z / N; }
    const auto poly = [&](std::vector<int> v) {
      if (!forward) std::reverse(v.begin(), v.end());
      if (v.size() == 3) faces.push_back({v[0], v[1], v[2], v[2]});
      else faces.push_back({v[0], v[1], v[2], v[3]});
    };
    if (fan_caps) {
      const int ci = static_cast<int>(pos.size());
      pos.push_back(c);
      for (int i = 0; i < N; ++i) poly({ring[static_cast<size_t>(i)], ring[static_cast<size_t>((i + 1) % N)], ci});
    } else {
      const int k = N / 4;
      constexpr double kAlpha = 0.55;  // square corner at this fraction of the rim radius
      std::vector<int> grid(static_cast<size_t>((k + 1) * (k + 1)), -1);
      const auto gi = [&](int x, int y) -> int& { return grid[static_cast<size_t>(y * (k + 1) + x)]; };
      std::array<ON_3dPoint, 4> q;
      for (int m = 0; m < 4; ++m) {
        const ON_3dPoint& rp = pos[static_cast<size_t>(ring[static_cast<size_t>((N / 8 + m * k) % N)])];
        q[static_cast<size_t>(m)] = ON_3dPoint(c.x + kAlpha * (rp.x - c.x), c.y + kAlpha * (rp.y - c.y), c.z + kAlpha * (rp.z - c.z));
      }
      for (int y = 0; y <= k; ++y) {
        for (int x = 0; x <= k; ++x) {
          const double s = static_cast<double>(x) / k, t = static_cast<double>(y) / k;
          const double w0 = (1 - s) * (1 - t), w1 = s * (1 - t), w2 = s * t, w3 = (1 - s) * t;
          const ON_3dPoint p(w0 * q[0].x + w1 * q[1].x + w2 * q[2].x + w3 * q[3].x, w0 * q[0].y + w1 * q[1].y + w2 * q[2].y + w3 * q[3].y,
                             w0 * q[0].z + w1 * q[1].z + w2 * q[2].z + w3 * q[3].z);
          gi(x, y) = static_cast<int>(pos.size());
          pos.push_back(p);
        }
      }
      // Side m, step t of the square's boundary, walking the ring direction.
      const auto side = [&](int m, int t) {
        switch (m) {
          case 0: return gi(t, 0);
          case 1: return gi(k, t);
          case 2: return gi(k - t, k);
          default: return gi(0, k - t);
        }
      };
      for (int y = 0; y < k; ++y)
        for (int x = 0; x < k; ++x) poly({gi(x, y), gi(x + 1, y), gi(x + 1, y + 1), gi(x, y + 1)});
      for (int m = 0; m < 4; ++m) {
        for (int t = 0; t < k; ++t) {
          const int a = ring[static_cast<size_t>((N / 8 + m * k + t) % N)], bb = ring[static_cast<size_t>((N / 8 + m * k + t + 1) % N)];
          poly({a, bb, side(m, t + 1), side(m, t)});
        }
      }
    }
    ++rc.caps;
  }
  const bool two_poles = opts.curved && ends == 0 && planar.empty() && degenerate[0] && degenerate[static_cast<size_t>(M)];
  if (!two_poles && (ends == 0 || rc.caps != ends || rc.caps != static_cast<int>(planar.size()))) return std::nullopt;

  // Orient outward: the signed volume of the triangulated faces must be positive.
  double sv = 0.0;
  for (const auto& f : faces) {
    for (int t = 0; t < (f[3] == f[2] ? 1 : 2); ++t) {
      const ON_3dVector a(pos[static_cast<size_t>(f[0])]);
      const ON_3dVector bq(pos[static_cast<size_t>(f[t + 1])]);
      const ON_3dVector cq(pos[static_cast<size_t>(f[t + 2])]);
      sv += ON_DotProduct(a, ON_CrossProduct(bq, cq)) / 6.0;
    }
  }
  for (int e : {0, M}) {
    if (!degenerate[static_cast<size_t>(e)]) continue;
    const int nj = (e == 0) ? 1 : M - 1;
    const ON_3fPoint apex(pos[static_cast<size_t>(at(e, 0))]);
    if (opts.curved) {
      // A pole the profile meets square (a sphere's, a dome's) is a smooth point: no creased spokes. A pole whose profile
      // runs along the axis (a cone-like tip) stays sharp. The axis direction is pole -> centroid of the first ring.
      ON_3dPoint cen(0, 0, 0);
      for (int i = 0; i < N; ++i) { const ON_3dPoint& q = pos[static_cast<size_t>(at(nj, i))]; cen.x += q.x / N; cen.y += q.y / N; cen.z += q.z / N; }
      ON_3dVector axis = cen - ON_3dPoint(apex);
      const double tiny = (e == 0) ? 1e-3 : 1.0 - 1e-3;
      const ON_3dPoint near_pole = eval(dc.ParameterAt(0.0), dov.ParameterAt(tiny));
      ON_3dVector tang = near_pole - ON_3dPoint(apex);
      if (axis.Unitize() && tang.Unitize() && std::fabs(ON_DotProduct(axis, tang)) <= 0.1) continue;
    }
    for (int i = 0; i < N; ++i) rc.apex_spokes.emplace_back(ON_3dPoint(apex), ON_3dPoint(ON_3fPoint(pos[static_cast<size_t>(at(nj, i))])));
  }
  const bool flip = sv < 0.0;
  ON_Mesh& raw = rc.mesh.raw();
  raw.m_V.Reserve(static_cast<int>(pos.size()));
  for (const ON_3dPoint& p : pos) raw.m_V.Append(ON_3fPoint(p));
  raw.m_F.Reserve(static_cast<int>(faces.size()));
  for (const auto& f : faces) {
    const bool tri = f[3] == f[2];
    ON_MeshFace mf;
    if (!flip) {
      mf.vi[0] = f[0]; mf.vi[1] = f[1]; mf.vi[2] = f[2]; mf.vi[3] = f[3];
    } else if (tri) {
      mf.vi[0] = f[2]; mf.vi[1] = f[1]; mf.vi[2] = f[0]; mf.vi[3] = f[0];
    } else {
      mf.vi[0] = f[3]; mf.vi[1] = f[2]; mf.vi[2] = f[1]; mf.vi[3] = f[0];
    }
    if (tri) mf.vi[3] = mf.vi[2];
    raw.m_F.Append(mf);
  }
  if (!fan_caps && !rc.mesh.IsClosedManifold()) return std::nullopt;
  return rc;
}

// Result of AnalyticCageForBrep().
struct AnalyticCageResult {
  kernel::SubD subd;
  int cage_faces = 0, cage_around = 0, cage_along = 0;
  int crease_edges = 0;
  int fit_iterations = 0;
  double fit_residual = 0.0;
  double deviation = 0.0;        // measured limit-surface distance + the reference's own chord sag (an upper bound)
  double reference_sag = 0.0;    // chord sag of the reference sampling of the exact surface
  int reference_around = 0, reference_along = 0;
  int samples = 0;
  bool verified = false;
  int profile_cvs = 0, profile_degree = 0, rings = 0, poles = 0, caps = 0;  // curved-profile revolve cage only
};

// ToSubD for a revolved solid (see BuildRevolvedCage): builds the analytic
// cage at 32, 40, 48, 64, 128 segments around until the interpolated Catmull-Clark
// limit surface is within `tol`. The MEASURE compares sampled limit-surface
// points (every vertex, edge midpoint and face centre of two further
// subdivision rounds: exact limit evaluation) with the exact surface of the
// Brep sampled much finer than the tolerance (the same analytic wall
// parametrisation at a segment count chosen so the chord sag is about
// tol/10, and exactly planar caps), through the mesh closest-point index;
// the reference's own estimated chord sag is added to the reported
// deviation, so the number is an upper bound on the distance to the exact
// surface (up to the cell-centre sag estimate).
std::optional<AnalyticCageResult> AnalyticCageForBrep(const SceneObject& o, double tol) {
  if (!o.brep) return std::nullopt;
  const ON_Brep& b = o.brep->raw();
  try {
    const ON_BoundingBox bb = b.BoundingBox();
    const double diag = std::max(bb.Diagonal().Length(), 1e-9);
    const double R = std::max(0.5 * diag, 1e-6);
    int nref = static_cast<int>(std::ceil(3.14159265 * std::sqrt(5.0 * R / std::max(tol, 1e-12))));
    nref = std::min(1024, std::max(256, (nref + 7) / 8 * 8));
    const int mref = 64;
    // Building the reference first also decides whether this is a revolved solid at all.
    const std::optional<RevolvedCage> ref = BuildRevolvedCage(b, nref, mref, true);
    if (!ref) return std::nullopt;
    const kernel::MeshClosestPointIndex ref_index(ref->mesh);
    std::optional<AnalyticCageResult> best;
    // 40 and 48 sit between 32 and 64 on purpose: the 64-segment cage of a
    // radius-4 cone (960 faces, O-grid cap cells ~0.1 across once refined for
    // ToNURBS) converts to a polysurface the Brep mesher cannot weld at the
    // Volume tolerance (ToNURBS said "closed solid", Volume said "not
    // closed"); the 40-segment cage verifies (0.00086 < 0.001), is a third
    // the size, and its ToNURBS result measures a correct Volume.
    for (int around : {32, 40, 48, 64, 128}) {
      const int along = std::min(12, std::max(4, static_cast<int>(std::lround(diag / (6.2831853 * R / around) * 0.5))));
      const std::optional<RevolvedCage> cage = BuildRevolvedCage(b, around, along, false);
      if (!cage) { if (!best) return std::nullopt; break; }
      AnalyticCageResult cur;
      kernel::SubD sd = kernel::SubD::FromControlMesh(cage->mesh);
      cur.crease_edges = sd.SetCreasesByDihedral(kFidelityCreaseAngleDeg);
      // A cone's apex is a sharp point: crease its spokes so Catmull-Clark does not round the tip off.
      for (const auto& sp : cage->apex_spokes) if (sd.SetCrease(sp.first, sp.second, true, 1e-5 * diag)) ++cur.crease_edges;
      kernel::SubDInterpolationReport frep;
      const kernel::BoundingBox cbb = cage->mesh.GetBoundingBox();
      const double extent = (cbb.max - cbb.min).Length();
      cur.subd = sd.InterpolateControlNet(std::max(tol * 0.05, 2e-7 * std::max(extent, 1.0)), 150, &frep);
      cur.fit_iterations = frep.iterations;
      cur.fit_residual = frep.final_max_residual;
      kernel::SubD probe = cur.subd;
      probe.Subdivide(2);
      const std::vector<kernel::SubDLimitPoint> lps = probe.LimitPoints();
      if (lps.empty()) return std::nullopt;
      const size_t step = std::max<size_t>(1, lps.size() / 20000);
      double dev = 0.0;
      for (size_t i = 0; i < lps.size(); i += step) {
        const Point3d& q = lps[i].limit_point;
        dev = std::max(dev, (ref_index.ClosestPoint(q) - q).Length());
        ++cur.samples;
      }
      cur.reference_sag = ref->chord_sag;
      cur.reference_around = nref;
      cur.reference_along = mref;
      cur.deviation = dev + ref->chord_sag;
      cur.verified = cur.deviation <= tol;
      cur.cage_faces = cage->mesh.FaceCount();
      cur.cage_around = around;
      cur.cage_along = along;
      best = std::move(cur);
      if (best->verified) break;
    }
    return best;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// Analytic cage for a TORUS (one face, closed in both parameter directions).
// The old path tessellated it into the kernel's uniform grid at n = 24, 48,
// 96, 192 divisions per direction (a 18432-face cage for the default
// document tolerance: 96 x 96 x 2 triangles). Here the cage is the plain
// N x M quad grid of the face's own parametrisation (every vertex ON the
// Brep, no pole, no irregular vertex: every vertex has valence 4 and the
// grid is doubly periodic), interpolated onto the surface with
// SubD::InterpolateControlNet, and the Catmull-Clark limit surface is then
// MEASURED against the exact surface sampled far finer than the tolerance
// (the same parametrisation at a chord sag of about tol/6; its own estimated
// sag is added to the reported deviation), exactly as the revolved-solid
// cage does it. nullopt / unverified falls through to the unchanged path.
// ---------------------------------------------------------------------------
struct TorusGrid {
  kernel::Mesh mesh;
  double chord_sag = 0.0;  // largest |midpoint of a cell diagonal - surface at the cell's parameter centre| (sampled cells)
};

// Orients a polygon soup outward (positive signed volume) and wraps it in a kernel::Mesh. Faces are
// 4 indices; a triangle repeats its third index.
kernel::Mesh MeshFromOrientedPolys(const std::vector<ON_3dPoint>& pos, const std::vector<std::array<int, 4>>& faces) {
  double sv = 0.0;
  for (const auto& f : faces) {
    for (int t = 0; t < (f[3] == f[2] ? 1 : 2); ++t) {
      const ON_3dVector a(pos[static_cast<size_t>(f[0])]);
      const ON_3dVector bq(pos[static_cast<size_t>(f[t + 1])]);
      const ON_3dVector cq(pos[static_cast<size_t>(f[t + 2])]);
      sv += ON_DotProduct(a, ON_CrossProduct(bq, cq)) / 6.0;
    }
  }
  const bool flip = sv < 0.0;
  kernel::Mesh mesh;
  ON_Mesh& raw = mesh.raw();
  raw.m_V.Reserve(static_cast<int>(pos.size()));
  for (const ON_3dPoint& p : pos) raw.m_V.Append(ON_3fPoint(p));
  raw.m_F.Reserve(static_cast<int>(faces.size()));
  for (const auto& f : faces) {
    const bool tri = f[3] == f[2];
    ON_MeshFace mf;
    if (!flip) {
      mf.vi[0] = f[0]; mf.vi[1] = f[1]; mf.vi[2] = f[2]; mf.vi[3] = f[3];
    } else if (tri) {
      mf.vi[0] = f[2]; mf.vi[1] = f[1]; mf.vi[2] = f[0]; mf.vi[3] = f[0];
    } else {
      mf.vi[0] = f[3]; mf.vi[1] = f[2]; mf.vi[2] = f[1]; mf.vi[3] = f[0];
    }
    if (tri) mf.vi[3] = mf.vi[2];
    raw.m_F.Append(mf);
  }
  return mesh;
}

// The surface of a one-face, doubly-closed Brep (null for anything else).
const ON_Surface* DoublyClosedFaceSurface(const ON_Brep& b) {
  if (b.m_F.Count() != 1) return nullptr;
  const ON_Surface* s = b.m_F[0].SurfaceOf();
  if (s == nullptr || !s->IsClosed(0) || !s->IsClosed(1)) return nullptr;
  return s;
}

std::optional<TorusGrid> BuildTorusGrid(const ON_Brep& b, int N, int M, bool with_sag) {
  if (N < 8 || M < 8) return std::nullopt;
  const ON_Surface* srf = DoublyClosedFaceSurface(b);
  if (srf == nullptr) return std::nullopt;
  const ON_BoundingBox bbox = b.BoundingBox();
  const double scale = std::max(bbox.Diagonal().Length(), 1e-9);
  ON_Plane pl;
  if (srf->IsPlanar(&pl, 1e-6 * scale)) return std::nullopt;
  const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
  std::vector<ON_3dPoint> pos(static_cast<size_t>(N) * M);
  const auto at = [&](int i, int j) { return (j % M) * N + (i % N); };
  for (int j = 0; j < M; ++j)
    for (int i = 0; i < N; ++i) pos[static_cast<size_t>(at(i, j))] = srf->PointAt(du.ParameterAt(static_cast<double>(i) / N), dv.ParameterAt(static_cast<double>(j) / M));
  std::vector<std::array<int, 4>> faces;
  faces.reserve(static_cast<size_t>(N) * M);
  for (int j = 0; j < M; ++j)
    for (int i = 0; i < N; ++i) faces.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
  for (const auto& f : faces)
    for (int k = 0; k < 4; ++k)
      if (pos[static_cast<size_t>(f[k])].DistanceTo(pos[static_cast<size_t>(f[(k + 1) % 4])]) < 1e-9 * scale) return std::nullopt;  // a collapsed edge: not a regular torus grid
  TorusGrid g;
  if (with_sag) {
    const int istep = std::max(1, N / 16);
    for (int j = 0; j < M; ++j) {
      for (int i = 0; i < N; i += istep) {
        const ON_3dPoint& p00 = pos[static_cast<size_t>(at(i, j))];
        const ON_3dPoint& p10 = pos[static_cast<size_t>(at(i + 1, j))];
        const ON_3dPoint& p11 = pos[static_cast<size_t>(at(i + 1, j + 1))];
        const ON_3dPoint& p01 = pos[static_cast<size_t>(at(i, j + 1))];
        const ON_3dPoint mid = srf->PointAt(du.ParameterAt((i + 0.5) / N), dv.ParameterAt((j + 0.5) / M));
        const ON_3dPoint d0((p00.x + p11.x) * 0.5, (p00.y + p11.y) * 0.5, (p00.z + p11.z) * 0.5);
        const ON_3dPoint d1((p10.x + p01.x) * 0.5, (p10.y + p01.y) * 0.5, (p10.z + p01.z) * 0.5);
        g.chord_sag = std::max({g.chord_sag, d0.DistanceTo(mid), d1.DistanceTo(mid)});
      }
    }
  }
  g.mesh = MeshFromOrientedPolys(pos, faces);
  if (!g.mesh.IsClosedManifold()) return std::nullopt;
  return g;
}

// Result of an analytic cage (revolved solid, torus, right prism) - reuses AnalyticCageResult.
std::optional<AnalyticCageResult> TorusCageForBrep(const SceneObject& o, double tol) {
  if (!o.brep) return std::nullopt;
  const ON_Brep& b = o.brep->raw();
  const ON_Surface* srf = DoublyClosedFaceSurface(b);
  if (srf == nullptr) return std::nullopt;
  try {
    const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
    // Circumference of the u and v parameter circles through the middle of the other domain: sets the cage's aspect.
    const auto circ = [&](bool along_u) {
      const int n = 96;
      double len = 0.0;
      ON_3dPoint prev;
      for (int k = 0; k <= n; ++k) {
        const double t = static_cast<double>(k) / n;
        const ON_3dPoint p = along_u ? srf->PointAt(du.ParameterAt(t), dv.ParameterAt(0.25)) : srf->PointAt(du.ParameterAt(0.25), dv.ParameterAt(t));
        if (k > 0) len += p.DistanceTo(prev);
        prev = p;
      }
      return len;
    };
    const double lu = circ(true), lv = circ(false);
    if (!(lu > 0.0) || !(lv > 0.0)) return std::nullopt;
    const double ratio = std::min(4.0, std::max(1.0, lu / lv));
    // Reference: the same parametrisation at a chord sag of about tol/6.
    const ON_BoundingBox bb = b.BoundingBox();
    const double rho = std::max(0.5 * bb.Diagonal().Length(), 1e-6);
    int nref = static_cast<int>(std::ceil(3.14159265 * std::sqrt(3.0 * rho / std::max(tol, 1e-12))));
    nref = std::min(1024, std::max(192, (nref + 7) / 8 * 8));
    const double lratio = std::max(0.25, std::min(1.0, lv / lu));
    int mref = std::max(96, (static_cast<int>(std::ceil(nref * lratio * 1.1)) + 7) / 8 * 8);
    while (static_cast<long long>(nref) * mref > 300000 && mref > 96) mref -= 8;
    const std::optional<TorusGrid> ref = BuildTorusGrid(b, nref, mref, true);
    if (!ref) return std::nullopt;
    const kernel::MeshClosestPointIndex ref_index(ref->mesh);
    std::optional<AnalyticCageResult> best;
    for (int M : {16, 20, 24, 32, 48}) {
      const int N = std::max(32, static_cast<int>(std::lround(M * ratio / 4.0)) * 4);
      const std::optional<TorusGrid> cage = BuildTorusGrid(b, N, M, false);
      if (!cage) { if (!best) return std::nullopt; break; }
      AnalyticCageResult cur;
      kernel::SubD sd = kernel::SubD::FromControlMesh(cage->mesh);
      // No creases: a torus is smooth everywhere; the grid is all valence-4, so Catmull-Clark is exactly bicubic B-spline.
      kernel::SubDInterpolationReport frep;
      const kernel::BoundingBox cbb = cage->mesh.GetBoundingBox();
      const double extent = (cbb.max - cbb.min).Length();
      cur.subd = sd.InterpolateControlNet(std::max(tol * 0.05, 2e-7 * std::max(extent, 1.0)), 150, &frep);
      cur.fit_iterations = frep.iterations;
      cur.fit_residual = frep.final_max_residual;
      kernel::SubD probe = cur.subd;
      probe.Subdivide(2);
      const std::vector<kernel::SubDLimitPoint> lps = probe.LimitPoints();
      if (lps.empty()) return std::nullopt;
      const size_t step = std::max<size_t>(1, lps.size() / 40000);
      double dev = 0.0;
      for (size_t i = 0; i < lps.size(); i += step) {
        const Point3d& q = lps[i].limit_point;
        dev = std::max(dev, (ref_index.ClosestPoint(q) - q).Length());
        ++cur.samples;
      }
      cur.reference_sag = ref->chord_sag;
      cur.reference_around = nref;
      cur.reference_along = mref;
      cur.deviation = dev + ref->chord_sag;
      cur.verified = cur.deviation <= tol;
      cur.cage_faces = cage->mesh.FaceCount();
      cur.cage_around = N;
      cur.cage_along = M;
      best = std::move(cur);
      if (best->verified) break;
    }
    return best;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// Exact-structure cage for a revolve of a CURVED profile (a vase, a bottle, a sphere, a
// dome / sphere segment: Brep::Revolve of a spline or arc). AnalyticCageForBrep above puts
// a fixed ladder of at most 12 evenly spaced rings on the wall and keeps whatever it reaches
// (a vase measured 0.011, a bottle 0.026 off the surface, in 2700-3800 faces), and a sphere
// (one wall face, two poles, no cap) was refused outright and tessellated into the dense
// dense cage (a sphere-like egg: 18240 faces). Here the cage follows the PROFILE: the ring count is
// (profile NURBS control points - 1) x k for a refinement factor k = 2, 3, 4, 5, 6, 8, raised until
// the measured limit surface is within `tol` (the rings are equally spaced in profile arc length and
// clustered toward the ends, see rings_for); the
// segments around are the 32/40/48/64/128 ladder of the straight-profile cage. The cage is
// interpolated onto the wall (SubD::InterpolateControlNet) and the Catmull-Clark limit
// surface is MEASURED (every vertex, edge midpoint and face centre of two further
// subdivision rounds) against the exact wall sampled much finer than the tolerance (its own
// chord sag added). A pole the profile meets square stays smooth; a flat end is an all-quad
// O-grid cap on the wall's shared rim ring. The caller uses the cage only when it is
// verified; otherwise `refused` says why and the unchanged path runs.
// Phase 1 fixes the ring refinement at the finest ring (128 segments around); phase 2 then
// takes the fewest segments around that still verify with those rings.
// nullopt without a reason: not this shape at all (a straight profile - Cylinder, Cone,
// TCone, handled by AnalyticCageForBrep -, a non-NURBS wall, several curved faces, a torus).
std::optional<AnalyticCageResult> RevolveCurvedCageForBrep(const SceneObject& o, double tol, std::string* refused) {
  if (!o.brep) return std::nullopt;
  const ON_Brep& b = o.brep->raw();
  try {
    if (b.m_F.Count() < 1 || b.m_F.Count() > 3) return std::nullopt;
    const ON_BoundingBox bb = b.BoundingBox();
    const double diag = std::max(bb.Diagonal().Length(), 1e-9);
    int wall = -1;
    for (int fi = 0; fi < b.m_F.Count(); ++fi) {
      const ON_Surface* s = b.m_F[fi].SurfaceOf();
      if (s == nullptr) return std::nullopt;
      ON_Plane pl;
      if (s->IsPlanar(&pl, 1e-6 * diag)) continue;
      if (wall >= 0) return std::nullopt;
      wall = fi;
    }
    if (wall < 0) return std::nullopt;
    const ON_Surface* ns = b.m_F[wall].SurfaceOf();
    const bool closed0 = ns->IsClosed(0) != 0, closed1 = ns->IsClosed(1) != 0;
    if (closed0 == closed1) return std::nullopt;
    const int cdir = closed0 ? 0 : 1, odir = 1 - cdir;
    const ON_Interval dc = ns->Domain(cdir), dov = ns->Domain(odir);
    const auto eval = [&](double tc, double to) {
      double uv[2];
      uv[cdir] = tc;
      uv[odir] = to;
      return ns->PointAt(uv[0], uv[1]);
    };
    // A straight profile (Cylinder, Cone, TCone) is AnalyticCageForBrep's: its rings need no profile fit.
    {
      const ON_3dPoint p0 = eval(dc.ParameterAt(0.37), dov.ParameterAt(0.0)), p1 = eval(dc.ParameterAt(0.37), dov.ParameterAt(1.0));
      const ON_Line chord(p0, p1);
      double off = 0.0;
      for (int k = 1; k < 64; ++k) off = std::max(off, chord.DistanceTo(eval(dc.ParameterAt(0.37), dov.ParameterAt(k / 64.0))));
      if (off <= 1e-7 * diag) return std::nullopt;
    }
    // The profile's NURBS description: the wall's control rows along the profile (a NURBS wall) or the generating
    // curve of a surface of revolution (what the Revolve command builds), whose parameter IS the wall's.
    const ON_NurbsSurface* wall_nurbs = ON_NurbsSurface::Cast(ns);
    const ON_RevSurface* wall_rev = ON_RevSurface::Cast(ns);
    const ON_NurbsCurve* prof = wall_rev != nullptr ? ON_NurbsCurve::Cast(wall_rev->m_curve) : nullptr;
    if (wall_nurbs == nullptr && prof == nullptr) return std::nullopt;
    if (prof != nullptr && (std::fabs(prof->Domain()[0] - dov[0]) > 1e-9 * (1.0 + std::fabs(dov[0])) || std::fabs(prof->Domain()[1] - dov[1]) > 1e-9 * (1.0 + std::fabs(dov[1])))) return std::nullopt;
    const int ncv = prof != nullptr ? prof->CVCount() : wall_nurbs->CVCount(odir);
    const int degree = prof != nullptr ? prof->Degree() : wall_nurbs->Degree(odir);
    if (ncv < 3) return std::nullopt;
    const auto refuse = [&](const std::string& why) -> std::optional<AnalyticCageResult> {
      if (refused != nullptr) *refused = "curved-profile revolve cage refused (" + why + "), using the existing path";
      return std::nullopt;
    };
    // Ring count: one ring per profile control point span, times a refinement factor k (below). Catmull-Clark treats
    // the rings as uniformly spaced, so any jump in the spacing costs accuracy (a ring set at the control points'
    // Greville abscissae, graded by halving toward the ends, measured 0.0029 against 0.0024 for plain uniform
    // refinement); a SMOOTH spacing keeps the fourth-order accuracy. The rings are therefore equally spaced in
    // arc length of the profile, then clustered toward each flat (creased) end by the smooth map
    // u = t - b sin(2 pi t)/(2 pi) (b = 0.75: end spacing 0.25, middle 1.75 of the uniform one): Catmull-Clark's crease end condition leaves an
    // error that grows with the square of the spacing next to the crease (measured on a vase, uniform rings:
    // 0.045, 0.019, 0.010, 0.0044, 0.0024 for spacings 1.5, 1.0, 0.75, 0.5, 0.375); a pole's valence-N fan has the same
    // kind of error in the first ring (measured on a flat-bottomed bottle: 0.0029 at the pole with unclustered rings).
    constexpr int kTable = 4096;
    std::vector<double> arc(static_cast<size_t>(kTable) + 1, 0.0);
    {
      ON_3dPoint prev = eval(dc.ParameterAt(0.37), dov.ParameterAt(0.0));
      for (int i = 1; i <= kTable; ++i) {
        const ON_3dPoint q = eval(dc.ParameterAt(0.37), dov.ParameterAt(static_cast<double>(i) / kTable));
        arc[static_cast<size_t>(i)] = arc[static_cast<size_t>(i) - 1] + q.DistanceTo(prev);
        prev = q;
      }
      if (!(arc.back() > 0.0)) return std::nullopt;
    }
    const auto frac_of_arc = [&](double u) {  // u in [0,1] of the profile's arc length -> fraction of its parameter domain
      const double target = u * arc.back();
      const size_t hi = static_cast<size_t>(std::lower_bound(arc.begin(), arc.end(), target) - arc.begin());
      if (hi == 0) return 0.0;
      if (hi > static_cast<size_t>(kTable)) return 1.0;
      const double a = arc[hi - 1], c = arc[hi];
      return (static_cast<double>(hi - 1) + (c > a ? (target - a) / (c - a) : 0.0)) / kTable;
    };
    constexpr double kCluster = 0.75;
    const auto rings_for = [&](int n) {
      std::vector<double> r(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        const double u = t - kCluster * std::sin(2.0 * 3.14159265358979 * t) / (2.0 * 3.14159265358979);
        r[static_cast<size_t>(i)] = i == 0 ? 0.0 : i == n - 1 ? 1.0 : frac_of_arc(u);
      }
      return r;
    };
    // Reference: the exact wall sampled so its chord sag is well under the tolerance (the sag is added to every deviation).
    const double R = std::max(0.5 * diag, 1e-6);
    int nref = static_cast<int>(std::ceil(3.14159265 * std::sqrt(5.0 * R / std::max(tol, 1e-12))));
    nref = std::min(1024, std::max(256, (nref + 7) / 8 * 8));
    std::optional<RevolvedCage> ref;
    int mref = 128;
    for (;; mref *= 2) {
      RevolvedCageOpts ropts;
      ropts.curved = true;
      ref = BuildRevolvedCage(b, nref, mref, true, ropts);
      if (!ref) return std::nullopt;  // not a wall + planar caps / two poles
      if (ref->chord_sag <= tol / 6.0 || mref >= 512) break;
    }
    const kernel::MeshClosestPointIndex ref_index(ref->mesh);
    const auto measure = [&](int around, const std::vector<double>& fr) -> std::optional<AnalyticCageResult> {
      RevolvedCageOpts opts;
      opts.along_fracs = &fr;
      opts.curved = true;
      const std::optional<RevolvedCage> cage = BuildRevolvedCage(b, around, 0, false, opts);
      if (!cage) return std::nullopt;
      AnalyticCageResult cur;
      kernel::SubD sd = kernel::SubD::FromControlMesh(cage->mesh);
      cur.crease_edges = sd.SetCreasesByDihedral(kFidelityCreaseAngleDeg);
      for (const auto& sp : cage->apex_spokes) if (sd.SetCrease(sp.first, sp.second, true, 1e-5 * diag)) ++cur.crease_edges;
      kernel::SubDInterpolationReport frep;
      const kernel::BoundingBox cbb = cage->mesh.GetBoundingBox();
      const double extent = (cbb.max - cbb.min).Length();
      cur.subd = sd.InterpolateControlNet(std::max(tol * 0.05, 2e-7 * std::max(extent, 1.0)), 150, &frep);
      cur.fit_iterations = frep.iterations;
      cur.fit_residual = frep.final_max_residual;
      kernel::SubD probe = cur.subd;
      probe.Subdivide(2);
      const std::vector<kernel::SubDLimitPoint> lps = probe.LimitPoints();
      if (lps.empty()) return std::nullopt;
      const size_t step = std::max<size_t>(1, lps.size() / 20000);
      double dev = 0.0;
      for (size_t i = 0; i < lps.size(); i += step) {
        const Point3d& q = lps[i].limit_point;
        dev = std::max(dev, (ref_index.ClosestPoint(q) - q).Length());
        ++cur.samples;
      }
      cur.reference_sag = ref->chord_sag;
      cur.reference_around = nref;
      cur.reference_along = mref;
      cur.deviation = dev + ref->chord_sag;
      cur.verified = cur.deviation <= tol;
      cur.cage_faces = cage->mesh.FaceCount();
      cur.cage_around = around;
      cur.rings = static_cast<int>(fr.size());
      cur.cage_along = cur.rings - 1;
      cur.profile_cvs = ncv;
      cur.profile_degree = degree;
      cur.caps = cage->caps;
      cur.poles = cage->apex_spokes.empty() ? 0 : 1;
      return cur;
    };
    constexpr int kMaxRings = 48;
    constexpr int kMaxFaces = 4000;
    std::optional<AnalyticCageResult> fine;
    std::vector<double> fr;
    double last_dev = -1.0;
    int last_rings = 0;
    for (int k : {2, 3, 4, 5, 6, 8}) {
      fr = rings_for((ncv - 1) * k + 1);
      if (fr.size() < 6) continue;
      if (fr.size() > static_cast<size_t>(kMaxRings)) break;
      const std::optional<AnalyticCageResult> cur = measure(128, fr);
      if (!cur) return refuse("the wall could not be caged: not a plain revolved wall with flat caps or poles");
      last_dev = cur->deviation;
      last_rings = static_cast<int>(fr.size());
      if (cur->verified) { fine = cur; break; }
    }
    if (!fine) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "deviation %.6f at %d rings x 128 segments exceeds the tolerance %.6f", last_dev, last_rings, tol);
      return refuse(buf);
    }
    std::optional<AnalyticCageResult> best = fine;
    for (int around : {32, 40, 48, 64, 96}) {
      const std::optional<AnalyticCageResult> cur = measure(around, fr);
      if (cur && cur->verified) { best = cur; break; }
    }
    if (best->cage_faces > kMaxFaces) {
      return refuse("the cage would need " + std::to_string(best->cage_faces) + " faces, more than the " + std::to_string(kMaxFaces) + " allowed");
    }
    return best;
  } catch (const std::exception& e) {
    if (refused != nullptr) *refused = std::string("curved-profile revolve cage refused (") + e.what() + "), using the existing path";
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// Analytic cage for a RIGHT PRISM whose profile is lines and circular arcs
// (an extruded L or U, a box with its vertical edges rounded by FilletEdge,
// a slab with rounded ends). The profile is read by prism::Recognize (the
// same recogniser the exact Offset / Shell of a prism uses; it cross-checks
// the solid's volume against profile area x height). The cage is one quad
// ring per profile column (arcs cut every `dth`, a straight line that meets a
// tangent neighbour is graded 1.5x away from the joint so the interpolating
// limit curve does not swing across it), J rows along the axis, and the two
// flat caps ear-clipped from the same boundary points (a flat cap is exactly
// planar whatever its triangulation: every Catmull-Clark limit point of
// coplanar control points is an affine combination of them, and its rim
// points come from the crease rule). Rims are creased (dihedral), vertical
// edges at profile corners are creased explicitly (also for turns below the
// dihedral threshold), the cage is interpolated onto the surface and the
// limit surface is MEASURED: against the exact wall (a ruled strip over the
// profile sampled at a chord sag of about tol/10, exact for an extrusion)
// and, for the caps, against the exact cap planes wherever the sample lies
// inside the profile. The reported deviation includes the reference's sag.
// ---------------------------------------------------------------------------
struct PrismColumns {
  std::vector<prism::V2> pts;
  std::vector<char> sharp;  // 1 where the profile has a corner (tangent turn > 2 degrees) at this column
};

// Column positions strictly inside a straight line of length L, measured from its start. An end that
// joins a tangent neighbour (ja / jb) starts with the arc's own spacing `s` and the spacing then
// grows geometrically towards the other end (at most 1.5x per interval). The ratio is solved so the
// intervals add up to the length EXACTLY: a leftover sliver next to a long interval (the old
// "grow until the next step would overshoot" rule left a 0.26 sliver after a 2.7 interval at a sharp
// corner) makes the interpolating cubic overshoot past the corner by tenths of a unit. Two tangent
// ends are graded towards the middle, which is a column of its own (equal spacing on both sides).
std::vector<double> GradedRun(double L, double s) {  // spacing s at 0, growing towards L; interior breaks only
  std::vector<double> out;
  if (!(s > 0.0) || !(L > 1.5 * s)) return out;
  constexpr double kGrow = 1.5, kShrink = 0.5;
  const auto total = [&](double g, int n) {
    double sum = 0.0, st = s;
    for (int k = 0; k < n; ++k) { sum += st; st *= g; }
    return sum;
  };
  int n = 2;
  while (n < 4096 && total(kGrow, n) < L) ++n;
  double lo = kShrink, hi = kGrow;  // total(g, n) is increasing in g; total(kShrink, n) <= L for the minimal n >= 2 here
  if (total(lo, n) > L) return out;
  for (int it = 0; it < 80; ++it) {
    const double mid = 0.5 * (lo + hi);
    (total(mid, n) < L ? lo : hi) = mid;
  }
  const double g = 0.5 * (lo + hi);
  double x = 0.0, st = s;
  for (int k = 0; k + 1 < n; ++k) { x += st; st *= g; out.push_back(x); }
  return out;
}

std::vector<double> GradedBreaks(double L, double s, bool ja, bool jb) {
  std::vector<double> br;
  if ((!ja && !jb) || !(s > 0.0) || !(L > 1.5 * s)) return br;
  if (ja && jb) {
    const std::vector<double> half = GradedRun(0.5 * L, s);
    for (double x : half) br.push_back(x);
    br.push_back(0.5 * L);
    for (auto it = half.rbegin(); it != half.rend(); ++it) br.push_back(L - *it);
    return br;
  }
  br = GradedRun(L, s);
  if (jb) {
    for (double& x : br) x = L - x;
    std::sort(br.begin(), br.end());
  }
  return br;
}

PrismColumns DiscretizeProfile(const std::vector<prism::Seg>& P, double dth, double s_line) {
  PrismColumns cols;
  const size_t S = P.size();
  const auto turn = [&](size_t k) {  // tangent turn at the start of segment k
    const prism::V2 t0 = prism::EndTangent(P[(k + S - 1) % S]), t1 = prism::StartTangent(P[k]);
    return std::atan2(std::fabs(prism::Cross(t0, t1)), prism::Dot(t0, t1));
  };
  constexpr double kCornerTurn = 0.035;  // 2 degrees
  for (size_t k = 0; k < S; ++k) {
    const prism::Seg& g = P[k];
    const bool sharp0 = turn(k) > kCornerTurn;
    cols.pts.push_back(g.a);
    cols.sharp.push_back(sharp0 ? 1 : 0);
    if (g.arc) {
      const double th = prism::Sweep(g);
      const int n = std::max(1, static_cast<int>(std::ceil(th / dth - 1e-9)));
      const double p0 = std::atan2(g.a.y - g.c.y, g.a.x - g.c.x);
      for (int t = 1; t < n; ++t) {
        const double p = p0 + g.s * th * t / n;
        cols.pts.push_back(g.c + prism::V2{g.r * std::cos(p), g.r * std::sin(p)});
        cols.sharp.push_back(0);
      }
    } else {
      const bool sharp1 = turn((k + 1) % S) > kCornerTurn;
      const double L = prism::Len(g.b - g.a);
      for (double x : GradedBreaks(L, s_line, !sharp0, !sharp1)) {
        cols.pts.push_back(g.a + (x / L) * (g.b - g.a));
        cols.sharp.push_back(0);
      }
    }
  }
  return cols;
}

// Ear clipping of a counter-clockwise simple polygon; empty on failure.
std::vector<std::array<int, 3>> EarClip(const std::vector<prism::V2>& p, double scale) {
  const int n = static_cast<int>(p.size());
  std::vector<std::array<int, 3>> tris;
  if (n < 3) return tris;
  std::vector<int> idx(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) idx[static_cast<size_t>(i)] = i;
  const double eps = 1e-9 * scale * scale;
  const auto cross3 = [&](int a, int b, int c) { return prism::Cross(p[static_cast<size_t>(b)] - p[static_cast<size_t>(a)], p[static_cast<size_t>(c)] - p[static_cast<size_t>(a)]); };
  const auto inside = [&](int a, int b, int c, int q) {
    return cross3(a, b, q) >= -eps && cross3(b, c, q) >= -eps && cross3(c, a, q) >= -eps;
  };
  // Ears are taken shortest-diagonal first: that peels the polygon like an onion instead of fanning
  // it from one vertex (a fan puts a very high valence vertex on the flat cap, whose Catmull-Clark
  // limit overshoots the cap's convex hull by tenths of a unit).
  int guard = 0;
  while (idx.size() > 3 && guard++ < 4 * n) {
    const int m = static_cast<int>(idx.size());
    int found = -1;
    for (int pass = 0; pass < 2 && found < 0; ++pass) {
      std::vector<std::pair<double, int>> cand;
      for (int k = 0; k < m; ++k) {
        const int a = idx[static_cast<size_t>((k + m - 1) % m)], b = idx[static_cast<size_t>(k)], c = idx[static_cast<size_t>((k + 1) % m)];
        const double cr = cross3(a, b, c);
        if (pass == 0 ? !(cr > eps) : !(cr >= -eps)) continue;
        cand.emplace_back(prism::Len(p[static_cast<size_t>(c)] - p[static_cast<size_t>(a)]), k);
      }
      std::sort(cand.begin(), cand.end());
      for (const auto& kc : cand) {
        const int k = kc.second;
        const int a = idx[static_cast<size_t>((k + m - 1) % m)], b = idx[static_cast<size_t>(k)], c = idx[static_cast<size_t>((k + 1) % m)];
        bool ok = true;
        for (int t = 0; t < m && ok; ++t) {
          const int q = idx[static_cast<size_t>(t)];
          if (q == a || q == b || q == c) continue;
          if (pass == 0 ? inside(a, b, c, q) : (cross3(a, b, q) > eps && cross3(b, c, q) > eps && cross3(c, a, q) > eps)) ok = false;
        }
        if (ok) { found = k; break; }
      }
    }
    if (found < 0) return {};
    tris.push_back({idx[static_cast<size_t>((found + m - 1) % m)], idx[static_cast<size_t>(found)], idx[static_cast<size_t>((found + 1) % m)]});
    idx.erase(idx.begin() + found);
  }
  if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
  return tris;
}

struct PrismCageMesh {
  kernel::Mesh mesh;
  int columns = 0, rows = 0, corners = 0;
  std::vector<std::pair<ON_3dPoint, ON_3dPoint>> corner_edges;  // vertical wall edges at profile corners (float-rounded positions)
};

std::optional<PrismCageMesh> BuildPrismCage(const prism::Prism& pr, double dth, double s_line, int J) {
  const PrismColumns cols = DiscretizeProfile(pr.prof, dth, s_line);
  const int C = static_cast<int>(cols.pts.size());
  if (C < 3 || C > 1500 || J < 1) return std::nullopt;
  const double scale = prism::Scale(pr);
  const std::vector<std::array<int, 3>> tris = EarClip(cols.pts, scale);
  if (static_cast<int>(tris.size()) != C - 2) return std::nullopt;
  std::vector<ON_3dPoint> pos;
  pos.reserve(static_cast<size_t>(C) * (J + 1));
  for (int j = 0; j <= J; ++j)
    for (int c = 0; c < C; ++c) pos.push_back(prism::To3(pr, cols.pts[static_cast<size_t>(c)], pr.H * j / J));
  const auto vi = [&](int c, int j) { return j * C + (c % C); };
  std::vector<std::array<int, 4>> faces;
  for (int j = 0; j < J; ++j)
    for (int c = 0; c < C; ++c) faces.push_back({vi(c, j), vi(c + 1, j), vi(c + 1, j + 1), vi(c, j + 1)});
  for (const auto& t : tris) {
    faces.push_back({vi(t[0], J), vi(t[1], J), vi(t[2], J), vi(t[2], J)});  // top cap, counter-clockwise about the axis
    faces.push_back({vi(t[2], 0), vi(t[1], 0), vi(t[0], 0), vi(t[0], 0)});  // bottom cap, reversed
  }
  PrismCageMesh out;
  out.columns = C;
  out.rows = J;
  for (int c = 0; c < C; ++c) {
    if (!cols.sharp[static_cast<size_t>(c)]) continue;
    ++out.corners;
    for (int j = 0; j < J; ++j)
      out.corner_edges.emplace_back(ON_3dPoint(ON_3fPoint(pos[static_cast<size_t>(vi(c, j))])), ON_3dPoint(ON_3fPoint(pos[static_cast<size_t>(vi(c, j + 1))])));
  }
  out.mesh = MeshFromOrientedPolys(pos, faces);
  if (!out.mesh.IsClosedManifold()) return std::nullopt;
  return out;
}

// The exact prism as a measuring stick: a ruled strip over the finely sampled profile (exact for an
// extrusion, bar the arcs' chord sag) and the two exact cap planes with the profile polygon.
struct PrismReference {
  kernel::Mesh walls;
  std::vector<prism::V2> poly;
  double sag = 0.0;
  bool Inside(prism::V2 q) const {
    bool in = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
      if (((poly[i].y > q.y) != (poly[j].y > q.y)) && (q.x < (poly[j].x - poly[i].x) * (q.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) in = !in;
    }
    return in;
  }
};

std::optional<PrismReference> BuildPrismReference(const prism::Prism& pr, double tol) {
  PrismReference ref;
  for (const prism::Seg& g : pr.prof) {
    ref.poly.push_back(g.a);
    if (!g.arc) continue;
    const double s = tol / 10.0;
    const double dth = std::max(2.0 * std::acos(std::max(-1.0, 1.0 - s / g.r)), 1e-3);
    const int n = std::min(4096, std::max(2, static_cast<int>(std::ceil(prism::Sweep(g) / dth))));
    const double th = prism::Sweep(g), p0 = std::atan2(g.a.y - g.c.y, g.a.x - g.c.x);
    for (int t = 1; t < n; ++t) {
      const double p = p0 + g.s * th * t / n;
      ref.poly.push_back(g.c + prism::V2{g.r * std::cos(p), g.r * std::sin(p)});
    }
    ref.sag = std::max(ref.sag, g.r * (1.0 - std::cos(0.5 * th / n)));
  }
  const int C = static_cast<int>(ref.poly.size());
  if (C < 3) return std::nullopt;
  std::vector<ON_3dPoint> pos;
  for (int j = 0; j < 2; ++j)
    for (int c = 0; c < C; ++c) pos.push_back(prism::To3(pr, ref.poly[static_cast<size_t>(c)], j == 0 ? 0.0 : pr.H));
  ON_Mesh& raw = ref.walls.raw();
  for (const ON_3dPoint& p : pos) raw.m_V.Append(ON_3fPoint(p));
  const auto tri = [&](int a, int b, int c) {
    ON_MeshFace mf;
    mf.vi[0] = a; mf.vi[1] = b; mf.vi[2] = c; mf.vi[3] = c;
    raw.m_F.Append(mf);
  };
  for (int c = 0; c < C; ++c) {
    const int c1 = (c + 1) % C;
    tri(c, c1, C + c1);
    tri(c, C + c1, C + c);
  }
  return ref;
}

struct PrismCageResult {
  kernel::SubD subd;
  int cage_faces = 0, columns = 0, rows = 0, corners = 0, profile_segments = 0, arcs = 0;
  int crease_edges = 0, fit_iterations = 0, samples = 0;
  double fit_residual = 0.0, deviation = 0.0, reference_sag = 0.0;
  bool verified = false;
};

// The cage is only ever accepted on a ladder that CONVERGES. A rung that verifies with a comfortable
// margin (at most half the tolerance) is accepted as it stands. A rung that verifies only marginally
// is accepted only if the next finer rung also verifies and is no worse (then that finer rung is the
// result); a marginal rung whose finer neighbour fails or gets worse is luck, not convergence, and
// the whole cage is refused (`refused` says why) so ToSubD falls through to the dense path.
std::optional<PrismCageResult> PrismCageForBrep(const SceneObject& o, double tol, std::string* refused = nullptr) {
  if (!o.brep) return std::nullopt;
  const ON_Brep& b = o.brep->raw();
  if (b.m_F.Count() < 3) return std::nullopt;  // two caps + at least one wall face (an extruded polyline is ONE wall face)
  try {
    prism::Prism pr;
    std::string why;
    bool not_prism = false;
    if (!prism::Recognize(b, pr, why, not_prism)) return std::nullopt;
    {
      // A profile that is one circle (an extruded circle, a Cylinder) belongs to the revolved-solid cage.
      bool all_arcs_one_circle = true;
      for (const prism::Seg& g : pr.prof)
        if (!g.arc || prism::Len(g.c - pr.prof[0].c) > 1e-6 * prism::Scale(pr) || std::fabs(g.r - pr.prof[0].r) > 1e-6 * prism::Scale(pr)) { all_arcs_one_circle = false; break; }
      if (all_arcs_one_circle) return std::nullopt;
    }
    const std::optional<PrismReference> ref = BuildPrismReference(pr, tol);
    if (!ref) return std::nullopt;
    const kernel::MeshClosestPointIndex ref_index(ref->walls);
    double rmin = 0.0;
    int arcs = 0;
    for (const prism::Seg& g : pr.prof)
      if (g.arc) { rmin = arcs == 0 ? g.r : std::min(rmin, g.r); ++arcs; }
    const double scale = prism::Scale(pr);
    std::optional<PrismCageResult> best, marginal;
    struct Rung { double dth_deg; int rows; };
    for (const Rung rung : {Rung{7.5, 3}, Rung{4.5, 4}, Rung{3.0, 4}, Rung{2.0, 4}}) {
      const double dth = rung.dth_deg * 3.14159265358979 / 180.0;
      const double s_line = arcs > 0 ? rmin * dth : scale;
      const std::optional<PrismCageMesh> cage = BuildPrismCage(pr, dth, s_line, rung.rows);
      if (!cage) { if (!best && !marginal) return std::nullopt; break; }
      if (cage->mesh.FaceCount() > 20000) break;
      PrismCageResult cur;
      kernel::SubD sd = kernel::SubD::FromControlMesh(cage->mesh);
      cur.crease_edges = sd.SetCreasesByDihedral(kFidelityCreaseAngleDeg);
      int cr_ok = 0;
      for (const auto& e : cage->corner_edges) if (sd.SetCrease(e.first, e.second, true, 1e-5 * scale)) { ++cur.crease_edges; ++cr_ok; }
      kernel::SubDInterpolationReport frep;
      const kernel::BoundingBox cbb = cage->mesh.GetBoundingBox();
      const double extent = (cbb.max - cbb.min).Length();
      cur.subd = sd.InterpolateControlNet(std::max(tol * 0.05, 2e-7 * std::max(extent, 1.0)), 150, &frep);
      cur.fit_iterations = frep.iterations;
      cur.fit_residual = frep.final_max_residual;
      kernel::SubD probe = cur.subd;
      probe.Subdivide(2);
      const std::vector<kernel::SubDLimitPoint> lps = probe.LimitPoints();
      if (lps.empty()) return std::nullopt;
      const size_t step = std::max<size_t>(1, lps.size() / 40000);
      double dev = 0.0;
      for (size_t i = 0; i < lps.size(); i += step) {
        const Point3d& q = lps[i].limit_point;
        double d = (ref_index.ClosestPoint(q) - q).Length();
        const ON_3dVector v = ON_3dPoint(q.x, q.y, q.z) - pr.O;
        const double h = v * pr.n;
        const double dc = std::min(std::fabs(h), std::fabs(h - pr.H));
        if (dc < d && ref->Inside(prism::V2{v * pr.X, v * pr.Y})) d = dc;
        dev = std::max(dev, d);
        ++cur.samples;
      }
      cur.reference_sag = ref->sag;
      cur.deviation = dev + ref->sag;
      cur.verified = cur.deviation <= tol;
      cur.cage_faces = cage->mesh.FaceCount();
      cur.columns = cage->columns;
      cur.rows = cage->rows;
      cur.corners = cage->corners;
      cur.profile_segments = static_cast<int>(pr.prof.size());
      cur.arcs = arcs;
      if (marginal) {
        if (cur.verified && cur.deviation <= marginal->deviation) return cur;  // the finer neighbour confirms the ladder
        if (refused)
          *refused = "prism cage refused: the " + std::to_string(marginal->columns) + "-column rung verified only marginally (deviation " + std::to_string(marginal->deviation) +
                     ") and the next finer rung (" + std::to_string(cur.columns) + " columns) " + (cur.verified ? "got worse" : "does not verify") + " (deviation " + std::to_string(cur.deviation) +
                     "), so the ladder does not converge";
        return std::nullopt;
      }
      if (cur.verified) {
        if (cur.deviation <= 0.5 * tol) return cur;
        marginal = std::move(cur);
        continue;
      }
      best = std::move(cur);
    }
    if (marginal && refused) *refused = "prism cage refused: the finest rung verified only marginally (deviation " + std::to_string(marginal->deviation) + ") and there is no finer rung to confirm it";
    if (marginal) return std::nullopt;
    return best;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Joins coincident naked edges of `b` like JoinNakedEdges (cmd_common.h)
// but with a spatial hash on edge mid points instead of an all-pairs scan,
// so a thousand-patch SubD conversion joins in milliseconds instead of
// minutes. Same acceptance test (both end points within `tol`, mid points
// within 10*tol), same vertex/edge merging, same face re-orientation.
int JoinNakedEdgesHashed(ON_Brep& b, double tol) {
  struct Cell { long long x, y, z; bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; } };
  struct CellHash {
    size_t operator()(const Cell& c) const {
      return static_cast<size_t>(c.x * 73856093LL) ^ static_cast<size_t>(c.y * 19349663LL) ^ static_cast<size_t>(c.z * 83492791LL);
    }
  };
  const double cell = std::max(tol * 40.0, 1e-9);
  const auto cell_of = [&](const ON_3dPoint& p) {
    return Cell{static_cast<long long>(std::floor(p.x / cell)), static_cast<long long>(std::floor(p.y / cell)), static_cast<long long>(std::floor(p.z / cell))};
  };
  std::unordered_map<Cell, std::vector<int>, CellHash> grid;
  std::vector<int> naked;
  for (int i = 0; i < b.m_E.Count(); ++i) {
    const ON_BrepEdge& e = b.m_E[i];
    if (e.m_edge_index < 0 || e.TrimCount() != 1) continue;
    naked.push_back(i);
    grid[cell_of(e.PointAt(e.Domain().Mid()))].push_back(i);
  }
  int joined = 0;
  for (int i : naked) {
    ON_BrepEdge& e0 = b.m_E[i];
    if (e0.m_edge_index < 0 || e0.TrimCount() != 1) continue;
    const ON_3dPoint a0 = e0.PointAtStart(), a1 = e0.PointAtEnd();
    const ON_3dPoint m0 = e0.PointAt(e0.Domain().Mid());
    const Cell c0 = cell_of(m0);
    bool done = false;
    for (long long dx = -1; dx <= 1 && !done; ++dx)
      for (long long dy = -1; dy <= 1 && !done; ++dy)
        for (long long dz = -1; dz <= 1 && !done; ++dz) {
          const auto it = grid.find(Cell{c0.x + dx, c0.y + dy, c0.z + dz});
          if (it == grid.end()) continue;
          for (int j : it->second) {
            if (j == i) continue;
            ON_BrepEdge& e1 = b.m_E[j];
            if (e1.m_edge_index < 0 || e1.TrimCount() != 1) continue;
            const ON_3dPoint b0 = e1.PointAtStart(), b1 = e1.PointAtEnd();
            bool forward = a0.DistanceTo(b0) <= tol && a1.DistanceTo(b1) <= tol;
            bool reversed = !forward && a0.DistanceTo(b1) <= tol && a1.DistanceTo(b0) <= tol;
            if (forward && a0.DistanceTo(a1) <= tol) {
              forward = ON_DotProduct(e0.TangentAt(e0.Domain().Min()), e1.TangentAt(e1.Domain().Min())) > 0;
              reversed = !forward;
            }
            if (!forward && !reversed) continue;
            const ON_3dPoint m1 = e1.PointAt(e1.Domain().Mid());
            if (m0.DistanceTo(m1) > tol * 10) continue;
            if (reversed && !e1.Reverse()) continue;
            for (int k = 0; k < 2; ++k) {
              if (e0.m_vi[k] == e1.m_vi[k]) continue;
              if (!b.CombineCoincidentVertices(b.m_V[e0.m_vi[k]], b.m_V[e1.m_vi[k]])) break;
            }
            if (b.CombineCoincidentEdges(e0, e1)) { ++joined; done = true; break; }
          }
        }
  }
  if (joined) {
    OrientBrepFaces(b);
    b.SetTolerancesBoxesAndFlags();
  }
  return joined;
}

// The SubD -> NURBS conversion ToNURBS uses: kernel
// SubD::ToNurbsPatchesWatertight() (every SubD edge has ONE canonical
// cubic boundary curve shared bit-for-bit by both faces that meet there),
// the patches appended into one ON_Brep, edge-matched with a strict 1e-6
// tolerance (NOT the loose 10*modeling-tolerance the adaptive path needed),
// and MEASURED: naked edge count after the join, whether the polysurface is
// a closed solid, and the largest distance of patch sample points from a
// Subdivide(+2) reference of the same SubD. Throws on kernel failure so
// the caller can fall back to the adaptive path.
struct WatertightNurbsResult {
  ON_Brep brep;
  kernel::SubDWatertightReport report;
  int levels = 0;
  int joined = 0;
  int naked_edges = 0;
  bool is_solid = false;
  bool is_manifold = false;
  double max_deviation = 0.0;
  int samples = 0;
};

WatertightNurbsResult ConvertSubDWatertight(const kernel::SubD& subd) {
  WatertightNurbsResult r;
  kernel::SubD refined = subd;
  int faces = refined.FaceCount();
  while (r.levels < 3 && faces > 0 && faces * 4 <= 6000) { faces *= 4; ++r.levels; }
  if (r.levels == 0) r.levels = 1;  // always at least one round, to guarantee quad faces
  refined.Subdivide(r.levels);
  std::vector<kernel::SubDNurbsPatch> patches = refined.ToNurbsPatchesWatertight(&r.report);
  for (kernel::SubDNurbsPatch& p : patches) {
    ON_Brep tmp;
    ON_NurbsSurface* srf = new ON_NurbsSurface(p.surface.raw());
    tmp.Create(srf);
    r.brep.Append(tmp);
  }
  r.joined = JoinNakedEdgesHashed(r.brep, 1e-6);
  for (int i = 0; i < r.brep.m_E.Count(); ++i) {
    if (r.brep.m_E[i].m_edge_index >= 0 && r.brep.m_E[i].TrimCount() == 1) ++r.naked_edges;
  }
  r.is_solid = r.brep.IsSolid();
  r.is_manifold = r.brep.IsManifold();
  // Deviation from the limit surface: sample every patch on a 3x3 grid
  // (bounded) and measure against a finer refinement of the same SubD.
  try {
    kernel::SubD fine = refined;
    int ff = fine.FaceCount(), extra = 0;
    while (extra < 2 && ff * 4 <= 120000) { ff *= 4; ++extra; }
    if (extra > 0) {
      fine.Subdivide(extra);
      const kernel::Mesh ref = fine.ToApproximateMesh();
      const size_t stride = std::max<size_t>(1, patches.size() / 4000);
      for (size_t i = 0; i < patches.size(); i += stride) {
        for (int a = 0; a <= 2; ++a)
          for (int c = 0; c <= 2; ++c) {
            const Point3d q = patches[i].surface.PointAt(a / 2.0, c / 2.0);
            r.max_deviation = std::max(r.max_deviation, (ref.ClosestPoint(q) - q).Length());
            ++r.samples;
          }
      }
    }
  } catch (const std::exception&) {}
  return r;
}

void MeshFromSelection(CommandContext& ctx, const std::vector<ObjectId>& ids, const char* label, bool to_subd) {
  ctx.Doc().BeginChange(label);
  int made = 0;
  int fidelity_checked = 0, fidelity_verified = 0;
  double fidelity_worst = 0.0;
  int fit_iterations_max = 0, creases_total = 0, fit_runs = 0, samples_total = 0;
  double fit_residual_worst = 0.0;
  int exact_runs = 0, exact_creases_total = 0, exact_samples_total = 0;
  int rev_runs = 0, rev_creases_total = 0, rev_samples_total = 0, rev_fit_iterations_max = 0;
  int rev_around = 0, rev_along = 0, rev_ref_around = 0, rev_ref_along = 0;
  double rev_fit_residual_worst = 0.0, rev_ref_sag = 0.0;
  int tor_runs = 0, tor_around = 0, tor_along = 0, tor_ref_around = 0, tor_ref_along = 0, tor_fit_iterations = 0, tor_samples = 0;
  double tor_fit_residual = 0.0, tor_ref_sag = 0.0;
  int pri_runs = 0, pri_cols = 0, pri_rows = 0, pri_corners = 0, pri_segments = 0, pri_arcs = 0, pri_creases = 0, pri_fit_iterations = 0, pri_samples = 0;
  double pri_fit_residual = 0.0, pri_ref_sag = 0.0;
  std::string pri_refused;  // why the right-prism cage was refused (the object then took the dense path)
  int cur_runs = 0, cur_cvs = 0, cur_degree = 0, cur_rings = 0, cur_around = 0, cur_caps = 0, cur_poles = 0, cur_faces = 0, cur_fit_iterations = 0, cur_samples = 0, cur_creases = 0;
  int cur_ref_around = 0, cur_ref_along = 0;
  double cur_fit_residual = 0.0, cur_ref_sag = 0.0;
  std::string cur_refused;  // why the curved-profile revolve cage was refused (the object then took the existing path)
  // ToSubD/MeshToSubD (to_subd) tessellate a Brep's real surface at the
  // document's own modeling tolerance (ctx.Settings().absolute_tolerance)
  // rather than surface_display_tolerance - a viewport-quality setting
  // with no geometric meaning, which previously gave this conversion's
  // control cage no actual fidelity bound at all (only "whatever happens
  // to look right on screen"). MeshBrepClosed's own chord_tolerance option
  // (threaded through MeshOf, cmd_common.h) bounds every tessellated
  // vertex to within that tolerance of the true Brep surface, so the
  // resulting SubD's CONTROL CAGE now has a real, documented fidelity
  // guarantee tied to the document's tolerance. This still doesn't cover
  // the whole category gap: Catmull-Clark smoothing pulls the SubD's own
  // LIMIT surface further from that control cage than the tessellation
  // tolerance alone bounds, so the plain "Mesh" command (to_subd == false,
  // whose own output IS the final shape) keeps using the display
  // tolerance unchanged. For a Brep operand specifically (the only input
  // kind with a true surface to measure the result against),
  // BuildSubDVerifiedAgainstBrep() above now closes that remaining half
  // too - see its own doc comment.
  const double tol = to_subd ? ctx.Settings().absolute_tolerance : ctx.App().surface_display_tolerance;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    SceneObject n;
    if (to_subd && o->kind == ObjectKind::Brep) {
      // Exact path first: SubD::FromBrep (dino8-kernel/include/dino8/
      // kernel/subd.h) builds the Catmull-Clark cage directly from the
      // Brep's own faces with no tessellation tolerance involved at all -
      // exact rather than merely verified-within-tolerance - but only for
      // a Brep whose every face is already that one exact shape
      // (untrimmed, planar, axis-aligned-in-own-uv, 4 corners - the same
      // shape Box()'s own faces are built from); it throws naming the
      // offending face for anything else (a curved face, a trim, a
      // non-quad face), so this falls through to the existing
      // tessellate-and-verify path below for every other Brep rather than
      // needing a separate command. `divisions = 1` gives one SubD face
      // per original Brep face - the natural, most editable cage for the
      // shape this accepts, matching how a plain box's own faces already
      // read.
      //
      // FromBrep creases only NAKED edges, so the cage is then given
      // creases along the Brep's sharp edges and its limit surface is
      // MEASURED against the Brep (ExactCageForBrep above) - the old
      // unconditional "++fidelity_verified" claimed a verification that
      // was never made, for a closed box whose uncreased limit surface is
      // a rounded blob.
      const std::optional<ExactCageResult> exact = ExactCageForBrep(*o, tol);
      if (exact) {
        n = SceneObject::MakeSubD(exact->subd);
        ++fidelity_checked;
        if (exact->verified) ++fidelity_verified;
        fidelity_worst = std::max(fidelity_worst, exact->deviation);
        ++exact_runs;
        exact_creases_total += exact->crease_edges;
        exact_samples_total += exact->samples;
      } else if (const std::optional<PrismCageResult> pri = PrismCageForBrep(*o, tol, &pri_refused); pri && pri->verified) {
        // A right prism over a profile of lines and circular arcs (see PrismCageForBrep); verified only.
        n = SceneObject::MakeSubD(pri->subd);
        ++fidelity_checked;
        ++fidelity_verified;
        fidelity_worst = std::max(fidelity_worst, pri->deviation);
        ++pri_runs;
        pri_cols = pri->columns;
        pri_rows = pri->rows;
        pri_corners = pri->corners;
        pri_segments = pri->profile_segments;
        pri_arcs = pri->arcs;
        pri_creases += pri->crease_edges;
        pri_fit_iterations = std::max(pri_fit_iterations, pri->fit_iterations);
        pri_fit_residual = std::max(pri_fit_residual, pri->fit_residual);
        pri_samples += pri->samples;
        pri_ref_sag = std::max(pri_ref_sag, pri->reference_sag);
      } else if (const std::optional<AnalyticCageResult> crv = RevolveCurvedCageForBrep(*o, tol, &cur_refused); crv && crv->verified) {
        // A revolve of a curved profile (a vase, a bottle, a sphere, a dome): rings follow the profile's control
        // polygon, see RevolveCurvedCageForBrep; verified only.
        n = SceneObject::MakeSubD(crv->subd);
        ++fidelity_checked;
        ++fidelity_verified;
        fidelity_worst = std::max(fidelity_worst, crv->deviation);
        ++cur_runs;
        cur_cvs = crv->profile_cvs;
        cur_degree = crv->profile_degree;
        cur_rings = crv->rings;
        cur_around = crv->cage_around;
        cur_caps = crv->caps;
        cur_poles = crv->poles;
        cur_faces = crv->cage_faces;
        cur_creases += crv->crease_edges;
        cur_fit_iterations = std::max(cur_fit_iterations, crv->fit_iterations);
        cur_fit_residual = std::max(cur_fit_residual, crv->fit_residual);
        cur_samples += crv->samples;
        cur_ref_around = crv->reference_around;
        cur_ref_along = crv->reference_along;
        cur_ref_sag = std::max(cur_ref_sag, crv->reference_sag);
      } else if (const std::optional<AnalyticCageResult> rev = AnalyticCageForBrep(*o, tol)) {
        // A revolved solid (Cylinder, Cone, TCone...): the cage is built from
        // the Brep's own wall parametrisation and shared rim ring, see
        // BuildRevolvedCage above.
        n = SceneObject::MakeSubD(rev->subd);
        ++fidelity_checked;
        if (rev->verified) ++fidelity_verified;
        fidelity_worst = std::max(fidelity_worst, rev->deviation);
        ++rev_runs;
        rev_creases_total += rev->crease_edges;
        rev_samples_total += rev->samples;
        rev_fit_iterations_max = std::max(rev_fit_iterations_max, rev->fit_iterations);
        rev_fit_residual_worst = std::max(rev_fit_residual_worst, rev->fit_residual);
        rev_around = rev->cage_around;
        rev_along = rev->cage_along;
        rev_ref_around = rev->reference_around;
        rev_ref_along = rev->reference_along;
        rev_ref_sag = std::max(rev_ref_sag, rev->reference_sag);
      } else if (const std::optional<AnalyticCageResult> tor = TorusCageForBrep(*o, tol); tor && tor->verified) {
        // A torus: the face's own N x M parametrisation grid (see TorusCageForBrep). Only a VERIFIED
        // cage is used; anything else falls through to the unchanged grid-tessellation path.
        n = SceneObject::MakeSubD(tor->subd);
        ++fidelity_checked;
        ++fidelity_verified;
        fidelity_worst = std::max(fidelity_worst, tor->deviation);
        ++tor_runs;
        tor_around = tor->cage_around;
        tor_along = tor->cage_along;
        tor_ref_around = tor->reference_around;
        tor_ref_along = tor->reference_along;
        tor_fit_iterations = std::max(tor_fit_iterations, tor->fit_iterations);
        tor_fit_residual = std::max(tor_fit_residual, tor->fit_residual);
        tor_samples += tor->samples;
        tor_ref_sag = std::max(tor_ref_sag, tor->reference_sag);
      } else {
        std::optional<SubDFidelityResult> fr = BuildSubDVerifiedAgainstBrep(*o, tol);
        if (!fr) continue;
        ++fidelity_checked;
        if (fr->verified) ++fidelity_verified;
        fidelity_worst = std::max(fidelity_worst, fr->achieved_deviation);
        fit_iterations_max = std::max(fit_iterations_max, fr->fit_iterations);
        fit_residual_worst = std::max(fit_residual_worst, fr->fit_residual);
        creases_total += fr->crease_edges;
        samples_total += fr->samples;
        ++fit_runs;
        n = SceneObject::MakeSubD(fr->subd);
      }
    } else {
      std::optional<kernel::Mesh> m = MeshOf(*o, tol);
      if (!m || m->FaceCount() == 0) continue;
      n = to_subd ? SceneObject::MakeSubD(kernel::SubD::FromControlMesh(*m)) : SceneObject::MakeMesh(*m);
    }
    n.layer_index = o->layer_index;
    n.name = o->name;
    ctx.Doc().Add(std::move(n));
    ++made;
  }
  std::string msg = std::string(label) + ": created " + std::to_string(made) + " object(s)";
  if (fidelity_checked > 0) {
    msg += " (" + std::to_string(fidelity_verified) + " of " + std::to_string(fidelity_checked) +
           " limit-surface-fidelity verified within tolerance " + std::to_string(tol) +
           ", worst measured deviation " + std::to_string(fidelity_worst) + ")";
    if (fit_runs > 0) {
      msg += "; cage interpolated onto the Brep surface (limit surface passes through every cage vertex: " +
             std::to_string(fit_iterations_max) + " iteration(s), residual " + std::to_string(fit_residual_worst) + "), " +
             std::to_string(creases_total) + " feature edge(s) creased, deviation measured at " + std::to_string(samples_total) +
             " exact limit-surface points";
    }
    if (rev_runs > 0) {
      msg += "; analytic revolved-solid cage (" + std::to_string(rev_around) + " segments around x " + std::to_string(rev_along) +
             " along the wall, all-quad O-grid caps welded to the wall's rim ring), cage interpolated onto the Brep surface (" +
             std::to_string(rev_fit_iterations_max) + " iteration(s), residual " + std::to_string(rev_fit_residual_worst) + "), " +
             std::to_string(rev_creases_total) + " feature edge(s) creased; deviation measured at " + std::to_string(rev_samples_total) +
             " exact limit-surface points against the exact surface sampled " + std::to_string(rev_ref_around) + " x " +
             std::to_string(rev_ref_along) + " (planar caps exact), reference chord sag " + std::to_string(rev_ref_sag) +
             " included in the deviation";
    }
    if (tor_runs > 0) {
      msg += "; analytic torus cage (" + std::to_string(tor_around) + " x " + std::to_string(tor_along) +
             " quad grid of the face's own parametrisation, every vertex valence 4, no creases), cage interpolated onto the Brep surface (" +
             std::to_string(tor_fit_iterations) + " iteration(s), residual " + std::to_string(tor_fit_residual) + "); deviation measured at " +
             std::to_string(tor_samples) + " exact limit-surface points against the exact surface sampled " + std::to_string(tor_ref_around) + " x " +
             std::to_string(tor_ref_along) + ", reference chord sag " + std::to_string(tor_ref_sag) + " included in the deviation";
    }
    if (pri_runs > 0) {
      msg += "; analytic right-prism cage (profile of " + std::to_string(pri_segments) + " segment(s), " + std::to_string(pri_arcs) + " of them arcs: " +
             std::to_string(pri_cols) + " columns x " + std::to_string(pri_rows) + " rows, flat caps ear-clipped from the rim, " + std::to_string(pri_corners) +
             " profile corner(s)), cage interpolated onto the Brep surface (" + std::to_string(pri_fit_iterations) + " iteration(s), residual " +
             std::to_string(pri_fit_residual) + "), " + std::to_string(pri_creases) + " feature edge(s) creased; deviation measured at " +
             std::to_string(pri_samples) + " exact limit-surface points against the exact prism (ruled wall strip + exact cap planes), reference chord sag " +
             std::to_string(pri_ref_sag) + " included in the deviation";
    }
    if (cur_runs > 0) {
      msg += "; curved-profile revolve cage (profile of " + std::to_string(cur_cvs) + " control point(s), degree " + std::to_string(cur_degree) + ": " +
             std::to_string(cur_rings) + " rings (control-point spans x a refinement factor, equally spaced in profile arc length, clustered toward the ends) x " + std::to_string(cur_around) +
             " segments around, " + std::to_string(cur_caps) + " flat O-grid cap(s), " + (cur_poles > 0 ? "sharp pole tip" : "no sharp pole tip") + ", " +
             std::to_string(cur_faces) + " cage faces), cage interpolated onto the Brep surface (" + std::to_string(cur_fit_iterations) +
             " iteration(s), residual " + std::to_string(cur_fit_residual) + "), " + std::to_string(cur_creases) +
             " feature edge(s) creased; deviation measured at " + std::to_string(cur_samples) + " exact limit-surface points against the exact surface sampled " +
             std::to_string(cur_ref_around) + " x " + std::to_string(cur_ref_along) + ", reference chord sag " + std::to_string(cur_ref_sag) + " included in the deviation";
    }
    if (!cur_refused.empty()) msg += "; " + cur_refused;
    if (!pri_refused.empty()) msg += "; " + pri_refused;
    if (exact_runs > 0) {
      msg += "; exact Brep-face cage (no tessellation), " + std::to_string(exact_creases_total) +
             " sharp edge(s) creased (dihedral above " + std::to_string(static_cast<int>(kFidelityCreaseAngleDeg)) +
             " degrees), deviation measured at " + std::to_string(exact_samples_total) + " exact limit-surface points";
    }
  }
  ctx.Print(msg);
}

}  // namespace

// Cap's per-object construction, shared by the live command and UpdateHistory
// (history_rebuild.h): caps every planar closed naked-edge loop of an open
// polysurface on a COPY of it. `caps_here` receives the number of loops capped;
// nullopt (with `why`) when the object is not an open polysurface or no loop
// could be capped.
std::optional<SceneObject> CapObject(CommandContext& ctx, const SceneObject& src, int& caps_here, std::string& why) {
  caps_here = 0;
  if (src.kind != ObjectKind::Brep || !src.brep) { why = std::string("the object is ") + HistoryKindName(src.kind) + ", not an open polysurface"; return std::nullopt; }
  kernel::Brep nb;
  nb.raw() = src.brep->raw();
  ON_Brep& b = nb.raw();
  if (b.IsSolid()) { why = "the object is already a closed solid, there is nothing to cap"; return std::nullopt; }
  std::vector<int> naked;
  for (int ei = 0; ei < b.m_E.Count(); ++ei) if (b.m_E[ei].TrimCount() == 1) naked.push_back(ei);
  if (naked.empty()) { why = "the object has no naked edges"; return std::nullopt; }
      // Group the naked edges into their separate closed loops by shared
      // vertices (a polysurface can have more than one hole to cap), then
      // walk each loop tip-to-tail so its polyline is in order instead of
      // just concatenating edges in array order (which only happened to
      // work when there was exactly one loop).
      std::vector<bool> used(naked.size(), false);
      std::unordered_map<int, std::vector<int>> by_vertex;  // vertex index -> positions in naked[]
      for (size_t i = 0; i < naked.size(); ++i) {
        const ON_BrepEdge& ed = b.m_E[naked[i]];
        by_vertex[ed.m_vi[0]].push_back(static_cast<int>(i));
        by_vertex[ed.m_vi[1]].push_back(static_cast<int>(i));
      }
      for (size_t start = 0; start < naked.size(); ++start) {
        if (used[start]) continue;
        std::vector<int> loop_idx{static_cast<int>(start)};
        std::vector<bool> loop_rev{false};
        used[start] = true;
        const ON_BrepEdge& first = b.m_E[naked[start]];
        const int loop_start_vertex = first.m_vi[0];
        int end_vertex = first.m_vi[1];
        while (end_vertex != loop_start_vertex) {
          int next = -1;
          for (int cand : by_vertex[end_vertex]) if (!used[cand]) { next = cand; break; }
          if (next < 0) break;  // dangling chain: not a closed loop
          used[next] = true;
          const ON_BrepEdge& ne = b.m_E[naked[next]];
          const bool rev = (ne.m_vi[0] != end_vertex);
          loop_idx.push_back(next);
          loop_rev.push_back(rev);
          end_vertex = rev ? ne.m_vi[0] : ne.m_vi[1];
        }
        if (end_vertex != loop_start_vertex) continue;  // open chain, can't cap
        ON_Polyline pl;
        for (size_t k = 0; k < loop_idx.size(); ++k) {
          const ON_BrepEdge& ed = b.m_E[naked[loop_idx[k]]];
          for (int s = 0; s < 8; ++s) {
            const double t = loop_rev[k] ? (1.0 - s / 8.0) : (s / 8.0);
            pl.Append(ed.PointAt(ed.Domain().ParameterAt(t)));
          }
        }
        ON_Plane plane;
        if (!pl.IsClosed(ctx.Settings().absolute_tolerance)) pl.Append(pl[0]);
        ON_PolylineCurve pc(pl);
        if (pc.IsPlanar(&plane, ctx.Settings().absolute_tolerance * 10)) {
          if (ON_Brep* cap = ON_BrepTrimmedPlane(plane, pc)) { b.Append(*cap); delete cap; ++caps_here; }
        }
      }
  if (caps_here == 0) { why = "none of its " + std::to_string(naked.size()) + " naked edge(s) forms a closed planar loop"; return std::nullopt; }
  JoinNakedEdges(b, ctx.Settings().absolute_tolerance * 10);
  return SceneObject::MakeBrep(nb);
}

std::optional<SceneObject> RebuildCap(CommandContext& ctx, const HistoryInputs& in, const HistoryRecord&, std::string& why) {
  if (in.size() != 1) { why = "expected 1 source object, the record has " + std::to_string(in.size()); return std::nullopt; }
  int caps = 0;
  std::optional<SceneObject> r = CapObject(ctx, *in[0], caps, why);
  if (!r && !why.empty()) why = "source object " + std::to_string(in[0]->id) + ": " + why;
  return r;
}

void RegisterSolidCommands(CommandEngine& e) {
  Reg(e, "Box", Make<BoxCommand>(BoxCommand::Out::Brep));
  Reg(e, "MeshBox", Make<BoxCommand>(BoxCommand::Out::Mesh));
  Reg(e, "SubDBox", Make<BoxCommand>(BoxCommand::Out::SubD));
  Reg(e, "Sphere", SphereLike("Sphere", [](CommandContext& ctx, Point3d c, double r) { AddBrep(ctx, ON_BrepSphere(ON_Sphere(c, r)), "Sphere"); }));
  Reg(e, "MeshSphere", SphereLike("MeshSphere", [](CommandContext& ctx, Point3d c, double r) { AddObject(ctx, SceneObject::MakeMesh(kernel::Brep::Sphere(c, r).TessellateToClosedMesh(24, 12)), "MeshSphere"); }));
  Reg(e, "SubDSphere", SphereLike("SubDSphere", [](CommandContext& ctx, Point3d c, double r) { AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Brep::Sphere(c, r).TessellateToClosedMesh(8, 4))), "SubDSphere"); }));
  Reg(e, "Ellipsoid", Make<EllipsoidCommand>(false), CommandStatus::Implemented, "Center, then the three semi-axis end points (real axis picking, any orientation).");
  Reg(e, "SubDEllipsoid", Make<EllipsoidCommand>(true), CommandStatus::Implemented, "Center, then the three semi-axis end points (real axis picking, any orientation).");
  Reg(e, "Cylinder", Make<CylinderCommand>(CylinderCommand::Kind::Cylinder));
  Reg(e, "Cone", Make<CylinderCommand>(CylinderCommand::Kind::Cone));
  Reg(e, "TCone", Make<CylinderCommand>(CylinderCommand::Kind::TCone));
  Reg(e, "Tube", Make<CylinderCommand>(CylinderCommand::Kind::Tube));
  Reg(e, "Pyramid", Make<CylinderCommand>(CylinderCommand::Kind::Pyramid), CommandStatus::Implemented, "Mesh pyramid; NumSides= sets the base polygon's side count (4 by default).");
  Reg(e, "MeshCylinder", Make<CylinderCommand>(CylinderCommand::Kind::MeshCylinder));
  Reg(e, "MeshCone", Make<CylinderCommand>(CylinderCommand::Kind::MeshCone));
  Reg(e, "SubDCylinder", Make<CylinderCommand>(CylinderCommand::Kind::SubDCylinder));
  Reg(e, "SubDCone", Make<CylinderCommand>(CylinderCommand::Kind::SubDCone));
  Reg(e, "Torus", Make<TorusCommand>(TorusCommand::Kind::Brep));
  Reg(e, "MeshTorus", Make<TorusCommand>(TorusCommand::Kind::Mesh));
  Reg(e, "SubDTorus", Make<TorusCommand>(TorusCommand::Kind::SubD));
  Reg(e, "ExtrudeCrv", Make<ExtrudeCommand>(ExtrudeCommand::Kind::Curve));
  Reg(e, "Extrude", Make<ExtrudeCommand>(ExtrudeCommand::Kind::Curve));
  Reg(e, "ExtrudeSrf", Make<ExtrudeCommand>(ExtrudeCommand::Kind::Surface));
  Reg(e, "ExtrudeCrvToPoint", Make<ExtrudeCommand>(ExtrudeCommand::Kind::ToPoint));
  Reg(e, "Revolve", Make<RevolveCommand>());
  Reg(e, "Loft", []() -> std::unique_ptr<Command> {
    return std::make_unique<ContinuityPickCommand>("Loft", "Select curves to loft in order", 2,
        [](CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<std::string>& extra) { const LoftTokens t = ParseLoftTokens(extra); Loft(ctx, ids, false, t.straight, t.cap); },
        [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ContinuityRequest& req, const AdjacentEdge& edge, const std::vector<std::string>& extra) {
          LoftContinuity(ctx, ids, req, edge, ParseLoftTokens(extra).straight);
        });
  }, CommandStatus::Implemented, "Calls the kernel's exact interpolating Brep::Loft: every selected curve lies exactly on the result (measured and printed), Normal is cubic in the loft direction and Style=Straight is linear (sharp at every section). Curves are first made consistent - open curves reversed to run the same way, closed curves reversed to circulate the same way and re-seamed to the point nearest the previous curve's start; a mix of open and closed curves is refused with the reason. Closed planar curves are capped into a polysurface solid (Cap=Yes, the default; Cap=No leaves the periodic surface). Loose/Tight aren't distinguishable here and aren't offered. If the kernel refuses a loft, the older control-point fit is used and the reason printed. Continuity=Position|Tangency|Curvature (G0/G1/G2, with AngleTol= in degrees and CurvTol= in 1/length; none given = everything above unchanged) asks for a click near an edge of an existing surface and calls the kernel's NurbsSurface::LoftMatched: the first or last section (whichever lies nearer that surface) is made identical to the edge and the next rows are rewritten by MatchEdge; the finished uncapped surface is MEASURED along the edge (position gap, tangent angle, cross-edge and Gaussian curvature difference) and every section's deviation from it is printed, and the request is refused with the kernel's reason (G0 not met, a closed section, a loft folding back over the adjacent surface, a degree too low, a measured angle / curvature above tolerance) building nothing. A continuity result is not recorded in History (the reason is printed).");
  Reg(e, "SubDLoft", OnSelection("Select curves to loft in order", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Loft(ctx, ids, true); }, 2));
  Reg(e, "PlanarSrf", OnSelection("Select planar closed curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("PlanarSrf");
        int made = 0, self_int = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Curve) continue;
          ON_Plane pl;
          if (!o->curve->raw().IsClosed() || !o->curve->raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance)) continue;
          // ON_BrepTrimmedPlane has no notion of a self-crossing boundary -
          // it would hand back a Brep whose single trim loop crosses
          // itself in 2D, silently reported as a normal planar surface.
          // Reject that here instead of shipping the corrupt result.
          if (CurveSelfIntersects(*o->curve, std::max(ctx.Settings().absolute_tolerance, 1e-9))) { ++self_int; continue; }
          if (std::optional<SceneObject> built = RebuildPlanarSrf(ctx, *o->curve, HistoryRecord{})) {
            const ObjectId new_id = ctx.Doc().Add(std::move(*built));
            RecordHistoryIfEnabled(ctx, new_id, "PlanarSrf", {id}, {});
            ++made;
          }
        }
        if (made == 0 && self_int > 0) ctx.Warn("PlanarSrf: " + std::to_string(self_int) + " curve(s) cross themselves - a planar surface needs a simple (non-self-intersecting) boundary");
        else if (made == 0) ctx.Warn("Select closed planar curves");
        else ctx.Print("Created " + std::to_string(made) + " planar surface(s)" + (self_int > 0 ? " (" + std::to_string(self_int) + " self-intersecting curve(s) skipped)" : ""));
      }));
  Reg(e, "Mesh", OnSelection("Select surfaces, polysurfaces or SubDs to mesh", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MeshFromSelection(ctx, ids, "Mesh", false); }));
  Reg(e, "ToSubD", OnSelection("Select meshes or polysurfaces to convert", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MeshFromSelection(ctx, ids, "ToSubD", true); }));
  Reg(e, "MeshToSubD", OnSelection("Select meshes to convert", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MeshFromSelection(ctx, ids, "MeshToSubD", true); }));
  Reg(e, "ToNURBS", OnSelection("Select SubDs or meshes to convert", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ToNURBS");
        int made = 0, exact_patches = 0, approx_patches = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          if (o->kind == ObjectKind::SubD) {
            // Real Catmull-Clark limit-surface conversion (kernel::SubD::
            // ToNurbsPatchesAdaptive(), see its own doc comment for the
            // math): every face whose 4 corners are ordinary interior
            // vertices gets its mathematically exact bicubic Bezier patch;
            // every face touching an extraordinary vertex, crease, or
            // boundary has no such closed form, so ToNurbsPatchesAdaptive()
            // recursively splits just that irregular region (not the whole
            // mesh) into progressively smaller exact regular patches plus
            // one still-approximated flat patch at its core - the same
            // local technique EvaluateFace() uses for a single (u, v)
            // query, applied to every (u, v) of the face at once. First
            // subdivide (Catmull-Clark refinement, real and non-stub - see
            // the class comment on kernel::SubD) a few rounds so (a)
            // triangle/n-gon input becomes quad-only, and (b) the
            // irregular patches shrink to a tight tolerance before the
            // adaptive split further narrows them - each round (whole-mesh
            // or adaptive) roughly quarters the remaining irregular
            // region's maximum deviation from the true limit surface (see
            // ToNurbsPatches()'s doc comment).
            // Watertight path first (see ConvertSubDWatertight above): every
            // SubD edge has one canonical boundary curve shared by both
            // faces, so a closed SubD converts to a closed polysurface. The
            // adaptive converter below stays as the fallback when the
            // watertight one throws (e.g. a non-quad face it cannot handle).
            bool watertight_done = false;
            try {
              const int src_faces = o->subd->FaceCount();
              WatertightNurbsResult wr = ConvertSubDWatertight(*o->subd);
              kernel::Brep k; k.raw() = wr.brep;
              ctx.Doc().Add(SceneObject::MakeBrep(k));
              ++made;
              exact_patches += wr.report.exact_patches;
              approx_patches += wr.report.coons_patches;
              watertight_done = true;
              ctx.Print("ToNURBS: watertight conversion of a " + std::to_string(src_faces) + "-face SubD (" + std::to_string(wr.levels) +
                         " Catmull-Clark refinement round(s) first): " + std::to_string(wr.report.patches) + " bicubic patches (" +
                         std::to_string(wr.report.exact_patches) + " exact limit patches, " + std::to_string(wr.report.coons_patches) +
                         " Coons patches on the shared edge curves; " + std::to_string(wr.report.exact_edge_curves) + " exact + " +
                         std::to_string(wr.report.hermite_edge_curves) + " Hermite edge curves), joined " + std::to_string(wr.joined) +
                         " edge pair(s) at 1e-6, " + std::to_string(wr.naked_edges) + " naked edge(s) left, " +
                         (wr.is_solid ? "closed solid: yes" : "closed solid: no") + (wr.is_manifold ? ", manifold" : ", non-manifold") +
                         "; max distance of " + std::to_string(wr.samples) + " patch samples from the Subdivide(+2) limit reference " +
                         std::to_string(wr.max_deviation));
            } catch (const std::exception&) {
              watertight_done = false;
            }
            if (!watertight_done) {
            kernel::SubD refined = *o->subd;
            // Cap subdivision rounds (and hence patch count) much lower than
            // the old dense-mesh path's cap: each patch here is a real NURBS
            // surface, not a mesh triangle, and JoinNakedEdges (below) is
            // O(edge_count^2), so a handful of rounds is both plenty to
            // shrink irregular patches to a tight tolerance (see
            // ToNurbsPatches()'s doc comment) and keeps patch/edge counts
            // practical.
            int faces = refined.FaceCount(), levels = 0;
            while (levels < 2 && faces > 0 && faces * 4 <= 4000) { faces *= 4; ++levels; }
            if (levels == 0) levels = 1;  // always at least one round, to guarantee quad faces
            // Beyond that whole-mesh pre-refinement, spend a further couple
            // of ADAPTIVE levels narrowing only the faces that are still
            // irregular after it - cheap, since (unlike Subdivide() above)
            // this never touches an already-regular face, only recursing
            // into each irregular face's own shrinking approximated corner
            // (see ToNurbsPatchesAdaptive()'s own doc comment for the exact
            // growth shape). This is the "newer kernel adaptive converter"
            // PARITY_MAP.md's app_subd_mesh category previously flagged as
            // not wired into this command.
            const int kAdaptiveLevels = 2;
            try {
              refined.Subdivide(levels);
              std::vector<kernel::SubDNurbsPatch> patches = refined.ToNurbsPatchesAdaptive(kAdaptiveLevels);
              if (!patches.empty()) {
                ON_Brep combined;
                for (kernel::SubDNurbsPatch& p : patches) {
                  ON_Brep tmp;
                  ON_NurbsSurface* srf = new ON_NurbsSurface(p.surface.raw());
                  tmp.Create(srf);
                  combined.Append(tmp);
                  if (p.exact) ++exact_patches; else ++approx_patches;
                }
                JoinNakedEdges(combined, ctx.Settings().absolute_tolerance * 10);
                kernel::Brep k; k.raw() = combined;
                ctx.Doc().Add(SceneObject::MakeBrep(k));
                ++made;
              }
            } catch (...) {
              // Fall back to the old dense-facetted-mesh approximation
              // only if the real conversion itself throws (e.g. a
              // pathological/non-manifold SubD) - not the normal path.
              try {
                kernel::SubD dense = *o->subd;
                dense.Subdivide(levels > 0 ? levels : 1);
                if (ON_Brep* b = ON_BrepFromMesh(dense.ToApproximateMesh().raw().Topology())) {
                  ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b)));
                  ++made;
                }
              } catch (...) {}
            }
            }  // !watertight_done
          } else if (o->kind == ObjectKind::Mesh) {
            ON_Brep* b = ON_BrepFromMesh(o->mesh->raw().Topology());
            if (b) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); ++made; }
          }
        }
        std::string msg = "Converted " + std::to_string(made) + " object(s)";
        if (exact_patches || approx_patches) {
          msg += " (" + std::to_string(exact_patches) + " exact, " + std::to_string(approx_patches) +
                 " approximated near extraordinary vertices/creases/boundaries)";
        }
        ctx.Print(msg);
      }), CommandStatus::Implemented,
      "SubD input: converts to a real multi-patch NURBS Brep via Catmull-Clark limit-surface evaluation "
      "(kernel::SubD::ToNurbsPatchesAdaptive) - every face whose 4 corners are ordinary interior "
      "(valence-4) vertices becomes the mathematically exact bicubic Bezier patch (the well-known, "
      "non-proprietary fact that a regular Catmull-Clark control net *is* a uniform bicubic B-spline "
      "lattice; adjacent exact patches share bit-identical boundary curves and join into one seamless "
      "polysurface). Faces touching an extraordinary vertex, a crease, or a boundary have no such closed "
      "form: after a few rounds of whole-mesh Catmull-Clark pre-refinement (applied automatically, up to "
      "2 rounds bounded by face count), each one is further adaptively split (2 more levels) into "
      "progressively smaller exact regular patches around a single still-approximated flat corner patch, "
      "rather than left as one coarse flat approximation across the whole original face - those final "
      "corner patches stay unjoined at their approximate edges rather than being silently forced to look "
      "exact, and the adaptive split itself can introduce further naked T-junction edges where a split "
      "patch neighbors an unsplit regular one. The status line reports how many patches of each kind were "
      "produced. Mesh input converts exactly (unchanged).");
  Reg(e, "MeshToNURB", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("MeshToNURB");
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) if (ON_Brep* b = ON_BrepFromMesh(o->mesh->raw().Topology())) ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); }
      }));
  Reg(e, "SubDivide", OnSelection("Select SubDs to subdivide", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SubDivide");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::SubD) { o->subd->Subdivide(1); o->InvalidateDisplay(); } }
      }));
  Reg(e, "MeshPlane", Make<PointsCommand>(std::vector<std::string>{"First corner", "Other corner"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ON_Plane pl = ActivePlane(ctx); double u0, v0, u1, v1;
                                            pl.ClosestPointTo(p[0], &u0, &v0); pl.ClosestPointTo(p[1], &u1, &v1);
                                            std::vector<Point3d> grid = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u0, v1), pl.PointAt(u1, v1)};
                                            AddObject(ctx, SceneObject::MakeMesh(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1).TessellateGrid(8, 8)), "MeshPlane");
                                          }));
  Reg(e, "SubDPlane", Make<PointsCommand>(std::vector<std::string>{"First corner", "Other corner"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ON_Plane pl = ActivePlane(ctx); double u0, v0, u1, v1;
                                            pl.ClosestPointTo(p[0], &u0, &v0); pl.ClosestPointTo(p[1], &u1, &v1);
                                            std::vector<Point3d> grid = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u0, v1), pl.PointAt(u1, v1)};
                                            AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1).TessellateGrid(4, 4))), "SubDPlane");
                                          }));
  Reg(e, "Cap", OnSelection("Select open surfaces to cap", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Cap");
        int capped = 0, openings = 0, recorded = 0;
        const bool keep_source = ctx.App().State().history_recording;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Brep || !o->brep) continue;
          int caps_here = 0;
          std::string why;
          std::optional<SceneObject> built = CapObject(ctx, *o, caps_here, why);
          if (!built) continue;
          ++capped;
          openings += caps_here;
          SceneObject* t = ctx.Doc().Find(id);
          if (keep_source) {
            // History On: the open polysurface is hidden (kept as the recorded source), the capped copy is a new object.
            built->layer_index = t->layer_index;
            t->visible = false;
            t->selected = false;
            t->InvalidateDisplay();
            const ObjectId new_id = ctx.Doc().Add(std::move(*built));
            RecordHistoryIfEnabled(ctx, new_id, "Cap", {id}, {});
            ++recorded;
          } else {
            t->brep->raw() = built->brep->raw();
            t->InvalidateDisplay();
          }
        }
        ctx.Print("Capped " + std::to_string(capped) + " object(s), " + std::to_string(openings) + " opening(s)" +
                  (recorded ? "; History is On: the open polysurface(s) are kept hidden as the source of " + std::to_string(recorded) + " recorded result(s)" : std::string()));
      }), CommandStatus::Implemented, "Caps every planar closed naked-edge loop on each selected polysurface (one call handles multiple separate holes). With History On the open polysurface is kept hidden as the recorded source and the capped copy follows edits of it.");
}

}  // namespace dino8::app
