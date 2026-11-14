// Kernel thread-safety probe (Wave 0, "kthreads" audit; see dino8-core/KERNEL_THREADS.md).
//
// N threads each run independent dino8-kernel calls on inputs they build themselves (no Brep, Mesh or
// PointCloud object is shared between threads in the `indep` phase), repeated `--iters` times:
//   box_cyl_mixed     box 10^3 minus an analytic cylinder          BooleanCombineMixed (Difference)
//   box_cyl_general   box 4x4x2 minus a closed finite cylinder     BooleanCombineGeneral (Difference)
//   sphere_box_union  sphere (corner of box) union box             BooleanCombineGeneral (Union)
//   compound_cutter   slab minus a compound cutter of 2 cylinders  MakeHoles -> BooleanCombineGeneralNAry
//   compound_planar   box minus a 2-lump compound of boxes         BooleanCombinePlanar (Difference)
//   tessellate        sphere/torus/box/welded cylinder meshes      TessellateToClosedMesh + adaptive variants
//   mass_props        exact and mesh mass properties               Volume/Area/VolumeMassProperties
//   mesh_boolean      Manifold mesh boolean, split, hull           BooleanCombine and friends
// The `shared` phase instead gives every thread the SAME const object (what an incremental engine does when
// two derived-data queries read one input snapshot): Brep tessellation and mass properties, welded solids, the
// same operands fed to concurrent booleans, Mesh queries, and a PointCloud nearest-neighbour query.
//
// Every thread's results are compared with a serial reference computed before any thread starts, so
// cross-thread interference shows up as a value divergence even in a build without ThreadSanitizer. Built with
// -fsanitize=thread, any data race is reported by the runtime with both stacks.
//
// usage: probe [--threads N] [--iters N] [--seconds T] [--phase indep|shared|both] [--ops a,b|all]
//              [--stagger 0|1|2] [--cold] [--prewarm-bbox] [--stack-report] [--list]
//   phases: indep (8 ops above), features (fillet/chamfer, revolve/extrude/pipe, convex hull, SubD, 3dm/STEP/IGES I/O),
//           shared (one const object read by all threads), both = indep + shared (default), all = every phase
//   --stagger 0: every thread runs the same op at the same time (hits one engine's globals hardest)
//   --stagger 1: thread t starts at op t, so different engines overlap (cross-engine interference)
//   --stagger 2: alternate 0 and 1 per iteration (default)
//   --seconds T: stop starting new iterations after T seconds (the shared build machine is small)
//   --cold:      no serial warm-up/reference pass; iteration 0 is the first call of each op in the process, made by all
//                threads at once, and later iterations are compared with each thread's own iteration 0 (this is how
//                lazily initialised statics, e.g. openNURBS's ON__isnand, are exposed to the sanitizer)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/convex_hull.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/features.h"
#include "dino8/kernel/file_io.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/point_cloud.h"
#include "dino8/kernel/subd.h"
#include "dino8/kernel/surface.h"

using namespace dino8::kernel;

namespace {

// ---------------------------------------------------------------- signatures

struct Sig {
  std::vector<double> v;  // numbers that must reproduce exactly (counts, volumes, moments)
  std::string err;        // non-empty when the call threw
};

bool SameSig(const Sig& a, const Sig& b) {
  if (a.err != b.err || a.v.size() != b.v.size()) return false;
  for (size_t i = 0; i < a.v.size(); ++i) {
    if (a.v[i] == b.v[i]) continue;
    if (std::isnan(a.v[i]) && std::isnan(b.v[i])) continue;
    return false;
  }
  return true;
}

std::string SigText(const Sig& s) {
  std::ostringstream o;
  if (!s.err.empty()) o << "threw \"" << s.err.substr(0, 80) << "\"";
  else {
    o << "[";
    for (size_t i = 0; i < s.v.size(); ++i) o << (i ? ", " : "") << s.v[i];
    o << "]";
  }
  return o.str();
}

template <class F>
Sig Guard(F&& f) {
  Sig s;
  try {
    f(s.v);
  } catch (const std::exception& e) {
    s.v.clear();
    s.err = e.what();
  } catch (...) {
    s.v.clear();
    s.err = "unknown exception";
  }
  return s;
}

// ---------------------------------------------------------------- fixtures (the repo's own, see tests/)

// Closed finite cylinder along +z, as tests/test_basic.cpp's MakeCylinderZForBoxCylinderTest.
Brep MakeCylinderZ(double cx, double cy, double z0, double z1, double r) {
  Brep::CylindricalFace cf;
  cf.frame = ON_Plane(ON_3dPoint(cx, cy, z0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0));
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = z1 - z0;
  const int n = 128;
  auto make_cap = [&](double z, bool flip) {
    Brep::PlanarFace pf;
    pf.plane = ON_Plane(ON_3dPoint(cx, cy, z), ON_3dVector(0, 0, flip ? -1 : 1));
    for (int i = 0; i <= n; ++i) {
      const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
      pf.loop.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a), z);
    }
    pf.loop.pop_back();
    pf.notch_begin = 0;
    pf.notch_count = static_cast<int>(pf.loop.size());
    return pf;
  };
  return Brep::FromMixedFaces({make_cap(z0, true), make_cap(z1, false)}, {cf});
}

