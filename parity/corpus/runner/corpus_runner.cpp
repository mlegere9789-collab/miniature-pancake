// Golden and Evil Corpus v0: manifest generator and runner (NOT part of the app or the kernel test build).
//
//   corpus_runner --emit [corpus.json]            write the manifest (recipes + closed-form oracles + tolerances)
//   corpus_runner --run  [corpus.json] [options]  build every case with the kernel API, run it, print one JSON line per case
//   corpus_runner --list [corpus.json]            list case ids
//   corpus_runner --selfcheck [corpus.json] [--filter s]   verify the manifest's closed forms against implicit-geometry sampling (no kernel)
//
// run options: --only id[,id...]   --filter substring   --timeout-scale k   --out results.jsonl
//
// Each case runs in a forked child with a wall-clock timeout (SIGKILL on expiry) and an address-space
// limit, so a hang, a crash or an allocation blow-up in the kernel is recorded, not fatal.
// Result values: ok | fail (ran, an invariant failed) | throw (exception, refusal or crash) | timeout.
//
// Build (kernel base objects in $OBJ, see parity/corpus/README block in BASELINE.md):
//   CPLUS_INCLUDE_PATH=<repo>/dino8-app/src bash kb_link.sh corpus_runner <repo>/dino8-kernel $OBJ \
//       corpus_runner.cpp <repo>/dino8-app/src/geom/BrepMesher.cpp
// BrepMesher.cpp is the app's edge-consistent closed mesher; it is compiled in only so the closed-manifold
// check is the one the product uses after a boolean.
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "geom/BrepMesher.h"

#include "corpus_build.h"
#include "corpus_selfcheck.h"
#include "json.h"

using namespace dino8::kernel;
using corpus::J;
namespace cf = corpus::cf;

