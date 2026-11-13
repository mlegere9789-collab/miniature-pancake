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
// two derived-data queries read one input snapshot): Brep tessellation and mass properties, Mesh queries, and
// a PointCloud nearest-neighbour query.
//
// Every thread's results are compared with a serial reference computed before any thread starts, so
// cross-thread interference shows up as a value divergence even in a build without ThreadSanitizer. Built with
// -fsanitize=thread, any data race is reported by the runtime with both stacks.
//
// usage: probe [--threads N] [--iters N] [--seconds T] [--phase indep|shared|both] [--ops a,b|all]
//              [--stagger 0|1|2] [--list]
//   --stagger 0: every thread runs the same op at the same time (hits one engine's globals hardest)
//   --stagger 1: thread t starts at op t, so different engines overlap (cross-engine interference)
//   --stagger 2: alternate 0 and 1 per iteration (default)
//   --seconds T: stop starting new iterations after T seconds (the shared build machine is small)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/point_cloud.h"
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

// ---------------------------------------------------------------- shared-const-input phase

struct SharedInputs {
  Brep sphere = Brep::Sphere(Point3d(0, 0, 0), 2.0);
  Brep torus = Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), 3.0, 0.7);
  Brep box = Brep::Box(0, 0, 0, 3, 4, 5);
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
    {"shared_mesh_queries", SharedMeshQueries},
    {"shared_pointcloud_knearest", SharedPointCloudQuery},
};
constexpr int kSharedCount = sizeof(kShared) / sizeof(kShared[0]);

// ---------------------------------------------------------------- harness

using Clock = std::chrono::steady_clock;
double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

struct OpStats {
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
  std::string phase = "both";
  std::string ops = "all";
  int stagger = 2;
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
  bool deterministic = true;
  for (int k = 0; k < K; ++k) {
    for (int t = 0; t < T; ++t) {
      const auto t0 = Clock::now();
      ref[k][t] = tasks[k].fn(t);
      const double ms = Ms(Clock::now() - t0);
      const Sig again = tasks[k].fn(t);
      if (!SameSig(ref[k][t], again)) {
        deterministic = false;
        std::printf("NOTE serial run of %s tid=%d is not reproducible: %s vs %s\n", tasks[k].name, t,
                    SigText(ref[k][t]).c_str(), SigText(again).c_str());
      }
      if (t == 0) std::printf("ref  %-26s %8.1f ms  tid0 -> %s\n", tasks[k].name, ms, SigText(ref[k][t]).c_str());
    }
  }
  std::printf("serial reference deterministic: %s\n", deterministic ? "yes" : "NO");
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
        const Sig got = tasks[k].fn(tid);
        const long us = static_cast<long>(Ms(Clock::now() - t0) * 1000.0);
        stats[k].calls++;
        stats[k].total_us += us;
        long prev = stats[k].max_us.load();
        while (us > prev && !stats[k].max_us.compare_exchange_weak(prev, us)) {}
        if (!got.err.empty()) stats[k].threw++;
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
    std::printf("  %-26s calls=%-5ld avg=%8.1f ms max=%8.1f ms threw=%-4ld divergences=%ld\n", tasks[k].name, c,
                c ? stats[k].total_us.load() / 1000.0 / c : 0.0, stats[k].max_us.load() / 1000.0,
                stats[k].threw.load(), stats[k].divergences.load());
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
    else if (a == "--list") {
      for (const OpDef& d : kOps) std::printf("%s\n", d.name);
      for (const SharedDef& d : kShared) std::printf("%s\n", d.name);
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument %s\n", a.c_str());
      return 2;
    }
  }
  if (o.threads < 1) o.threads = 1;

  int divergences = 0;
  if (o.phase == "indep" || o.phase == "both") {
    std::vector<Task> tasks;
    for (const OpDef& d : kOps)
      if (Selected(o, d.name)) tasks.push_back({d.name, [fn = d.fn](int tid) { return fn(tid); }});
    divergences += RunPhase("indep", o, tasks);
  }
  if (o.phase == "shared" || o.phase == "both") {
    SharedInputs shared;  // built once, then only read through const references
    std::vector<Task> tasks;
    for (const SharedDef& d : kShared)
      if (Selected(o, d.name)) tasks.push_back({d.name, [&shared, fn = d.fn](int tid) { return fn(shared, tid); }});
    divergences += RunPhase("shared", o, tasks, [&shared] {
      shared.cloud.SetPointAt(0, shared.cloud.PointAt(0));  // drops the lazily built R-tree
      const Mesh copy = shared.mesh;
      shared.mesh = copy;  // copy-assignment drops the Mesh's lazily built spatial index
    });
  }
  std::printf("\nPROBE DONE divergences=%d\n", divergences);
  return divergences ? 1 : 0;
}