// A single full-circle CylindricalFace (no caps) for the analytic plane/cylinder engine, as BuildDrilledBoxInputs.
Brep MakeAnalyticHole(double radius, double z0, double length) {
  Brep::CylindricalFace hole;
  hole.frame.origin = Point3d(5, 5, z0);
  hole.frame.xaxis = Vector3d(1, 0, 0);
  hole.frame.yaxis = Vector3d(0, 1, 0);
  hole.frame.zaxis = Vector3d(0, 0, 1);
  hole.frame.UpdateEquation();
  hole.radius = radius;
  hole.angle = 2.0 * ON_PI;
  hole.length = length;
  return Brep::FromMixedFaces({}, {hole});
}

// ---------------------------------------------------------------- operations (per-thread independent inputs)

Sig OpBoxCylMixed(int tid) {
  return Guard([&](std::vector<double>& v) {
    const double r = 1.5 + 0.25 * tid;
    const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
    const Brep cyl = MakeAnalyticHole(r, -1.0, 12.0);
    const Brep d = BooleanCombineMixed(box, cyl, BooleanOp::Difference);
    v.push_back(d.FaceCount());
    v.push_back(d.TessellateToClosedMesh(16, 16).Volume());
    v.push_back(1000.0 - ON_PI * r * r * 10.0);  // closed form, for the sanity line of the reference pass
  });
}

Sig OpBoxCylGeneral(int tid) {
  return Guard([&](std::vector<double>& v) {
    const double r = 0.9 + 0.05 * tid;
    const Brep box = Brep::Box(-2, -2, -1, 2, 2, 1);
    const Brep cyl = MakeCylinderZ(0, 0, -2, 2, r);
    const Brep d = BooleanCombineGeneral(box, cyl, BooleanOp::Difference);
    v.push_back(d.FaceCount());
    v.push_back(d.raw().IsValid() ? 1 : 0);
    v.push_back(TessellateGeneralBooleanClosedMesh(d, 8, 32).Volume());  // (24, 96) costs 1.2 s; 8x32 keeps the loop short
    v.push_back(d.TessellateToClosedMesh(8, 32).Volume());
    v.push_back(32.0 - ON_PI * r * r * 2.0);
  });
}

Sig OpSphereBoxUnion(int tid) {
  return Guard([&](std::vector<double>& v) {
    const double r = 2.0 + 0.1 * tid;
    const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), r);
    const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
    const Brep u = BooleanCombineGeneral(sphere, box, BooleanOp::Union);
    v.push_back(u.FaceCount());
    v.push_back(u.raw().IsValid() ? 1 : 0);
    v.push_back(u.TessellateToClosedMesh(24, 48).Volume());
    v.push_back(1000.0 + (4.0 / 3.0) * ON_PI * r * r * r * (7.0 / 8.0));
  });
}

Sig OpCompoundCutter(int tid) {
  return Guard([&](std::vector<double>& v) {
    const double r = 0.5 + 0.02 * tid;
    const Brep slab = Brep::Box(0, 0, 0, 4, 8, 4);
    const Point3d c1(2, 2, 4), c2(2, 6, 4);
    const Brep drilled = MakeHoles(slab, {c1, c2}, Vector3d(0, 0, -1), r, 0.0, true);
    v.push_back(drilled.FaceCount());
    v.push_back(drilled.raw().IsValid() ? 1 : 0);
    v.push_back(drilled.TessellateToClosedMesh(24, 96).Volume());
    v.push_back(128.0 - 2.0 * ON_PI * r * r * 4.0);
  });
}

Sig OpCompoundPlanar(int tid) {
  return Guard([&](std::vector<double>& v) {
    const double s = 2.0 + 0.25 * tid;
    const Brep target = Brep::Box(0, 0, 0, 10, 10, 10);
    const Brep compound = Brep::Compound({Brep::Box(1, 1, 1, 1 + s, 1 + s, 1 + s), Brep::Box(6, 6, 6, 8, 8, 8)});
    const Brep d = BooleanCombinePlanar(target, compound, BooleanOp::Difference);
    v.push_back(d.FaceCount());
    v.push_back(static_cast<double>(d.LumpFaceRanges().size()));
    v.push_back(d.TessellateToClosedMesh(2, 2).Volume());
    v.push_back(1000.0 - s * s * s - 8.0);
  });
}

