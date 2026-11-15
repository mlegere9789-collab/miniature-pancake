// Kernel benchmark harness for the CI-measurable rows of the Master Plan's Appendix B (unified performance budgets).
//
//   dino8_bench_budgets [--out file.json] [--commit <hash>] [--reps N] [--scale quick|full] [--only <substring>]
//
// Standalone executable (NOT part of dino8_kernel_tests, NOT registered with add_test): it prints one human line per metric
// on stderr and writes ONE machine-readable JSON document (default: stdout) of the form
//
//   { "schema": "dino8-bench/1", "commit": "...", "host": {...},
//     "metrics": [ { "metric": "nurbs_boolean_ms", "value": 12.3, "unit": "ms", "dataset": "prism_faces_100", "commit": "..." }, ... ] }
//
// Each metric is the MEDIAN of `--reps` timed runs (default 5; one untimed warm-up first), so a single scheduling hiccup on a
// shared CI runner does not move it. Inputs are deterministic synthetic solids built from the kernel's own primitives, scaled
// to what the kernel can do today (a regular N-gon prism has N+2 planar faces; a "comb" has teeth; a torus/sphere are single
// rational-NURBS faces). parity/tools/perf_compare.py compares two such files (3% regression rule) and maps metric+dataset to
// the Appendix B gate. A measurement that throws is reported with value -1 and "error" set, never silently dropped, so the
// comparison script can fail on it.
//
// Appendix B rows covered here (the rest - cold start, orbit, sync, publish, path tracing, memory - need the app/GPU and are
// out of scope for a kernel CI job):
//   NURBS boolean, 1,000 faces            -> nurbs_boolean_ms / planar_boolean_ms        (gate 1000 ms at 1000 faces)
//   Section or elevation regeneration     -> section_ms (one view), section_20_views_ms  (gate 1000 ms / 3000 ms)
//   Mesh boolean, two 1 M-triangle meshes -> mesh_boolean_ms                             (gate 2000 ms at 1 M; scaled rows are indicative)
//   Tessellation, mass properties         -> tessellate_ms, mass_properties_ms           (no Appendix B row: tracked for the 3% rule)
//   Undo or redo (kernel side)            -> brep_copy_ms: undo snapshots are Brep copies  (gate 30 ms)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface_intersect.h"

using namespace dino8::kernel;

namespace {

struct Metric {
  std::string metric, unit, dataset, error;
  double value = 0.0;
  double spread = 0.0;  // (max - min) / median over the timed runs, a noise indicator
  int reps = 0;
};

std::vector<Metric> g_metrics;
std::string g_commit = "unknown";
std::string g_only;
int g_reps = 5;

double NowMs() {
  using clk = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clk::now().time_since_epoch()).count();
}

// Times fn() `g_reps` times after one untimed warm-up and records the median in milliseconds. `fn` may return a checksum so
// the optimizer cannot discard the work; it is folded into a volatile sink.
volatile double g_sink = 0.0;
void Measure(const std::string& metric, const std::string& dataset, const std::function<double()>& fn) {
  if (!g_only.empty() && metric.find(g_only) == std::string::npos && dataset.find(g_only) == std::string::npos) return;
  Metric m;
  m.metric = metric;
  m.dataset = dataset;
  m.unit = "ms";
  try {
    const double w0 = NowMs();
    g_sink = g_sink + fn();
    // Slow cases (warm-up over 4 s) get fewer timed runs so the whole harness stays inside a few minutes on a CI runner; the
    // number of runs actually used is recorded as "reps" in the JSON.
    const double warm = NowMs() - w0;
    const int reps = warm > 8000.0 ? 1 : (warm > 4000.0 ? std::min(g_reps, 3) : g_reps);
    std::vector<double> t;
    for (int i = 0; i < reps; ++i) {
      const double t0 = NowMs();
      g_sink = g_sink + fn();
      t.push_back(NowMs() - t0);
    }
    std::sort(t.begin(), t.end());
    m.value = t[t.size() / 2];
    m.spread = m.value > 0 ? (t.back() - t.front()) / m.value : 0.0;
    m.reps = reps;
  } catch (const std::exception& e) {
    m.value = -1.0;
    m.error = e.what();
  } catch (...) {
    m.value = -1.0;
    m.error = "unknown exception";
  }
  std::fprintf(stderr, "%-24s %-26s %12.3f ms  (spread %.0f%%)%s%s\n", m.metric.c_str(), m.dataset.c_str(), m.value,
               m.spread * 100.0, m.error.empty() ? "" : "  ERROR: ", m.error.c_str());
  g_metrics.push_back(m);
}