namespace {

// ------------------------------------------------------------------ recipes -> operands
ON_3dPoint P3(const J* a) { return a ? ON_3dPoint(a->at(0), a->at(1), a->at(2)) : ON_3dPoint::Origin; }
ON_3dVector V3v(const J* a) { return a ? ON_3dVector(a->at(0), a->at(1), a->at(2)) : ON_3dVector::ZeroVector; }

// Plane with `origin` and z axis `axis`; the x axis (hence the seam of a cylinder, cone, sphere or torus
// built on it) is the world x axis projected perpendicular to the axis (world y if the axis is along x).
ON_Plane PlaneFor(const ON_3dPoint& origin, ON_3dVector axis) {
  axis.Unitize();
  const ON_3dVector seed = std::fabs(axis.x) < 0.9 ? ON_3dVector(1, 0, 0) : ON_3dVector(0, 1, 0);
  ON_3dVector x = seed - (seed * axis) * axis;
  x.Unitize();
  const ON_3dVector y = ON_CrossProduct(axis, x);
  return ON_Plane(origin, x, y);
}

Brep Wrap(ON_Brep* b, const char* what) {
  if (!b) throw std::runtime_error(std::string("operand construction failed: ") + what);
  Brep k;
  k.raw() = *b;
  delete b;
  if (k.raw().IsSolid()) {  // outward normals, as the app does (the open-source SolidOrientation() cannot tell: it returns 2)
    double v = 0;
    try { v = k.VolumeOfSealedSolid(); } catch (const std::exception&) {}
    if (v < 0) k.raw().Flip();
  }
  return k;
}

Brep MakeOperand(const J& s) {
  const std::string kind = s.str("kind");
  if (kind == "box") {
    const ON_3dPoint lo = P3(s.get("min")), hi = P3(s.get("max"));
    ON_3dPoint c[8] = {ON_3dPoint(lo.x, lo.y, lo.z), ON_3dPoint(hi.x, lo.y, lo.z), ON_3dPoint(hi.x, hi.y, lo.z), ON_3dPoint(lo.x, hi.y, lo.z),
                       ON_3dPoint(lo.x, lo.y, hi.z), ON_3dPoint(hi.x, lo.y, hi.z), ON_3dPoint(hi.x, hi.y, hi.z), ON_3dPoint(lo.x, hi.y, hi.z)};
    if (const J* rot = s.get("rot")) {
      ON_Xform xf;
      xf.Rotation(rot->num("deg", 0) * ON_PI / 180.0, V3v(rot->get("axis")), P3(rot->get("about")));
      for (auto& p : c) p = xf * p;
    }
    return Wrap(ON_BrepBox(c), "box");
  }
  if (kind == "cylinder") {
    const ON_Plane pl = PlaneFor(P3(s.get("base")), V3v(s.get("axis")));
    ON_Cylinder cyl(ON_Circle(pl, s.num("radius", 1)), s.num("height", 1));
    return Wrap(ON_BrepCylinder(cyl, true, true), "cylinder");
  }
  if (kind == "cone") {
    const ON_Plane pl = PlaneFor(P3(s.get("apex")), V3v(s.get("axis")));
    ON_Cone cone(pl, s.num("height", 1), s.num("radius", 1));
    return Wrap(ON_BrepCone(cone, true), "cone");
  }
  if (kind == "sphere") {
    ON_Sphere sph(P3(s.get("center")), s.num("radius", 1));
    sph.plane = PlaneFor(P3(s.get("center")), V3v(s.get("axis")));
    return Wrap(ON_BrepSphere(sph), "sphere");
  }
  if (kind == "torus") {
    ON_Torus t(PlaneFor(P3(s.get("center")), V3v(s.get("axis"))), s.num("major", 3), s.num("minor", 1));
    return Wrap(ON_BrepTorus(t), "torus");
  }
  if (kind == "prism") {
    const double z0 = s.num("z0", 0), z1 = s.num("z1", 1);
    ON_PolylineCurve poly;
    for (const J& p : s.get("polygon")->a) poly.m_pline.Append(ON_3dPoint(p.at(0), p.at(1), z0));
    const J& first = s.get("polygon")->a[0];
    poly.m_pline.Append(ON_3dPoint(first.at(0), first.at(1), z0));
    ON_Plane pl(ON_3dPoint(0, 0, z0), ON_3dVector(0, 0, 1));
    ON_Brep* b = ON_BrepTrimmedPlane(pl, poly);
    if (!b) throw std::runtime_error("operand construction failed: prism base");
    ON_LineCurve path(ON_Line(ON_3dPoint(0, 0, z0), ON_3dPoint(0, 0, z1)));
    if (ON_BrepExtrudeFace(*b, 0, path, true) < 0) { delete b; throw std::runtime_error("operand construction failed: prism extrude"); }
    return Wrap(b, "prism");
  }
  throw std::runtime_error("unknown operand kind: " + kind);
}

// ------------------------------------------------------------------ the operation (what the app does)
struct StepOutcome {
  Brep brep;
  std::string engine;
};

// Same order and acceptance as the app's Boolean command (cmd_boolean.cpp): the exact planar engine first
// (its result is accepted as returned), then the general engine with the kernel sealing step, then the
// app's plain sewing as the fallback when sealing refuses.
StepOutcome RunStep(const std::vector<Brep>& g1, const std::vector<Brep>& g2, BooleanOp op) {
  StepOutcome out;
  std::string planar_why;
  try {
    out.brep = g2.empty() ? BooleanCombinePlanarNAry(g1, {}, BooleanOp::Union) : BooleanCombinePlanarNAry(g1, g2, op);
    out.engine = "planar";
    return out;
  } catch (const std::exception& e) {
    planar_why = e.what();
  }
  Brep raw;
  try {
    raw = g2.empty() ? BooleanCombineGeneralNAry(g1, {}, BooleanOp::Union) : BooleanCombineGeneralNAry(g1, g2, op);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("general engine: ") + e.what() + " | planar: " + planar_why);
  }
  if (raw.raw().m_F.Count() == 0) { out.brep = raw; out.engine = "general(empty)"; return out; }
  double va = std::nan(""), vb = std::nan("");
  if (g1.size() == 1 && g2.size() == 1) {
    try { va = std::fabs(g1[0].VolumeOfSealedSolid()); vb = std::fabs(g2[0].VolumeOfSealedSolid()); } catch (const std::exception&) { va = vb = std::nan(""); }
  }
  std::string seal_why;
  try {
    SealedGeneralBoolean sb = SealGeneralBooleanResult(raw, g2.empty() ? BooleanOp::Union : op, va, vb);
    if (sb.brep.raw().m_F.Count() > 0) { out.brep = std::move(sb.brep); out.engine = "general+seal"; return out; }
  } catch (const std::exception& e) {
    seal_why = e.what();
  }
  // plain sewing fallback (HealGeneralBooleanResult of the app)
  ON_BoundingBox bb;
  raw.raw().GetBoundingBox(bb);
  const double diag = bb.Diagonal().Length();
  if (raw.raw().IsSolid()) { out.brep = raw; out.engine = "general"; return out; }
  for (double fraction : {2e-4, 1e-3, 3e-3, 1e-2}) {
    Brep c = raw;
    try {
      for (int pass = 0; pass < 3 && !c.raw().IsSolid(); ++pass) { c.SewTJunctions(fraction * diag); c.JoinNakedEdges(fraction * diag); }
    } catch (const std::exception&) { continue; }
    if (c.raw().IsSolid()) { out.brep = std::move(c); out.engine = "general+heal"; return out; }
  }
  throw std::runtime_error("result is not a closed solid: seal refused (" + seal_why + "), sewing did not close it");
}