Sig OpTessellate(int tid) {
  return Guard([&](std::vector<double>& v) {
    const double r = 1.5 + 0.2 * tid;
    const Brep sphere = Brep::Sphere(Point3d(tid, 0, 0), r);
    const Mesh ms = sphere.TessellateToClosedMesh(24, 48);
    v.push_back(ms.VertexCount());
    v.push_back(ms.FaceCount());
    v.push_back(ms.Volume());
    v.push_back(ms.IsClosedManifold() ? 1 : 0);
    const Brep torus = Brep::Torus(Point3d(0, tid, 0), Vector3d(0, 0, 1), 3.0 + 0.1 * tid, 0.7);
    const Mesh mt = torus.TessellateToClosedMesh(32, 16);
    v.push_back(mt.VertexCount());
    v.push_back(mt.Volume());
    const Brep box = Brep::Box(0, 0, 0, 3 + tid, 4, 5);
    const Mesh mb = box.TessellateToClosedMeshConforming(6, 6);
    v.push_back(mb.FaceCount());
    v.push_back(mb.Volume());
    const Brep cyl = Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0 + 0.1 * tid, 3.0);
    const Mesh mc = cyl.TessellateToClosedMesh(24, 12);
    v.push_back(mc.FaceCount());
    v.push_back(mc.Volume());
    v.push_back(sphere.TessellateToClosedMeshAdaptive(0.01).FaceCount());
    v.push_back(torus.TessellateToClosedMeshAdaptiveByAngle(0.2).FaceCount());
  });
}

Sig OpMassProps(int tid) {
  return Guard([&](std::vector<double>& v) {
    const Brep box = Brep::Box(0, 0, 0, 3 + tid, 4, 5);
    const MassProperties mp = box.VolumeMassProperties();
    for (double x : {mp.volume, mp.centroid.x, mp.centroid.y, mp.centroid.z, mp.ixx, mp.iyy, mp.izz, mp.ixy,
                     mp.principal_moments[0], mp.principal_moments[2]})
      v.push_back(x);
    const Brep sphere = Brep::Sphere(Point3d(1, 2, 3), 1.5 + 0.2 * tid);
    v.push_back(sphere.Volume());
    v.push_back(sphere.Area());
    const Brep torus = Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), 3.0 + 0.1 * tid, 0.7);
    v.push_back(torus.Volume());
    const MassProperties mm = torus.TessellateToClosedMesh(48, 24).VolumeMassProperties();
    for (double x : {mm.volume, mm.ixx, mm.izz, mm.centroid.z}) v.push_back(x);
    v.push_back(box.Area());
  });
}

Sig OpMeshBoolean(int tid) {
  return Guard([&](std::vector<double>& v) {
    const Mesh a = Brep::Box(0, 0, 0, 4, 4, 4).TessellateToClosedMesh(2, 2);
    const Mesh b = Brep::Sphere(Point3d(2 + 0.1 * tid, 2, 4), 1.5).TessellateToClosedMesh(24, 48);
    for (BooleanOp op : {BooleanOp::Union, BooleanOp::Intersection, BooleanOp::Difference}) {
      const Mesh r = BooleanCombine(a, b, op);
      v.push_back(r.FaceCount());
      v.push_back(r.Volume());
    }
  });
}

// ---- `features` phase: other kernel subsystems, each on thread-private inputs

Sig OpFilletChamfer(int tid) {
  return Guard([&](std::vector<double>& v) {
    const Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
    const double r = 1.0 + 0.1 * tid;
    const Brep f = FilletConvexEdge(box, Point3d(0, 0, 0), Point3d(10, 0, 0), r);
    v.push_back(f.FaceCount());
    v.push_back(f.TessellateToClosedMesh(8, 8).Volume());
    v.push_back(1000.0 - (1.0 - ON_PI / 4.0) * r * r * 10.0);
    const Brep c = ChamferConvexEdge(box, Point3d(0, 0, 10), Point3d(10, 0, 10), 1.0 + 0.1 * tid, 1.0 + 0.1 * tid);
    v.push_back(c.FaceCount());
    v.push_back(c.TessellateToClosedMesh(2, 2).Volume());
  });
}