// Regular n-gon prism, centered on the z axis: n side quads + 2 n-gon caps = n + 2 planar faces.
Brep MakePrism(int n, double radius, double z0, double z1) {
  std::vector<Point3d> b, t;
  for (int i = 0; i < n; ++i) {
    const double a = 2.0 * M_PI * i / n;
    b.emplace_back(radius * std::cos(a), radius * std::sin(a), z0);
    t.emplace_back(radius * std::cos(a), radius * std::sin(a), z1);
  }
  auto make_face = [](std::vector<Point3d> loop, ON_3dVector normal) {
    Brep::PlanarFace f;
    f.loop = std::move(loop);
    f.plane = ON_Plane(f.loop[0], normal);
    return f;
  };
  std::vector<Brep::PlanarFace> faces;
  std::vector<Point3d> bottom(b.rbegin(), b.rend());
  faces.push_back(make_face(bottom, ON_3dVector(0, 0, -1)));
  faces.push_back(make_face(t, ON_3dVector(0, 0, 1)));
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    ON_3dVector nrm = ON_CrossProduct(b[j] - b[i], t[i] - b[i]);
    nrm.Unitize();
    faces.push_back(make_face({b[i], b[j], t[j], t[i]}, nrm));
  }
  return Brep::FromPlanarFaces(faces);
}

// Scale ladder: the Appendix B gate is stated for 1,000 faces; the ladder 100 / 300 / 1,000 shows how the cost grows.
const int kFaceLadder[] = {100, 300, 1000};

double MeshChecksum(const Mesh& m) { return static_cast<double>(m.FaceCount()) + 1e-9 * static_cast<double>(m.VertexCount()); }

}  // namespace