// ------------------------------------------------------------------ measurement
struct Meas {
  bool empty = false, valid = false, solid = false, tess_closed = false, have_mesh = false, have_cen = false;
  int orient = 0, faces = 0, chi = INT_MIN, shells = -1, mesh_chi = INT_MIN, mesh_shells = -1;
  double vol = NAN, area = NAN, mesh_vol = NAN, mesh_area = NAN;
  std::string vol_src = "none", area_src = "none", vol_err, tess_err;
  ON_3dPoint cen;
};

// Euler characteristic V - E + F - (inner loops) and shell count of a B-rep (faces joined through shared edges).
void BrepTopology(const ON_Brep& b, int& chi, int& shells) {
  int V = 0, E = 0, F = 0, R = 0;
  std::vector<int> parent(b.m_F.Count());
  for (int i = 0; i < parent.size(); ++i) parent[i] = i;
  auto find = [&](int x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
  for (int i = 0; i < b.m_V.Count(); ++i) if (b.m_V[i].m_vertex_index == i && b.m_V[i].m_ei.Count() > 0) ++V;
  for (int i = 0; i < b.m_E.Count(); ++i) {
    const ON_BrepEdge& e = b.m_E[i];
    if (e.m_edge_index != i) continue;
    ++E;
    int f0 = -1;
    for (int k = 0; k < e.m_ti.Count(); ++k) {
      const ON_BrepFace* f = b.m_T[e.m_ti[k]].Face();
      if (!f) continue;
      if (f0 < 0) f0 = f->m_face_index; else parent[find(f->m_face_index)] = find(f0);
    }
  }
  for (int i = 0; i < b.m_F.Count(); ++i) if (b.m_F[i].m_face_index == i) ++F;
  for (int i = 0; i < b.m_L.Count(); ++i) {
    const ON_BrepLoop& l = b.m_L[i];
    if (l.m_loop_index == i && l.m_type == ON_BrepLoop::inner && l.Face() && l.Face()->m_face_index >= 0) ++R;
  }
  std::set<int> roots;
  for (int i = 0; i < b.m_F.Count(); ++i) if (b.m_F[i].m_face_index == i) roots.insert(find(i));
  chi = V - E + F - R;
  shells = static_cast<int>(roots.size());
}

void MeshTopology(const ON_Mesh& m, int& chi, int& shells) {
  std::set<std::pair<int, int>> edges;
  std::set<int> used;
  std::vector<int> parent(m.m_V.Count());
  for (int i = 0; i < parent.size(); ++i) parent[i] = i;
  auto find = [&](int x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
  int F = 0;
  for (int i = 0; i < m.m_F.Count(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    ++F;
    for (int k = 0; k < n; ++k) {
      const int a = f.vi[k], c = f.vi[(k + 1) % n];
      used.insert(a);
      edges.insert({std::min(a, c), std::max(a, c)});
      parent[find(a)] = find(c);
    }
  }
  std::set<int> roots;
  for (int v : used) roots.insert(find(v));
  chi = static_cast<int>(used.size()) - static_cast<int>(edges.size()) + F;
  shells = static_cast<int>(roots.size());
}

Meas Measure(const Brep& R) {
  Meas m;
  const ON_Brep& b = R.raw();
  m.faces = b.m_F.Count();
  m.empty = m.faces == 0;
  if (m.empty) return m;
  m.valid = b.IsValid();
  m.solid = b.IsSolid();
  BrepTopology(b, m.chi, m.shells);
  try { m.vol = R.VolumeOfSealedSolid(); m.vol_src = "sealed"; }
  catch (const std::exception& e) {
    m.vol_err = e.what();
    try { m.vol = R.Volume(); m.vol_src = "brep"; } catch (const std::exception& e2) { m.vol_err += std::string(" | ") + e2.what(); }
  }
  try { m.area = R.Area(); m.area_src = "brep"; } catch (const std::exception&) {}
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  dino8::app::BrepMeshOptions mo;
  mo.chord_tolerance = bb.Diagonal().Length() * 5e-4;  // the app's 0.05%-of-diagonal rule, without its absolute clamp
  try {
    Mesh mesh = dino8::app::MeshBrepClosed(b, mo);
    m.have_mesh = true;
    m.tess_closed = mesh.IsClosedManifold();
    MeshTopology(mesh.raw(), m.mesh_chi, m.mesh_shells);
    if (m.tess_closed) {
      m.mesh_vol = mesh.Volume();
      m.mesh_area = mesh.Area();
      try { const Point3d c = mesh.GetCentroid(); m.cen = ON_3dPoint(c.x, c.y, c.z); m.have_cen = true; } catch (const std::exception&) {}
    }
  } catch (const std::exception& e) {
    m.tess_err = e.what();
  }
  if (std::isnan(m.vol) && m.tess_closed) { m.vol = m.mesh_vol; m.vol_src = "mesh"; }
  m.orient = std::isnan(m.vol) ? 0 : m.vol > 0 ? 1 : m.vol < 0 ? -1 : 0;  // sign of the signed volume: +1 outward, -1 inside-out
  if (std::isnan(m.area) && m.tess_closed) { m.area = m.mesh_area; m.area_src = "mesh"; }
  return m;
}

// ------------------------------------------------------------------ checking against the manifest
std::string Num(double v) { return J::Number(v); }
bool InList(const J* j, int v) {
  if (!j) return true;
  if (j->t == J::Num) return static_cast<int>(j->n) == v;
  for (const J& e : j->a) if (static_cast<int>(e.n) == v) return true;
  return false;
}

struct Verdict {
  std::vector<std::string> failed;
  std::string reason;
  void Fail(const std::string& name, const std::string& msg) {
    failed.push_back(name);
    if (!reason.empty()) reason += "; ";
    reason += name + ": " + msg;
  }
};

Verdict Check(const J& ex, const Meas& m) {
  Verdict v;
  const J* tol = ex.get("tol");
  const double vol_rel = tol ? tol->num("volume_rel", 1e-3) : 1e-3, vol_abs = tol ? tol->num("volume_abs", 0) : 0;
  const double area_rel = tol ? tol->num("area_rel", 2e-3) : 2e-3, cen_abs = tol ? tol->num("centroid_abs", 0) : 0;
  const J* em = ex.get("empty");
  const bool want_empty = em && em->t == J::Bool && em->b, either = em && em->t == J::Str;
  if (m.empty) {
    if (!want_empty && !either) v.Fail("empty", "result is empty, expected a solid of volume " + Num(ex.num("volume", NAN)));
    return v;
  }
  if (want_empty) {
    if (!(std::fabs(m.vol) <= vol_abs)) v.Fail("empty", "expected an empty result, got a body with " + std::to_string(m.faces) + " faces and volume " + Num(m.vol));
    return v;
  }
  if (ex.boolean("valid", false) && !m.valid) v.Fail("valid", "ON_Brep::IsValid() is false");
  if (ex.boolean("solid", false) && !(m.solid && m.orient > 0)) v.Fail("solid", m.solid ? "closed but inside-out or of unknown orientation (signed volume " + Num(m.vol) + ")" : "IsSolid() is false (not a closed solid)");
  if (ex.boolean("tess_closed", false) && !m.tess_closed) v.Fail("tess_closed", m.tess_err.empty() ? "the closed mesher did not produce a closed manifold mesh" : "mesher threw: " + m.tess_err);
  if (ex.has("volume")) {
    const double V = ex.num("volume", NAN), tv = std::fmax(vol_rel * std::fabs(V), vol_abs);
    if (std::isnan(m.vol)) v.Fail("volume", "not measurable (" + m.vol_err + ")");
    else if (!(std::fabs(m.vol - V) <= tv)) v.Fail("volume", Num(m.vol) + " vs " + Num(V) + " (" + m.vol_src + ", error " + Num(std::fabs(m.vol - V) / std::fmax(std::fabs(V), 1e-300)) + " relative, tolerance " + Num(vol_rel) + ")");
  }
  if (ex.has("area")) {
    const double A = ex.num("area", NAN);
    if (std::isnan(m.area)) v.Fail("area", "not measurable");
    else if (!(std::fabs(m.area - A) <= area_rel * A)) v.Fail("area", Num(m.area) + " vs " + Num(A) + " (" + m.area_src + ")");
  }
  if (const J* c = ex.get("centroid")) {
    if (!m.have_cen) v.Fail("centroid", "not measurable (no closed mesh)");
    else {
      const double d = ON_3dPoint(c->at(0), c->at(1), c->at(2)).DistanceTo(m.cen);
      if (!(d <= cen_abs)) v.Fail("centroid", "off by " + Num(d) + " (tolerance " + Num(cen_abs) + ")");
    }
  }
  if (const J* e = ex.get("euler")) if (!InList(e, m.chi)) v.Fail("euler", "Euler characteristic " + std::to_string(m.chi) + ", expected " + e->dump());
  if (const J* e = ex.get("shells")) if (!InList(e, m.shells)) v.Fail("shells", std::to_string(m.shells) + " shells, expected " + e->dump());
  return v;
}

J MeasJson(const Meas& m) {
  J j = J::Object();
  j.set("empty", m.empty).set("faces", m.faces);
  if (m.empty) return j;
  j.set("valid", m.valid).set("solid", m.solid).set("orient", m.orient).set("tess_closed", m.tess_closed);
  j.set("volume", m.vol).set("volume_src", m.vol_src).set("area", m.area).set("area_src", m.area_src);
  if (m.have_cen) j.set("centroid", J::A({m.cen.x, m.cen.y, m.cen.z}));
  j.set("euler", m.chi).set("shells", m.shells);
  if (m.have_mesh) j.set("mesh_volume", m.mesh_vol).set("mesh_euler", m.mesh_chi).set("mesh_shells", m.mesh_shells);
  if (!m.vol_err.empty()) j.set("volume_note", m.vol_err.substr(0, 160));
  return j;
}

std::string RunCase(const J& c) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  auto ms = [&] { return std::chrono::duration<double, std::milli>(clock::now() - t0).count(); };
  J out = J::Object();
  out.set("id", c.str("id")).set("set", c.str("set")).set("category", c.str("category"));
  std::string phase = "construct";
  try {
    std::map<std::string, Brep> store;
    for (const J& o : c.get("operands")->a) store[o.str("id")] = MakeOperand(o);
    const J& op = *c.get("operation");
    Brep result;
    std::string engine = "measure";
    phase = "operate";
    if (op.str("type") == "measure") {
      result = store.at(op.str("operand"));
    } else {
      for (const J& st : op.get("steps")->a) {
        std::vector<Brep> g1, g2;
        for (const J& id : st.get("a")->a) g1.push_back(store.at(id.s));
        for (const J& id : st.get("b")->a) g2.push_back(store.at(id.s));
        const std::string o = st.str("op");
        const BooleanOp bop = o == "union" ? BooleanOp::Union : o == "intersection" ? BooleanOp::Intersection : BooleanOp::Difference;
        StepOutcome so = RunStep(g1, g2, bop);
        store[st.str("out")] = so.brep;
        result = so.brep;
        engine = engine == "measure" ? so.engine : engine + ">" + so.engine;
      }
    }
    const double ms_op = ms();
    phase = "measure";
    const Meas m = Measure(result);
    const J& ex = *c.get("expect");
    const Verdict v = Check(ex, m);
    out.set("result", v.failed.empty() ? "ok" : "fail").set("ms", ms()).set("ms_op", ms_op).set("engine", engine);
    if (!v.failed.empty()) {
      J f = J::Array();
      for (const auto& n : v.failed) f.push(J(n));
      out.set("failed", f).set("reason", v.reason.substr(0, 400));
    }
    out.set("measured", MeasJson(m));
    J e = J::Object();
    for (const char* k : {"empty", "volume", "area", "centroid", "euler", "shells"}) if (const J* p = ex.get(k)) e.set(k, *p);
    out.set("expected", e);
  } catch (const std::exception& e) {
    out.set("result", "throw").set("phase", phase).set("ms", ms()).set("reason", std::string(e.what()).substr(0, 400));
  }
  return out.dump();
}

// ------------------------------------------------------------------ isolation: fork + wall-clock timeout
std::string RunIsolated(const J& c, double timeout_s) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  int fd[2];
  if (pipe(fd) != 0) return "{\"id\":\"" + c.str("id") + "\",\"result\":\"throw\",\"reason\":\"pipe failed\"}";
  fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]);
    const int dn = open("/dev/null", O_WRONLY);
    dup2(dn, 1);
    dup2(dn, 2);
    rlimit as{6ull << 30, 6ull << 30};
    setrlimit(RLIMIT_AS, &as);
    rlimit cpu{static_cast<rlim_t>(timeout_s) + 10, static_cast<rlim_t>(timeout_s) + 10};
    setrlimit(RLIMIT_CPU, &cpu);
    const std::string line = RunCase(c);
    size_t off = 0;
    while (off < line.size()) { const ssize_t w = write(fd[1], line.data() + off, line.size() - off); if (w <= 0) break; off += static_cast<size_t>(w); }
    _exit(0);
  }
  close(fd[1]);
  std::string buf;
  bool timed_out = false;
  for (;;) {
    const double elapsed = std::chrono::duration<double>(clock::now() - t0).count();
    const double left = timeout_s - elapsed;
    if (left <= 0) { timed_out = true; break; }
    pollfd p{fd[0], POLLIN, 0};
    const int r = poll(&p, 1, static_cast<int>(std::min(left, 1.0) * 1000) + 1);
    if (r > 0) {
      char tmp[4096];
      const ssize_t n = read(fd[0], tmp, sizeof tmp);
      if (n > 0) buf.append(tmp, static_cast<size_t>(n));
      else if (n == 0) break;
    }
  }
  if (timed_out) kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  close(fd[0]);
  const double wall = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
  J o = J::Object();
  o.set("id", c.str("id")).set("set", c.str("set")).set("category", c.str("category"));
  if (timed_out) {
    o.set("result", "timeout").set("ms", wall).set("reason", "no result within " + Num(timeout_s) + " s");
    return o.dump();
  }
  if (!buf.empty()) return buf;
  if (WIFSIGNALED(status)) o.set("result", "throw").set("ms", wall).set("reason", std::string("crash: signal ") + std::to_string(WTERMSIG(status)) + " (" + strsignal(WTERMSIG(status)) + ")");
  else o.set("result", "throw").set("ms", wall).set("reason", "no output, exit code " + std::to_string(WEXITSTATUS(status)));
  return o.dump();
}