Sig OpSweepFamily(int tid) {
  return Guard([&](std::vector<double>& v) {
    const double r0 = 1.0, r1 = 2.0 + 0.1 * tid, h = 3.0;
    const NurbsCurve profile = NurbsCurve::FromControlPoints(
        {Point3d(r0, 0, 0), Point3d(r1, 0, 0), Point3d(r1, 0, h), Point3d(r0, 0, h), Point3d(r0, 0, 0)}, 1);
    const Brep rev = Brep::Revolve(profile, Point3d(0, 0, 0), Vector3d(0, 0, 1));
    v.push_back(rev.FaceCount());
    v.push_back(rev.TessellateToClosedMesh(48, 16).Volume());
    v.push_back(ON_PI * (r1 * r1 - r0 * r0) * h);
    const NurbsCurve square = NurbsCurve::FromControlPoints(
        {Point3d(0, 0, 0), Point3d(2 + tid, 0, 0), Point3d(2 + tid, 2, 0), Point3d(0, 2, 0), Point3d(0, 0, 0)}, 1);
    const Brep ext = Brep::Extrude(square, Vector3d(0, 0, 5));
    v.push_back(ext.FaceCount());
    v.push_back(ext.TessellateToClosedMesh(16, 16).Volume());
    const NurbsCurve rail = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(0, 0, 6)}, 1);
    const Brep pipe = Brep::Pipe(rail, 0.5 + 0.05 * tid);
    v.push_back(pipe.FaceCount());
    v.push_back(pipe.TessellateToClosedMesh(32, 8).Volume());
  });
}

Sig OpConvexHull(int tid) {
  return Guard([&](std::vector<double>& v) {
    std::vector<Point3d> pts;
    for (int i = 0; i < 40; ++i) {
      const double a = 0.7 * i + tid, b = 0.31 * i;
      pts.emplace_back((3 + tid) * std::sin(b) * std::cos(a), 3 * std::sin(b) * std::sin(a), 3 * std::cos(b));
    }
    const Brep hull = ExactConvexHull(pts);
    v.push_back(hull.FaceCount());
    v.push_back(hull.TessellateToClosedMesh(2, 2).Volume());
  });
}

Sig OpSubD(int tid) {
  return Guard([&](std::vector<double>& v) {
    const Brep box = Brep::Box(0, 0, 0, 4 + tid, 4, 4);
    const SubD sd = SubD::FromBrep(box, 1);
    const Mesh m = sd.ToApproximateMesh();
    v.push_back(m.VertexCount());
    v.push_back(m.FaceCount());
    v.push_back(m.Area());
  });
}

std::string TempPath(int tid, const char* ext) {
  const char* dir = std::getenv("TMPDIR");
  return std::string(dir && *dir ? dir : "/tmp") + "/dino8_kprobe_" + std::to_string(static_cast<long>(::getpid())) +
         "_t" + std::to_string(tid) + ext;
}

Sig OpFileIo(int tid) {
  return Guard([&](std::vector<double>& v) {
    // 3dm round trip through openNURBS' archive code (zlib, ON_CreateUuid for object ids), plus STEP and IGES curve export
    // (the kernel stamps the export time with std::localtime).
    const std::string p3 = TempPath(tid, ".3dm"), pi = TempPath(tid, ".igs"), ps = TempPath(tid, ".stp");
    Model m;
    m.AddBrep(Brep::Box(0, 0, 0, 2 + tid, 3, 4), "box");
    v.push_back(static_cast<double>(m.Save(p3)));
    Model back;
    v.push_back(static_cast<double>(Model::Load(p3, back)));
    v.push_back(back.ObjectCount());
    const NurbsCurve c = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 2 + tid, 0), Point3d(3, 1, 0), Point3d(4, 4, 0)}, 3);
    v.push_back(static_cast<double>(c.SaveIges(pi)));
    NurbsCurve ci;
    v.push_back(static_cast<double>(NurbsCurve::LoadIges(pi, ci)));
    v.push_back(static_cast<double>(c.SaveStep(ps)));
    NurbsCurve cs;
    v.push_back(static_cast<double>(NurbsCurve::LoadStep(ps, cs)));
    std::remove(p3.c_str());
    std::remove(pi.c_str());
    std::remove(ps.c_str());
  });
}

struct OpDef {
  const char* name;
  Sig (*fn)(int);
};
const OpDef kOps[] = {
    {"box_cyl_mixed", OpBoxCylMixed},      {"box_cyl_general", OpBoxCylGeneral},
    {"sphere_box_union", OpSphereBoxUnion}, {"compound_cutter", OpCompoundCutter},
    {"compound_planar", OpCompoundPlanar},  {"tessellate", OpTessellate},
    {"mass_props", OpMassProps},            {"mesh_boolean", OpMeshBoolean},
};
constexpr int kOpCount = sizeof(kOps) / sizeof(kOps[0]);

const OpDef kFeatureOps[] = {
    {"fillet_chamfer", OpFilletChamfer}, {"sweep_family", OpSweepFamily}, {"convex_hull", OpConvexHull},
    {"subd", OpSubD},                    {"file_io", OpFileIo},
};

// ---------------------------------------------------------------- shared-const-input phase

