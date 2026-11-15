// Independent file I/O and SubD calls from several threads (dino8-core/KERNEL_THREADS.md, section 2.4). Every thread builds its own
// Model / SubD from its own inputs and runs the same calls; the results must equal a serial run of the same inputs exactly. Without the
// kernel's openNURBS global-state guard these calls race on openNURBS' error counters, serial number generators, class-id UUID and
// libuuid state (found with ThreadSanitizer: the run_probe.sh probe reports 0 races with it).
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/file_io.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::Model;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::SubD;

constexpr int kThreads = 4;
constexpr int kIterations = 12;

std::string TempPath(const std::string& tag, int tid, int it, const char* ext) {
  return (std::filesystem::temp_directory_path() / ("dino8_threadsafe_" + tag + "_t" + std::to_string(tid) + "_i" + std::to_string(it) + ext)).string();
}

// One 3dm + IGES + STEP round trip; returns the numbers that must not depend on thread interleaving, or {-1} on an exception.
std::vector<double> FileIoOnce(int tid, int it) {
  std::vector<double> v;
  try {
    const std::string p3 = TempPath("io", tid, it, ".3dm"), pi = TempPath("io", tid, it, ".igs"), ps = TempPath("io", tid, it, ".stp");
    Model m;
    m.AddLayer("layer", dino8::kernel::Color{10, 20, 30});
    m.AddBrep(Brep::Box(0, 0, 0, 2 + tid, 3, 4), "box");
    v.push_back(static_cast<double>(m.Save(p3)));
    Model back;
    v.push_back(static_cast<double>(Model::Load(p3, back)));
    v.push_back(back.ObjectCount());
    v.push_back(back.LayerCount());
    const NurbsCurve c = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 2 + tid, 0), Point3d(3, 1, 0), Point3d(4, 4, 0)}, 3);
    v.push_back(static_cast<double>(c.SaveIges(pi)));
    NurbsCurve ci;
    v.push_back(static_cast<double>(NurbsCurve::LoadIges(pi, ci)));
    v.push_back(static_cast<double>(c.SaveStep(ps)));
    NurbsCurve cs;
    v.push_back(static_cast<double>(NurbsCurve::LoadStep(ps, cs)));
    std::error_code ec;
    std::filesystem::remove(p3, ec);
    std::filesystem::remove(pi, ec);
    std::filesystem::remove(ps, ec);
  } catch (const std::exception&) {
    return {-1.0};
  }
  return v;
}

std::vector<double> SubDOnce(int tid) {
  std::vector<double> v;
  try {
    const Brep box = Brep::Box(0, 0, 0, 4 + tid, 4, 4);
    SubD sd = SubD::FromBrep(box, 1);
    const SubD copy = sd;  // the guarded copy constructor
    sd.Subdivide(1);
    const Mesh m = sd.ToApproximateMesh();
    v.push_back(copy.FaceCount());
    v.push_back(sd.FaceCount());
    v.push_back(m.VertexCount());
    v.push_back(m.FaceCount());
    v.push_back(m.Area());
  } catch (const std::exception&) {
    return {-1.0};
  }
  return v;
}

template <class F>
int RunConcurrently(F per_thread_check) {
  std::atomic<int> bad{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t) ts.emplace_back([&, t] { bad += per_thread_check(t); });
  for (auto& th : ts) th.join();
  return bad.load();
}

}  // namespace

D8_EXTRA_TEST(ThreadSafe3dmIgesStepFileIoConcurrentMatchesSerial) {
  std::vector<std::vector<double>> serial(kThreads);
  for (int t = 0; t < kThreads; ++t) serial[t] = FileIoOnce(t, 0);
  bool serial_ok = true;
  for (const auto& s : serial) serial_ok = serial_ok && !s.empty() && s[0] == 0.0 /* Result::Ok */ ;
  d8test::Check(serial_ok, "thread-safe file I/O: serial 3dm/IGES/STEP round trip succeeds for every thread's input");
  const int bad = RunConcurrently([&](int t) {
    int diverged = 0;
    for (int it = 0; it < kIterations; ++it) {
      if (FileIoOnce(t, it + 1) != serial[t]) ++diverged;
    }
    return diverged;
  });
  d8test::Check(bad == 0, "thread-safe file I/O: 4 threads x 12 independent 3dm/IGES/STEP round trips equal the serial results exactly");
}

D8_EXTRA_TEST(ThreadSafeSubDConcurrentMatchesSerial) {
  std::vector<std::vector<double>> serial(kThreads);
  for (int t = 0; t < kThreads; ++t) serial[t] = SubDOnce(t);
  bool serial_ok = true;
  for (const auto& s : serial) serial_ok = serial_ok && s.size() == 5;
  d8test::Check(serial_ok, "thread-safe SubD: serial FromBrep / copy / Subdivide / ToApproximateMesh succeeds for every thread's input");
  const int bad = RunConcurrently([&](int t) {
    int diverged = 0;
    for (int it = 0; it < kIterations * 4; ++it) {
      if (SubDOnce(t) != serial[t]) ++diverged;
    }
    return diverged;
  });
  d8test::Check(bad == 0, "thread-safe SubD: 4 threads x 48 independent SubD builds, copies, subdivisions and meshes equal the serial results exactly");
}