// ------------------------------------------------------------------ manifest I/O and CLI
std::string ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

J Header(size_t total, size_t golden) {
  J h = J::Object();
  h.set("schema", "dino8.corpus.v0");
  h.set("title", "Golden and Evil Corpus v0 (kernel robustness, boolean operations)");
  h.set("kernel_base", "dino8-kernel at cand2");
  h.set("generated_by", "parity/corpus/runner/corpus_runner.cpp --emit (closed forms in closed_forms.h; no kernel code is used to produce an expectation)");
  h.set("units", "model units; every length in a recipe is final (already scaled/translated), so a recipe is exactly what the runner builds");
  h.set("counts", J::O({{"total", total}, {"golden", golden}, {"evil", total - golden}}));
  h.set("success", "a case is ok when the operation returns without exception or timeout and EVERY invariant in `expect` holds: empty/non-empty, ON_Brep::IsValid, IsSolid with outward orientation, a closed-manifold tessellation (the app's MeshBrepClosed), exact volume (VolumeOfSealedSolid, else Volume, else the closed mesh), exact area, centroid from the closed mesh, Euler characteristic V-E+F-inner loops and shell count of the B-rep");
  h.set("tolerance_policy", J::O({{"planar_only", "volume/area relative 1e-8, centroid 1e-6 of the result size (exact arithmetic)"},
                                   {"curved", "gross-correctness gate: volume and area relative 2e-2, centroid 5e-3 of the result size; the kernel's own exact integrals polygonize circular trims (a plain cylinder measures 3.8e-3 low), so tighter gates would score the integrator; the measured error of every case is recorded and the precision ladder 1e-2/1e-3/1e-4/1e-6 is tabulated from the same run"},
                                   {"volume_abs", "1e-9 of the bounding volume: below it a volume is numerically zero"},
                                   {"far_origin", "volume/area relative widened to 160 eps D/L (divergence-theorem noise floor about the world origin)"},
                                   {"ambiguity", "|delta| <= 1e-6 (the kernel model tolerance): merged and separated topologies are both listed as allowed; empty may be 'either'"}}));
  h.set("operand_kinds", "box (optional rot), cylinder, cone, sphere, torus, prism - built as the app builds them (ON_BrepBox, ON_BrepCylinder, ON_BrepCone, ON_BrepSphere, ON_BrepTorus, trimmed plane + extrude); the seam of a round primitive is on the world x axis projected perpendicular to its axis");
  h.set("operations", "boolean: steps of union/intersection/difference over groups of operand ids (N-ary folds as BooleanCombine*NAry), engine 'auto' = the app's order (planar engine, then general engine + kernel sealing, then plain sewing); measure: no operation, the operand itself is checked");
  return h;
}