struct SharedInputs {
  Brep sphere = Brep::Sphere(Point3d(0, 0, 0), 2.0);
  Brep torus = Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), 3.0, 0.7);
  Brep box = Brep::Box(0, 0, 0, 3, 4, 5);
  // Operands of the booleans below: every thread cuts with the SAME const cutter / union operand.
  Brep big_box = Brep::Box(0, 0, 0, 10, 10, 10);
  Brep analytic_hole = MakeAnalyticHole(2.0, -1.0, 12.0);
  Brep slab = Brep::Box(-2, -2, -1, 2, 2, 1);
  Brep cyl_z = MakeCylinderZ(0, 0, -2, 2, 1.0);
  // Welded (ON_RevSurface based) solids: openNURBS caches a bounding box lazily inside const ON_RevSurface methods.
  Brep sphere_welded = Brep::SphereWelded(Point3d(0, 0, 0), 2.0);
  Brep cyl_welded = Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 3.0);
  Brep torus_welded = Brep::TorusWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 3.0, 0.7);
  Mesh mesh = Brep::Sphere(Point3d(0, 0, 0), 2.0).TessellateToClosedMesh(32, 64);
  PointCloud cloud;
  SharedInputs() {
    for (int i = 0; i < 2000; ++i) {
      const double t = 0.37 * i;
      cloud.AppendPoint(Point3d(std::cos(t) * (1 + i % 7), std::sin(t) * (1 + i % 5), 0.01 * i));
    }
  }
};

Sig SharedBrepQueries(const SharedInputs& in, int tid) {
  return Guard([&](std::vector<double>& v) {
    const Mesh m = (tid % 2 ? in.sphere : in.torus).TessellateToClosedMesh(24, 24);
    v.push_back(m.FaceCount());
    v.push_back(m.Volume());
    v.push_back(in.sphere.Volume());
    v.push_back(in.torus.Area());
    const MassProperties mp = in.box.VolumeMassProperties();
    v.push_back(mp.volume);
    v.push_back(mp.ixx);
    v.push_back(in.sphere.TessellateToClosedMeshAdaptive(0.02).FaceCount());
  });
}

Sig SharedBooleanOperands(const SharedInputs& in, int tid) {
  return Guard([&](std::vector<double>& v) {
    // Each thread runs its own boolean, but all of them read the same operand objects.
    const BooleanOp ops[3] = {BooleanOp::Difference, BooleanOp::Union, BooleanOp::Intersection};
    const Brep a = BooleanCombineMixed(in.big_box, in.analytic_hole, ops[tid % 3]);
    v.push_back(a.FaceCount());
    v.push_back(a.TessellateToClosedMesh(16, 16).Volume());
    const Brep b = BooleanCombineGeneral(in.sphere, in.big_box, ops[tid % 3]);
    v.push_back(b.FaceCount());
    v.push_back(b.TessellateToClosedMesh(16, 32).Volume());
    const Brep c = BooleanCombineGeneral(in.slab, in.cyl_z, ops[tid % 3]);
    v.push_back(c.FaceCount());
    v.push_back(TessellateGeneralBooleanClosedMesh(c, 8, 32).Volume());
  });
}

Sig SharedWeldedQueries(const SharedInputs& in, int tid) {
  return Guard([&](std::vector<double>& v) {
    const Brep* solids[3] = {&in.sphere_welded, &in.cyl_welded, &in.torus_welded};
    const Brep& s = *solids[tid % 3];
    v.push_back(s.FaceCount());
    v.push_back(s.TessellateToClosedMesh(16, 16).Volume());
    v.push_back(s.VolumeOfSealedSolid());
    const ON_BoundingBox bb = s.raw().BoundingBox();
    v.push_back(bb.m_max.x - bb.m_min.x);
    v.push_back(s.raw().IsValid() ? 1 : 0);
  });
}

Sig SharedMeshQueries(const SharedInputs& in, int tid) {
  return Guard([&](std::vector<double>& v) {
    const Point3d p(0.1 * tid, 0.2, 0.3);
    v.push_back(in.mesh.ContainsPoint(p) ? 1 : 0);
    const Point3d q = in.mesh.ClosestPoint(Point3d(5, 5 - tid, 5));
    v.push_back(q.x);
    v.push_back(q.y);
    v.push_back(q.z);
    v.push_back(in.mesh.SignedDistance(Point3d(0, 0, 3)));
    v.push_back(in.mesh.Volume());
  });
}

Sig SharedPointCloudQuery(const SharedInputs& in, int tid) {
  return Guard([&](std::vector<double>& v) {
    const std::vector<PointCloudNeighbor> nn = in.cloud.KNearest(Point3d(0.5 * tid, 0.25, 0.0), 5);
    for (const PointCloudNeighbor& n : nn) v.push_back(n.index);
  });
}