int main(int argc, char** argv) {
  const char* out_path = nullptr;
  bool quick = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (a == "--commit" && i + 1 < argc) g_commit = argv[++i];
    else if (a == "--reps" && i + 1 < argc) g_reps = std::max(1, std::atoi(argv[++i]));
    else if (a == "--scale" && i + 1 < argc) quick = (std::string(argv[++i]) == "quick");
    else if (a == "--only" && i + 1 < argc) g_only = argv[++i];
    else {
      std::fprintf(stderr, "usage: %s [--out f.json] [--commit h] [--reps N] [--scale quick|full] [--only substr]\n", argv[0]);
      return 2;
    }
  }
  ON::Begin();

  // ---- 1. Boolean on N-face operands (Difference of an N-gon prism and an off-center slab that cuts through it) ----
  for (int faces : kFaceLadder) {
    if (quick && faces > 300) continue;
    const int n = faces - 2;
    const std::string ds = "prism_faces_" + std::to_string(faces);
    const Brep prism = MakePrism(n, 10.0, 0.0, 4.0);
    const Brep slab = Brep::Box(2.0, -3.0, 1.0, 14.0, 3.0, 3.0);
    Measure("nurbs_boolean_ms", ds, [&] {
      const Brep r = BooleanCombineGeneral(prism, slab, BooleanOp::Difference);
      return static_cast<double>(r.FaceCount());
    });
    Measure("planar_boolean_ms", ds, [&] {
      const Brep r = BooleanCombinePlanar(prism, slab, BooleanOp::Difference);
      return static_cast<double>(r.FaceCount());
    });
  }

  // ---- 2. Tessellation, mass properties, kernel-side undo snapshot on the same N-face solids and on single NURBS faces ----
  for (int faces : kFaceLadder) {
    if (quick && faces > 300) continue;
    const std::string ds = "prism_faces_" + std::to_string(faces);
    const Brep prism = MakePrism(faces - 2, 10.0, 0.0, 4.0);
    Measure("tessellate_ms", ds, [&] { return MeshChecksum(prism.TessellateToClosedMesh(8, 8)); });
    const Mesh prism_mesh = prism.TessellateToClosedMesh(8, 8);
    Measure("mass_properties_ms", ds, [&] { return prism.Volume() + prism.Area(); });
    Measure("mesh_mass_properties_ms", ds, [&] { return prism_mesh.Volume() + prism_mesh.Area() + prism_mesh.GetCentroid().x; });
    Measure("brep_copy_ms", ds, [&] {
      const Brep copy = prism;
      return static_cast<double>(copy.FaceCount());
    });
  }
  {
    const Brep sphere = Brep::Sphere(Point3d(0, 0, 0), 5.0);
    Measure("tessellate_ms", "nurbs_sphere_1face_32x32", [&] { return MeshChecksum(sphere.TessellateToClosedMesh(32, 32)); });
    Measure("tessellate_ms", "nurbs_sphere_1face_128x128", [&] { return MeshChecksum(sphere.TessellateToClosedMesh(128, 128)); });
    Measure("mass_properties_ms", "nurbs_sphere_1face", [&] { return sphere.Volume() + sphere.Area(); });
    const Brep torus = Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), 6.0, 2.0);
    Measure("tessellate_ms", "nurbs_torus_1face_64x64", [&] { return MeshChecksum(torus.TessellateToClosedMesh(64, 64)); });
    Measure("mass_properties_ms", "nurbs_torus_1face", [&] { return torus.Volume() + torus.Area(); });
  }

  // ---- 3. Section generation: exact plane section of the N-face prism (one view) and 20 views back to back ----
  for (int faces : kFaceLadder) {
    if (quick && faces > 300) continue;
    const std::string ds = "prism_faces_" + std::to_string(faces);
    const Brep prism = MakePrism(faces - 2, 10.0, 0.0, 4.0);
    IntersectOptions opt;
    opt.refuse_overshoot = true;
    auto section_at = [&](double z) {
      const ON_Plane plane(ON_3dPoint(0, 0, z), ON_3dVector(0, 0, 1));
      const std::vector<BrepSectionContour> c = SectionBrepByPlaneJoined(prism.raw(), plane, opt);
      return static_cast<double>(c.size());
    };
    Measure("section_ms", ds, [&] { return section_at(2.0); });
    Measure("section_20_views_ms", ds, [&] {
      double s = 0;
      for (int v = 0; v < 20; ++v) s += section_at(0.1 + 3.8 * v / 19.0);
      return s;
    });
  }

  // ---- 4. Mesh boolean (Manifold): two closed torus meshes of about 230,000 triangles each (the Appendix B row is 1 M each) ----
  {
    const int major = quick ? 160 : 480, minor = quick ? 80 : 240;  // major * minor quad faces each
    const Mesh a = Mesh::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), 6.0, 2.0, major, minor);
    const Mesh b = Mesh::Torus(Point3d(6.0, 0, 0), Vector3d(1, 0, 0), 6.0, 2.0, major, minor);
    const std::string ds = "torus_pair_" + std::to_string(a.FaceCount()) + "_tris_each";
    Measure("mesh_boolean_ms", ds, [&] { return MeshChecksum(BooleanCombine(a, b, BooleanOp::Union)); });
  }

  ON::End();

  // ---- JSON output ----
  FILE* f = out_path ? std::fopen(out_path, "w") : stdout;
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", out_path);
    return 2;
  }
  std::fprintf(f, "{\n  \"schema\": \"dino8-bench/1\",\n  \"commit\": \"%s\",\n  \"reps\": %d,\n  \"metrics\": [\n", g_commit.c_str(),
               g_reps);
  for (size_t i = 0; i < g_metrics.size(); ++i) {
    const Metric& m = g_metrics[i];
    std::string err;
    for (char c : m.error) err += (c == '"' || c == '\\' || (unsigned char)c < 0x20) ? ' ' : c;
    std::fprintf(f,
                 "    {\"metric\": \"%s\", \"value\": %.4f, \"unit\": \"%s\", \"dataset\": \"%s\", \"commit\": \"%s\", "
                 "\"spread\": %.3f, \"reps\": %d%s%s%s}%s\n",
                 m.metric.c_str(), m.value, m.unit.c_str(), m.dataset.c_str(), g_commit.c_str(), m.spread, m.reps,
                 err.empty() ? "" : ", \"error\": \"", err.c_str(), err.empty() ? "" : "\"", i + 1 < g_metrics.size() ? "," : "");
  }
  std::fprintf(f, "  ]\n}\n");
  if (out_path) std::fclose(f);
  int bad = 0;
  for (const Metric& m : g_metrics) bad += m.value < 0 ? 1 : 0;
  return bad ? 1 : 0;
}
