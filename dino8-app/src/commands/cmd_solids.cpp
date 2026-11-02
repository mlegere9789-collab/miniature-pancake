// Surface, solid, mesh and SubD creation commands.
#include "commands/cmd_common.h"
#include "commands/history_rebuild.h"
#include "commands/section_fit.h"

#include <array>
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
LoftTokens TakeLoftTokens(CommandContext& ctx) {
  LoftTokens out;
  while (auto t = ctx.Engine().TakePendingInput()) {
    std::string s = *t;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "style=straight") out.straight = true;
    else if (s == "style=normal") out.straight = false;
    else if (s == "cap=yes" || s == "cap=y") out.cap = true;
    else if (s == "cap=no" || s == "cap=n") out.cap = false;
  }
  return out;
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
      std::optional<kernel::SubD> exact;
      try {
        exact = kernel::SubD::FromBrep(*o->brep, 1);
      } catch (const std::exception&) {
        exact = std::nullopt;
      }
      if (exact) {
        n = SceneObject::MakeSubD(*exact);
        ++fidelity_checked;
        ++fidelity_verified;
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
  }
  ctx.Print(msg);
}

}  // namespace

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
  Reg(e, "Loft", OnSelection("Select curves to loft in order", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { const LoftTokens t = TakeLoftTokens(ctx); Loft(ctx, ids, false, t.straight, t.cap); }, 2), CommandStatus::Implemented, "Calls the kernel's exact interpolating Brep::Loft: every selected curve lies exactly on the result (measured and printed), Normal is cubic in the loft direction and Style=Straight is linear (sharp at every section). Curves are first made consistent - open curves reversed to run the same way, closed curves reversed to circulate the same way and re-seamed to the point nearest the previous curve's start; a mix of open and closed curves is refused with the reason. Closed planar curves are capped into a polysurface solid (Cap=Yes, the default; Cap=No leaves the periodic surface). Loose/Tight aren't distinguishable here and aren't offered. If the kernel refuses a loft, the older control-point fit is used and the reason printed.");
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
        int capped = 0, openings = 0;
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Brep) continue;
          ON_Brep& b = o->brep->raw();
          if (b.IsSolid()) continue;
          std::vector<int> naked;
          for (int ei = 0; ei < b.m_E.Count(); ++ei) if (b.m_E[ei].TrimCount() == 1) naked.push_back(ei);
          if (naked.empty()) continue;
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
          int caps_here = 0;
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
          if (caps_here > 0) { JoinNakedEdges(b, ctx.Settings().absolute_tolerance * 10); o->InvalidateDisplay(); ++capped; openings += caps_here; }
        }
        ctx.Print("Capped " + std::to_string(capped) + " object(s), " + std::to_string(openings) + " opening(s)");
      }), CommandStatus::Implemented, "Caps every planar closed naked-edge loop on each selected polysurface (one call handles multiple separate holes).");
}

}  // namespace dino8::app