struct SharedDef {
  const char* name;
  Sig (*fn)(const SharedInputs&, int);
};
const SharedDef kShared[] = {
    {"shared_brep_queries", SharedBrepQueries},
    {"shared_welded_queries", SharedWeldedQueries},
    {"shared_boolean_operands", SharedBooleanOperands},
    {"shared_mesh_queries", SharedMeshQueries},
    {"shared_pointcloud_knearest", SharedPointCloudQuery},
};
constexpr int kSharedCount = sizeof(kShared) / sizeof(kShared[0]);

// ---------------------------------------------------------------- stack high-water mark

// Runs `fn` on a thread whose stack is a private 32 MiB mapping pre-filled with a pattern (lowest page protected),
// then reports how many bytes of that stack were touched. Worker threads of an engine usually have a smaller stack
// (a Rust thread: 2 MiB by default) than the 8 MiB main thread, so the kernel's peak stack use is a hard requirement.
struct StackProbe {
  std::function<void()> fn;
  void* base = nullptr;
  size_t size = 0;
  size_t used = 0;
};

void* StackProbeMain(void* arg) {
  StackProbe* p = static_cast<StackProbe*>(arg);
  p->fn();
  return nullptr;
}

size_t PeakStackBytes(const std::function<void()>& fn) {
  const size_t size = 32u << 20;
  void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (mem == MAP_FAILED) return 0;
  const unsigned long long kPattern = 0xA5A5A5A5A5A5A5A5ull;
  unsigned long long* w = static_cast<unsigned long long*>(mem);
  for (size_t i = 0; i < size / 8; ++i) w[i] = kPattern;
  mprotect(mem, 4096, PROT_NONE);  // overflow faults instead of corrupting
  StackProbe probe;
  probe.fn = fn;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstack(&attr, static_cast<char*>(mem) + 4096, size - 4096);
  pthread_t th;
  size_t used = 0;
  if (pthread_create(&th, &attr, StackProbeMain, &probe) == 0) {
    pthread_join(th, nullptr);
    size_t i = 4096 / 8;
    while (i < size / 8 && w[i] == kPattern) ++i;
    used = size - i * 8;  // bytes from the lowest touched word to the top of the mapping
  }
  pthread_attr_destroy(&attr);
  munmap(mem, size);
  return used;
}

// ---------------------------------------------------------------- harness

using Clock = std::chrono::steady_clock;
double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

long ThreadCpuUs() {  // CPU time of the calling thread: unaffected by other processes sharing the machine
  timespec ts;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return static_cast<long>(ts.tv_sec) * 1000000L + ts.tv_nsec / 1000;
}

struct OpStats {
  std::atomic<long> cpu_us{0};
  std::atomic<long> calls{0};
  std::atomic<long> divergences{0};
  std::atomic<long> threw{0};
  std::atomic<long> total_us{0};
  std::atomic<long> max_us{0};
};