void Emit(const std::string& path) {
  const std::vector<J> cases = corpus::BuildCorpus();
  size_t golden = 0;
  for (const J& c : cases) if (c.str("set") == "golden") ++golden;
  std::string text = Header(cases.size(), golden).dump();
  text.pop_back();  // drop the closing brace; the cases array follows, one case per line
  text += ",\"cases\":[\n";
  for (size_t i = 0; i < cases.size(); ++i) text += cases[i].dump() + (i + 1 < cases.size() ? ",\n" : "\n");
  text += "]}\n";
  if (path.empty()) std::fputs(text.c_str(), stdout);
  else { std::ofstream(path, std::ios::binary) << text; std::fprintf(stderr, "wrote %s: %zu cases (%zu golden, %zu evil), %zu bytes\n", path.c_str(), cases.size(), golden, cases.size() - golden, text.size()); }
}

}  // namespace

int main(int argc, char** argv) {
  ON::Begin();
  std::string mode, manifest = "corpus.json", filter, only, outpath;
  double tscale = 1.0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--emit" || a == "--run" || a == "--list" || a == "--selfcheck") { mode = a; if (i + 1 < argc && argv[i + 1][0] != '-') manifest = argv[++i]; }
    else if (a == "--filter" && i + 1 < argc) filter = argv[++i];
    else if (a == "--only" && i + 1 < argc) only = argv[++i];
    else if (a == "--out" && i + 1 < argc) outpath = argv[++i];
    else if (a == "--timeout-scale" && i + 1 < argc) tscale = std::atof(argv[++i]);
  }
  try {
    if (mode == "--emit") { Emit(argc > 2 && argv[2][0] != '-' ? argv[2] : ""); return 0; }
    if (mode == "--selfcheck") {
      const J m = corpus::ParseJson(ReadFile(manifest));
      int bad = 0, skipped = 0, checked = 0, areas = 0;
      for (const J& c : m.get("cases")->a) {
        const std::string id = c.str("id");
        if (!filter.empty() && id.find(filter) == std::string::npos) continue;
        const int plates = static_cast<int>(c.get("operands")->a.size());
        const corpus::selfcheck::Report r = corpus::selfcheck::check_case(c, plates > 20 ? 150 : plates > 3 ? 160 : 220);
        if (r.skipped) { ++skipped; std::printf("skip     %-34s %s\n", id.c_str(), r.skip_reason.c_str()); continue; }
        ++checked;
        if (r.area_checked) ++areas;
        if (r.mismatch) { ++bad; std::printf("MISMATCH %-34s %s\n", id.c_str(), r.detail.c_str()); }
        else std::printf("match    %-34s%s\n", id.c_str(), r.area_checked ? " (volume, centroid, area)" : " (volume, centroid)");
        std::fflush(stdout);
      }
      std::printf("selfcheck: %d checked (%d with an area check), %d mismatches, %d skipped (volume too small for the sampling grid)\n", checked, areas, bad, skipped);
      return bad ? 1 : 0;
    }
    if (mode != "--run" && mode != "--list") { std::fprintf(stderr, "usage: corpus_runner --emit [file] | --run [manifest] [--only ids] [--filter s] [--timeout-scale k] [--out file] | --list [manifest]\n"); return 2; }
    const J m = corpus::ParseJson(ReadFile(manifest));
    std::set<std::string> want;
    { std::stringstream ss(only); std::string t; while (std::getline(ss, t, ',')) if (!t.empty()) want.insert(t); }
    std::ofstream out;
    if (!outpath.empty()) out.open(outpath, std::ios::binary);
    for (const J& c : m.get("cases")->a) {
      const std::string id = c.str("id");
      if (!want.empty() && !want.count(id)) continue;
      if (!filter.empty() && id.find(filter) == std::string::npos) continue;
      if (mode == "--list") { std::printf("%s\n", id.c_str()); continue; }
      const std::string line = RunIsolated(c, c.num("timeout_s", 30) * tscale);
      std::printf("%s\n", line.c_str());
      std::fflush(stdout);
      if (out) { out << line << "\n"; out.flush(); }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "corpus_runner: %s\n", e.what());
    return 1;
  }
  return 0;
}