struct Gate {  // start gate so that all threads begin an iteration together
  std::atomic<int> arrived{0};
  std::atomic<long> generation{0};
  int count;
  explicit Gate(int n) : count(n) {}
  void Wait() {
    const long gen = generation.load();
    if (arrived.fetch_add(1) + 1 == count) {
      arrived.store(0);
      generation.fetch_add(1);
    } else {
      while (generation.load() == gen) std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
};

std::mutex g_print_mutex;
std::atomic<int> g_printed{0};

void ReportDivergence(const char* op, int tid, long iter, const Sig& got, const Sig& want) {
  if (g_printed.fetch_add(1) >= 12) return;
  std::lock_guard<std::mutex> lock(g_print_mutex);
  std::printf("DIVERGENCE op=%s thread=%d iter=%ld\n  got  %s\n  want %s\n", op, tid, iter, SigText(got).c_str(),
              SigText(want).c_str());
  std::fflush(stdout);
}

struct Options {
  int threads = 4;
  long iters = 200;
  double seconds = 1e18;
  std::string phase = "both";  // indep | features | shared | both (= indep + shared) | all
  std::string ops = "all";
  int stagger = 2;
  bool prewarm_bbox = false;  // shared phase: compute every shared Brep's cached bounding box on the main thread first
  bool stack_report = false;  // print each op's peak stack use on a pattern-filled private stack, then exit
  bool cold = false;  // skip the serial reference: the first call of every op happens concurrently (first-use races)
};

bool Selected(const Options& o, const char* name) {
  if (o.ops == "all") return true;
  std::stringstream ss(o.ops);
  std::string tok;
  while (std::getline(ss, tok, ','))
    if (tok == name) return true;
  return false;
}

// Runs one phase. `ops` are generic callables (tid, thread-private?) -> Sig.
struct Task {
  const char* name;
  std::function<Sig(int)> fn;
};

// `prepare` (may be empty) runs on thread 0 at the top of every iteration while the other threads wait at the
// gate, so lazily-built caches can be reset to the "first use" state before the threads race to build them.
int RunPhase(const char* phase_name, const Options& o, const std::vector<Task>& tasks,
             const std::function<void()>& prepare = nullptr) {
  if (tasks.empty()) return 0;
  const int T = o.threads;
  const int K = static_cast<int>(tasks.size());
  std::printf("\n== phase %s: %d threads, up to %ld iterations x %d ops (stagger=%d, budget %.0fs)\n", phase_name, T,
              o.iters, K, o.stagger, o.seconds > 1e17 ? 0.0 : o.seconds);

  // Serial reference per (task, tid), twice: the second pass proves the kernel is deterministic single-threaded.
  std::vector<std::vector<Sig>> ref(K, std::vector<Sig>(T));
  std::vector<std::vector<char>> have_ref(K, std::vector<char>(T, o.cold ? 0 : 1));
  bool deterministic = true;
  for (int k = 0; k < K && !o.cold; ++k) {
    for (int t = 0; t < T; ++t) {
      const auto t0 = Clock::now();
      const int err0 = ON_GetErrorCount(), warn0 = ON_GetWarningCount();
      ref[k][t] = tasks[k].fn(t);
      const int err_delta = ON_GetErrorCount() - err0, warn_delta = ON_GetWarningCount() - warn0;
      const double ms = Ms(Clock::now() - t0);
      const Sig again = tasks[k].fn(t);
      if (!SameSig(ref[k][t], again)) {
        deterministic = false;
        std::printf("NOTE serial run of %s tid=%d is not reproducible: %s vs %s\n", tasks[k].name, t,
                    SigText(ref[k][t]).c_str(), SigText(again).c_str());
      }
      if (t == 0) {
        std::printf("ref  %-26s %8.1f ms  tid0 -> %s\n", tasks[k].name, ms, SigText(ref[k][t]).c_str());
        if (err_delta || warn_delta)
          std::printf("     ^ this call raised %d ON_ERROR and %d ON_WARNING events (openNURBS' global counters)\n", err_delta,
                      warn_delta);
      }
    }
  }
  if (o.cold) std::printf("cold start: no serial reference; iteration 0 of each (op, thread) becomes its reference\n");
  else std::printf("serial reference deterministic: %s\n", deterministic ? "yes" : "NO");
  std::fflush(stdout);

  std::vector<OpStats> stats(K);
  Gate gate(T);
  std::atomic<bool> stop{false};
  std::atomic<long> iterations_done{0};
  const auto start = Clock::now();

  auto worker = [&](int tid) {
    for (long it = 0; it < o.iters; ++it) {
      gate.Wait();  // all threads enter the iteration together; thread 0 decides whether to continue
      if (tid == 0) {
        if (Ms(Clock::now() - start) / 1000.0 > o.seconds) stop.store(true);
        else if (prepare) prepare();
      }
      gate.Wait();
      if (stop.load()) break;
      const bool rotate = o.stagger == 1 || (o.stagger == 2 && (it % 2 == 1));
      for (int j = 0; j < K; ++j) {
        const int k = rotate ? (j + tid) % K : j;
        const auto t0 = Clock::now();
        const long cpu0 = ThreadCpuUs();
        const Sig got = tasks[k].fn(tid);
        stats[k].cpu_us += ThreadCpuUs() - cpu0;
        const long us = static_cast<long>(Ms(Clock::now() - t0) * 1000.0);
        stats[k].calls++;
        stats[k].total_us += us;
        long prev = stats[k].max_us.load();
        while (us > prev && !stats[k].max_us.compare_exchange_weak(prev, us)) {}
        if (!got.err.empty()) stats[k].threw++;
        if (!have_ref[k][tid]) {  // cold mode: only thread `tid` ever touches (k, tid)
          ref[k][tid] = got;
          have_ref[k][tid] = 1;
          continue;
        }
        if (!SameSig(got, ref[k][tid])) {
          stats[k].divergences++;
          ReportDivergence(tasks[k].name, tid, it, got, ref[k][tid]);
        }
      }
      if (tid == 0) iterations_done++;
    }
  };

  std::vector<std::thread> pool;
  for (int t = 0; t < T; ++t) pool.emplace_back(worker, t);
  for (std::thread& th : pool) th.join();

  const double wall = Ms(Clock::now() - start) / 1000.0;
  long total_div = 0;
  std::printf("phase %s finished: %ld iterations per thread in %.1f s wall\n", phase_name, iterations_done.load(), wall);
  for (int k = 0; k < K; ++k) {
    const long c = stats[k].calls.load();
    std::printf("  %-26s calls=%-5ld avg=%8.1f ms (thread cpu %8.1f ms) max=%8.1f ms threw=%-4ld divergences=%ld\n",
                tasks[k].name, c, c ? stats[k].total_us.load() / 1000.0 / c : 0.0,
                c ? stats[k].cpu_us.load() / 1000.0 / c : 0.0, stats[k].max_us.load() / 1000.0, stats[k].threw.load(),
                stats[k].divergences.load());
    total_div += stats[k].divergences.load();
  }
  std::printf("phase %s divergences from the serial reference: %ld\n", phase_name, total_div);
  std::fflush(stdout);
  return static_cast<int>(total_div);
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--threads") o.threads = std::atoi(next().c_str());
    else if (a == "--iters") o.iters = std::atol(next().c_str());
    else if (a == "--seconds") o.seconds = std::atof(next().c_str());
    else if (a == "--phase") o.phase = next();
    else if (a == "--ops") o.ops = next();
    else if (a == "--stagger") o.stagger = std::atoi(next().c_str());
    else if (a == "--cold") o.cold = true;
    else if (a == "--stack-report") o.stack_report = true;
    else if (a == "--prewarm-bbox") o.prewarm_bbox = true;
    else if (a == "--list") {
      for (const OpDef& d : kOps) std::printf("%s\n", d.name);
      for (const OpDef& d : kFeatureOps) std::printf("%s\n", d.name);
      for (const SharedDef& d : kShared) std::printf("%s\n", d.name);
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument %s\n", a.c_str());
      return 2;
    }
  }
  if (o.threads < 1) o.threads = 1;

  if (o.stack_report) {
    std::printf("peak stack use per operation on a worker thread (32 MiB pattern-filled private stack):\n");
    auto report = [&](const OpDef& d) {
      if (!Selected(o, d.name)) return;
      const size_t first = PeakStackBytes([&] { d.fn(0); });  // includes first-call initialisation
      const size_t again = PeakStackBytes([&] { d.fn(3); });
      std::printf("  %-18s %8.1f KiB (first call %8.1f KiB)\n", d.name, again / 1024.0, first / 1024.0);
    };
    for (const OpDef& d : kOps) report(d);
    for (const OpDef& d : kFeatureOps) report(d);
    return 0;
  }

  int divergences = 0;
  if (o.phase == "indep" || o.phase == "both" || o.phase == "all") {
    std::vector<Task> tasks;
    for (const OpDef& d : kOps)
      if (Selected(o, d.name)) tasks.push_back({d.name, [fn = d.fn](int tid) { return fn(tid); }});
    divergences += RunPhase("indep", o, tasks);
  }
  if (o.phase == "features" || o.phase == "all") {
    std::vector<Task> tasks;
    for (const OpDef& d : kFeatureOps)
      if (Selected(o, d.name)) tasks.push_back({d.name, [fn = d.fn](int tid) { return fn(tid); }});
    divergences += RunPhase("features", o, tasks);
  }
  if (o.phase == "shared" || o.phase == "both" || o.phase == "all") {
    SharedInputs shared;  // built once, then only read through const references
    if (o.prewarm_bbox) {
      // openNURBS fills ON_Brep::m_bbox lazily inside the const BoundingBox() call; doing it once up front, before the
      // object is visible to other threads, is the warm-up the engine should do when it publishes an immutable Brep.
      for (const Brep* b : {&shared.sphere, &shared.torus, &shared.box, &shared.big_box, &shared.analytic_hole, &shared.slab,
                            &shared.cyl_z, &shared.sphere_welded, &shared.cyl_welded, &shared.torus_welded})
        (void)b->raw().BoundingBox();
    }
    std::vector<Task> tasks;
    for (const SharedDef& d : kShared)
      if (Selected(o, d.name)) tasks.push_back({d.name, [&shared, fn = d.fn](int tid) { return fn(shared, tid); }});
    divergences += RunPhase("shared", o, tasks, [&shared] {
      shared.cloud.SetPointAt(0, shared.cloud.PointAt(0));  // drops the lazily built R-tree
      const Mesh copy = shared.mesh;
      shared.mesh = copy;  // copy-assignment drops the Mesh's lazily built spatial index
    });
  }
  // openNURBS keeps process-global, non-atomic error counters (opennurbs_error.cpp): non-zero values mean ON_ERROR /
  // ON_WARNING fired during the run, i.e. those increments were exposed to concurrent execution.
  std::printf("\nopenNURBS global counters after the run: errors=%d warnings=%d math_errors=%d\n", ON_GetErrorCount(),
              ON_GetWarningCount(), ON_GetMathErrorCount());
  std::printf("PROBE DONE divergences=%d\n", divergences);
  return divergences ? 1 : 0;
}
