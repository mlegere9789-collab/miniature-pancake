// Degenerate-input fuzz of the public Brep entry points (boolean / offset / shell / fillet / chamfer / loft / sweep / extrude / revolve /
// pipe / curve array). No fuzzing library: a fixed table of hostile arguments (NaN, +-inf, 0, negative sizes, 1e-12 and 1e12 scale, an empty
// Brep, a faceless Brep, a zero-length direction, coincident points, an out-of-range face index, an empty / one-point / coincident curve).
//
// Contract checked for EVERY call: it either returns a valid Brep (IsValid(), at least one face, finite bounding box) or throws a
// std::exception. It must never crash, never hang, never return an invalid / empty / non-finite Brep silently. Each call runs in its own
// forked child under an alarm (QC_FUZZ_SECONDS, default 20), so one crash or hang fails only its own check and the rest still run;
// the check names the call and the argument set. POSIX only (the check is skipped where fork() does not exist).
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/features.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/offset_solid.h"
#include "test_registry.h"

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#define QC_HAVE_FORK 1
#endif

namespace {

using dino8::kernel::Brep;
using dino8::kernel::BooleanOp;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;
using d8test::Check;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

enum Outcome { kOk = 0, kThrew = 10, kInvalid = 11, kNonFinite = 12, kOtherThrow = 13, kEmpty = 14, kRefused = 15 };

// `strict` = also demand raw().IsValid(). Brep::Box / Sphere / Torus / CylinderWelded-less primitives are documented to be unwelded surface
// collections that IsValid() rejects even for sane input, so the primitives are judged on non-empty + finite only.
int Judge(const Brep& b, bool strict = true, bool allow_empty = false) {
  if (b.FaceCount() == 0) return allow_empty ? static_cast<int>(kOk) : static_cast<int>(kEmpty);
  const ON_BoundingBox bb = b.raw().BoundingBox();
  if (!bb.IsValid() || !std::isfinite(bb.m_min.x) || !std::isfinite(bb.m_min.y) || !std::isfinite(bb.m_min.z) || !std::isfinite(bb.m_max.x) ||
      !std::isfinite(bb.m_max.y) || !std::isfinite(bb.m_max.z))
    return kNonFinite;
  if (strict && !b.raw().IsValid()) return kInvalid;
  return kOk;
}

using Case = std::function<int()>;  // returns an Outcome (kOk / kEmpty / kInvalid / kNonFinite); exceptions are classified by the runner

// Runs `c` in a forked child with an alarm. Returns an Outcome, or -signal for a crash / hang (-14 = SIGALRM = the time limit).
int RunForked(const Case& c) {
#ifdef QC_HAVE_FORK
  const char* env = std::getenv("QC_FUZZ_SECONDS");
  const unsigned secs = env ? static_cast<unsigned>(std::atoi(env)) : 20u;
  const pid_t pid = fork();
  if (pid < 0) return kOk;
  if (pid == 0) {
    alarm(secs);
    int code = kOk;
    try {
      code = c();
    } catch (const std::exception&) {
      code = kThrew;
    } catch (...) {
      code = kOtherThrow;
    }
    _exit(code);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFSIGNALED(status)) return -WTERMSIG(status);
  return WIFEXITED(status) ? WEXITSTATUS(status) : kInvalid;
#else
  (void)c;
  return kOk;
#endif
}

void Run(const std::string& name, const Case& c) {
  const int r = RunForked(c);
  const char* what = r == kOk ? "valid result" : r == kThrew ? "typed exception" : r == kEmpty ? "EMPTY Brep returned silently"
                     : r == kInvalid ? "INVALID Brep returned silently" : r == kNonFinite ? "NON-FINITE Brep returned silently"
                     : r == kOtherThrow ? "threw a non-std exception" : r == -SIGALRM ? "HUNG (time limit)" : "CRASHED (signal)";
  Check(r == kOk || r == kThrew, (name + ": " + what + (r < 0 ? " " + std::to_string(-r) : std::string())).c_str());
}

// ---- argument tables ------------------------------------------------------------------------------------------------------------------
struct Num { double v; const char* tag; };
const std::vector<Num>& Sizes() {
  static const std::vector<Num> s = {{kNaN, "nan"}, {kInf, "inf"}, {-kInf, "-inf"}, {0.0, "0"}, {-5.0, "-5"}, {1e-12, "1e-12"}, {1e12, "1e12"}};
  return s;
}

NurbsCurve Poly(const std::vector<Point3d>& p, int degree = 1) { return NurbsCurve::FromControlPoints(p, degree); }
NurbsCurve Square(double s) { return Poly({Point3d(0, 0, 0), Point3d(s, 0, 0), Point3d(s, s, 0), Point3d(0, s, 0), Point3d(0, 0, 0)}); }

struct Profile { const char* tag; std::function<NurbsCurve()> make; };
const std::vector<Profile>& Profiles() {
  static const std::vector<Profile> p = {
      {"square10", [] { return Square(10.0); }},
      {"square1e-12", [] { return Square(1e-12); }},
      {"square1e12", [] { return Square(1e12); }},
      {"squareNaN", [] { return Square(kNaN); }},
      {"squareInf", [] { return Square(kInf); }},
      {"coincident", [] { return Poly({Point3d(1, 1, 1), Point3d(1, 1, 1), Point3d(1, 1, 1)}); }},
      {"collinear", [] { return Poly({Point3d(0, 0, 0), Point3d(5, 0, 0), Point3d(10, 0, 0), Point3d(0, 0, 0)}); }},
      {"open2pt", [] { return Poly({Point3d(0, 0, 0), Point3d(10, 0, 0)}); }},
      {"emptyCurve", [] { return NurbsCurve(); }},
  };
  return p;
}

struct Dir { const char* tag; Vector3d v; };
const std::vector<Dir>& Dirs() {
  static const std::vector<Dir> d = {{"zero", Vector3d(0, 0, 0)}, {"nan", Vector3d(kNaN, 0, 1)}, {"inf", Vector3d(0, 0, kInf)},
                                      {"1e-12", Vector3d(0, 0, 1e-12)}, {"1e12", Vector3d(0, 0, 1e12)}, {"z10", Vector3d(0, 0, 10)},
                                      {"inplane", Vector3d(10, 0, 0)}};
  return d;
}

Brep Cube() { return Brep::BoxWelded(0, 0, 0, 10, 10, 10); }
Brep Faceless() { Brep b; b.raw().NewVertex(ON_3dPoint(0, 0, 0)); return b; }

struct Body { const char* tag; std::function<Brep()> make; };
const std::vector<Body>& Bodies() {
  static const std::vector<Body> b = {
      {"cube", [] { return Cube(); }},
      {"empty", [] { return Brep(); }},
      {"faceless", [] { return Faceless(); }},
      {"cube1e-12", [] { return Brep::BoxWelded(0, 0, 0, 1e-12, 1e-12, 1e-12); }},
      {"cube1e12", [] { return Brep::BoxWelded(0, 0, 0, 1e12, 1e12, 1e12); }},
      {"cubeNaN", [] { return Brep::BoxWelded(0, 0, 0, kNaN, 10, 10); }},
      {"flatBox", [] { return Brep::BoxWelded(0, 0, 0, 10, 10, 0); }},
  };
  return b;
}

void Add(const std::string& name, const Case& c) { Run(name, c); }

}  // namespace

D8_EXTRA_TEST(QcDegeneratePrimitives) {
  for (const Num& a : Sizes()) {
    Add(std::string("Brep::Box(0,0,0,") + a.tag + ",10,10)", [a] { return Judge(Brep::Box(0, 0, 0, a.v, 10, 10), false); });
    Add(std::string("Brep::Box(") + a.tag + ",0,0,10,10,10)", [a] { return Judge(Brep::Box(a.v, 0, 0, 10, 10, 10), false); });
    Add(std::string("Brep::Sphere r=") + a.tag, [a] { return Judge(Brep::Sphere(Point3d(0, 0, 0), a.v), false); });
    Add(std::string("Brep::Sphere center=") + a.tag, [a] { return Judge(Brep::Sphere(Point3d(a.v, 0, 0), 5), false); });
    Add(std::string("Brep::Torus R=") + a.tag, [a] { return Judge(Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), a.v, 1), false); });
    Add(std::string("Brep::Torus r=") + a.tag, [a] { return Judge(Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), 5, a.v), false); });
    Add(std::string("Brep::CylinderWelded r=") + a.tag, [a] { return Judge(Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), a.v, 10), false); });
    Add(std::string("Brep::CylinderWelded h=") + a.tag, [a] { return Judge(Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 3, a.v), false); });
  }
  Add("Brep::Torus axis=0", [] { return Judge(Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 0), 5, 1), false); });
  Add("Brep::CylinderWelded axis=0", [] { return Judge(Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 0), 3, 10), false); });
  Add("Brep::CylinderWelded axis=nan", [] { return Judge(Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(kNaN, 0, 1), 3, 10), false); });
}

D8_EXTRA_TEST(QcDegenerateExtrudeRevolve) {
  for (const Profile& p : Profiles()) {
    for (const Dir& d : Dirs()) {
      for (const bool cap : {true, false}) {
        const std::string tag = std::string(p.tag) + " dir=" + d.tag + (cap ? " cap" : " nocap");
        Add("Brep::Extrude " + tag, [p, d, cap] { return Judge(Brep::Extrude(p.make(), d.v, cap)); });
      }
      Add(std::string("Brep::ExtrudeTapered ") + p.tag + " dir=" + d.tag + " draft=0.1", [p, d] { return Judge(Brep::ExtrudeTapered(p.make(), d.v, 0.1, true)); });
    }
    for (const Num& a : Sizes()) {
      Add(std::string("Brep::ExtrudeTapered ") + p.tag + " draft=" + a.tag, [p, a] { return Judge(Brep::ExtrudeTapered(p.make(), Vector3d(0, 0, 10), a.v, true)); });
      Add(std::string("Brep::Revolve ") + p.tag + " angle=" + a.tag, [p, a] { return Judge(Brep::Revolve(p.make(), Point3d(-20, 0, 0), Vector3d(0, 1, 0), a.v, true, 0.0)); });
      Add(std::string("Brep::Revolve ") + p.tag + " start=" + a.tag, [p, a] { return Judge(Brep::Revolve(p.make(), Point3d(-20, 0, 0), Vector3d(0, 1, 0), ON_PI, true, a.v)); });
      Add(std::string("Brep::Pipe ") + p.tag + " r=" + a.tag, [p, a] { return Judge(Brep::Pipe(p.make(), a.v, true, 32, false)); });
    }
    for (const Dir& d : Dirs()) {
      Add(std::string("Brep::Revolve ") + p.tag + " axis=" + d.tag, [p, d] { return Judge(Brep::Revolve(p.make(), Point3d(-20, 0, 0), d.v, 2.0 * ON_PI, true, 0.0)); });
    }
    Add(std::string("Brep::Revolve ") + p.tag + " axis point=nan", [p] { return Judge(Brep::Revolve(p.make(), Point3d(kNaN, 0, 0), Vector3d(0, 1, 0), 2.0 * ON_PI, true, 0.0)); });
    for (const int stations : {-1, 0, 1, 2}) {
      Add(std::string("Brep::Pipe ") + p.tag + " stations=" + std::to_string(stations), [p, stations] { return Judge(Brep::Pipe(p.make(), 1.0, true, stations, false)); });
    }
  }
  for (const Body& b : Bodies()) {
    for (const int face : {-1, 0, 5, 6, 1000}) {
      for (const Dir& d : Dirs()) {
        Add(std::string("Brep::ExtrudeFace ") + b.tag + " face=" + std::to_string(face) + " dir=" + d.tag, [b, face, d] { return Judge(Brep::ExtrudeFace(b.make(), face, d.v, true)); });
      }
    }
    for (const Num& a : Sizes()) {
      Add(std::string("Brep::Thicken ") + b.tag + " t=" + a.tag, [b, a] { return Judge(Brep::Thicken(b.make(), a.v, false)); });
      Add(std::string("Brep::Thicken symmetric ") + b.tag + " t=" + a.tag, [b, a] { return Judge(Brep::Thicken(b.make(), a.v, true)); });
    }
  }
}

D8_EXTRA_TEST(QcDegenerateLoftSweep) {
  const auto Sq = [](double s, double z) { return Poly({Point3d(0, 0, z), Point3d(s, 0, z), Point3d(s, s, z), Point3d(0, s, z), Point3d(0, 0, z)}); };
  struct SecSet { const char* tag; std::function<std::vector<NurbsCurve>()> make; };
  const std::vector<SecSet> sets = {
      {"none", [] { return std::vector<NurbsCurve>(); }},
      {"one", [Sq] { return std::vector<NurbsCurve>{Sq(10, 0)}; }},
      {"twoIdentical", [Sq] { return std::vector<NurbsCurve>{Sq(10, 0), Sq(10, 0)}; }},
      {"twoOk", [Sq] { return std::vector<NurbsCurve>{Sq(10, 0), Sq(10, 10)}; }},
      {"threeOk", [Sq] { return std::vector<NurbsCurve>{Sq(10, 0), Sq(10, 10), Sq(10, 20)}; }},
      {"nanSection", [Sq] { return std::vector<NurbsCurve>{Sq(10, 0), Sq(kNaN, 10)}; }},
      {"infSection", [Sq] { return std::vector<NurbsCurve>{Sq(10, 0), Sq(kInf, 10)}; }},
      {"tiny", [Sq] { return std::vector<NurbsCurve>{Sq(1e-12, 0), Sq(1e-12, 1e-12)}; }},
      {"huge", [Sq] { return std::vector<NurbsCurve>{Sq(1e12, 0), Sq(1e12, 1e12)}; }},
      {"emptyCurveInside", [Sq] { return std::vector<NurbsCurve>{Sq(10, 0), NurbsCurve(), Sq(10, 20)}; }},
      {"coincidentCurves", [] { return std::vector<NurbsCurve>{Poly({Point3d(1, 1, 1), Point3d(1, 1, 1)}), Poly({Point3d(1, 1, 1), Point3d(1, 1, 1)})}; }},
  };
  for (const SecSet& s : sets) {
    for (const int degree : {-1, 0, 1, 3, 100}) {
      for (const bool closed : {false, true}) {
        Add(std::string("Brep::Loft ") + s.tag + " degree=" + std::to_string(degree) + (closed ? " closed" : " open"),
            [s, degree, closed] { return Judge(Brep::Loft(s.make(), degree, closed, true)); });
      }
    }
  }
  for (const Profile& sec : Profiles()) {
    for (const Profile& rail : Profiles()) {
      Add(std::string("Brep::Sweep1 section=") + sec.tag + " rail=" + rail.tag, [sec, rail] { return Judge(Brep::Sweep1(sec.make(), rail.make(), 32, true)); });
      Add(std::string("Brep::Sweep2 section=") + sec.tag + " rails=" + rail.tag, [sec, rail] { return Judge(Brep::Sweep2(sec.make(), rail.make(), rail.make(), 32, true)); });
    }
    for (const int stations : {-1, 0, 1}) {
      Add(std::string("Brep::Sweep1 ") + sec.tag + " stations=" + std::to_string(stations),
          [sec, stations] { return Judge(Brep::Sweep1(sec.make(), Poly({Point3d(0, 0, 0), Point3d(0, 0, 10)}), stations, true)); });
    }
    for (const Num& a : Sizes()) {
      Add(std::string("Brep::Sweep1 ") + sec.tag + " twist=" + a.tag,
          [sec, a] { return Judge(Brep::Sweep1(sec.make(), Poly({Point3d(0, 0, 0), Point3d(0, 0, 10)}), 8, true, a.v, 1.0)); });
      Add(std::string("Brep::Sweep1 ") + sec.tag + " scale_end=" + a.tag,
          [sec, a] { return Judge(Brep::Sweep1(sec.make(), Poly({Point3d(0, 0, 0), Point3d(0, 0, 10)}), 8, true, 0.0, a.v)); });
    }
  }
}

D8_EXTRA_TEST(QcDegenerateShellOffset) {
  for (const Body& b : Bodies()) {
    for (const Num& a : Sizes()) {
      const std::string tag = std::string(b.tag) + " d=" + a.tag;
      Add("ShellConvexPlanar " + tag, [b, a] { return Judge(dino8::kernel::ShellConvexPlanar(b.make(), {}, a.v)); });
      Add("ShellConvexPlanar open face0 " + tag, [b, a] { return Judge(dino8::kernel::ShellConvexPlanar(b.make(), {0}, a.v)); });
      Add("OffsetSolidConvexPlanar " + tag, [b, a] { return Judge(dino8::kernel::OffsetSolidConvexPlanar(b.make(), a.v)); });
      Add("OffsetFace " + tag, [b, a] { return Judge(dino8::kernel::OffsetFace(b.make(), 0, a.v)); });
      Add("OffsetSolidPlanar " + tag, [b, a] {
        Brep out;
        const Result r = dino8::kernel::OffsetSolidPlanar(b.make(), a.v, out);
        return r == Result::Ok ? Judge(out) : static_cast<int>(kOk);  // a refusal (Result::Failed) is the typed answer
      });
      Add("Brep::OffsetPlanarFaceAlongNormal " + tag, [b, a] {
        Brep x = b.make();
        const Result r = x.OffsetPlanarFaceAlongNormal(0, a.v);
        return r == Result::Ok ? Judge(x) : static_cast<int>(kOk);
      });
    }
    for (const std::vector<int>& removed : {std::vector<int>{-1}, std::vector<int>{99}, std::vector<int>{0, 0}, std::vector<int>{0, 1, 2, 3, 4, 5}}) {
      Add(std::string("ShellConvexPlanar ") + b.tag + " removed n=" + std::to_string(removed.size()) + " first=" + std::to_string(removed[0]),
          [b, removed] { return Judge(dino8::kernel::ShellConvexPlanar(b.make(), removed, 1.0)); });
    }
    Add(std::string("OffsetSolidConvexPlanar ") + b.tag + " distances wrong count", [b] { return Judge(dino8::kernel::OffsetSolidConvexPlanar(b.make(), std::vector<double>{1.0})); });
  }
  for (const Num& a : Sizes()) {
    Add(std::string("ShellClosedSphere r=") + a.tag, [a] { return Judge(dino8::kernel::ShellClosedSphere(Point3d(0, 0, 0), a.v, 1.0)); });
    Add(std::string("ShellClosedSphere t=") + a.tag, [a] { return Judge(dino8::kernel::ShellClosedSphere(Point3d(0, 0, 0), 5.0, a.v)); });
    Add(std::string("ShellClosedTorus t=") + a.tag, [a] { return Judge(dino8::kernel::ShellClosedTorus(ON_Plane::World_xy, 10.0, 2.0, a.v)); });
    for (const Profile& p : Profiles()) {
      Add(std::string("ShellClosedExtrusion ") + p.tag + " t=" + a.tag, [p, a] { return Judge(dino8::kernel::ShellClosedExtrusion(p.make(), Vector3d(0, 0, 10), a.v)); });
    }
  }
}

D8_EXTRA_TEST(QcDegenerateBoolean) {
  const std::vector<BooleanOp> ops = {BooleanOp::Union, BooleanOp::Intersection, BooleanOp::Difference};
  for (const Body& a : Bodies()) {
    for (const Body& b : Bodies()) {
      for (const BooleanOp op : ops) {
        const std::string tag = std::string(a.tag) + " x " + b.tag + " op=" + std::to_string(static_cast<int>(op));
        // An empty result is the right answer for an empty operand, for A - A, and for A - (a body that contains A); never otherwise.
        const std::string ta = a.tag, tb = b.tag;
        const bool empty_ok = ta == "empty" || ta == "faceless" || tb == "empty" || tb == "faceless" ||
                              (op == BooleanOp::Difference && (ta == tb || (ta == "cube" && tb == "cube1e12")));
        Add("BooleanCombinePlanar " + tag, [a, b, op, empty_ok] { return Judge(dino8::kernel::BooleanCombinePlanar(a.make(), b.make(), op), true, empty_ok); });
        Add("BooleanCombineMixed " + tag, [a, b, op, empty_ok] { return Judge(dino8::kernel::BooleanCombineMixed(a.make(), b.make(), op), true, empty_ok); });
      }
    }
  }
  for (const Num& t : std::vector<Num>{{kNaN, "nan"}, {kInf, "inf"}, {-kInf, "-inf"}, {0.0, "0"}}) {
    Add(std::string("BooleanCombineMixed tolerance=") + t.tag,
        [t] { return Judge(dino8::kernel::BooleanCombineMixed(Cube(), Brep::BoxWelded(5, 5, 5, 15, 15, 15), BooleanOp::Union, t.v)); });
  }
  Add("BooleanCombineMixed identical operands union", [] { return Judge(dino8::kernel::BooleanCombineMixed(Cube(), Cube(), BooleanOp::Union)); });
    Add("BooleanCombineMixed touching face difference", [] { return Judge(dino8::kernel::BooleanCombineMixed(Cube(), Brep::BoxWelded(10, 0, 0, 20, 10, 10), BooleanOp::Difference)); });
  Add("BooleanCombineMixed 1e12 apart", [] { return Judge(dino8::kernel::BooleanCombineMixed(Cube(), Brep::BoxWelded(1e12, 0, 0, 1e12 + 10, 10, 10), BooleanOp::Union)); });
}

D8_EXTRA_TEST(QcDegenerateFilletChamfer) {
  struct Edge { const char* tag; Point3d a, b; };
  const std::vector<Edge> edges = {
      {"real", Point3d(0, 0, 10), Point3d(10, 0, 10)},
      {"coincident", Point3d(0, 0, 10), Point3d(0, 0, 10)},
      {"nanEnd", Point3d(0, 0, 10), Point3d(kNaN, 0, 10)},
      {"infEnd", Point3d(kInf, 0, 10), Point3d(10, 0, 10)},
      {"notAnEdge", Point3d(1, 1, 1), Point3d(2, 2, 2)},
      {"diagonal", Point3d(0, 0, 0), Point3d(10, 10, 10)},
  };
  for (const Body& b : Bodies()) {
    for (const Edge& e : edges) {
      for (const Num& a : Sizes()) {
        const std::string tag = std::string(b.tag) + " edge=" + e.tag + " d=" + a.tag;
        Add("FilletConvexEdge " + tag, [b, e, a] { return Judge(dino8::kernel::FilletConvexEdge(b.make(), e.a, e.b, a.v)); });
        Add("FilletConcaveEdge " + tag, [b, e, a] { return Judge(dino8::kernel::FilletConcaveEdge(b.make(), e.a, e.b, a.v)); });
        Add("ChamferConvexEdge " + tag, [b, e, a] { return Judge(dino8::kernel::ChamferConvexEdge(b.make(), e.a, e.b, a.v, a.v)); });
        Add("ChamferConcaveEdge " + tag, [b, e, a] { return Judge(dino8::kernel::ChamferConcaveEdge(b.make(), e.a, e.b, a.v, 1.0)); });
        Add("FilletConvexEdgeTapered " + tag, [b, e, a] { return Judge(dino8::kernel::FilletConvexEdgeTapered(b.make(), e.a, e.b, a.v, 1.0)); });
      }
    }
    for (const Num& a : Sizes()) {
      Add(std::string("ChamferConvexVertex ") + b.tag + " d=" + a.tag, [b, a] { return Judge(dino8::kernel::ChamferConvexVertex(b.make(), Point3d(0, 0, 0), a.v)); });
      Add(std::string("FilletConvexEdges empty list ") + b.tag + " r=" + a.tag, [b, a] { return Judge(dino8::kernel::FilletConvexEdges(b.make(), {}, a.v)); });
    }
    Add(std::string("ChamferConvexVertex ") + b.tag + " vertex=nan", [b] { return Judge(dino8::kernel::ChamferConvexVertex(b.make(), Point3d(kNaN, 0, 0), 1.0)); });
    Add(std::string("ChamferConvexVertex ") + b.tag + " not a vertex", [b] { return Judge(dino8::kernel::ChamferConvexVertex(b.make(), Point3d(5, 5, 5), 1.0)); });
  }
  Add("FilletConvexEdgePairMitre same ends", [] { return Judge(dino8::kernel::FilletConvexEdgePairMitre(Cube(), Point3d(0, 0, 0), Point3d(10, 0, 0), Point3d(10, 0, 0), 1.0, 1.0)); });
  Add("ChamferConvexEdgePair same ends", [] { return Judge(dino8::kernel::ChamferConvexEdgePair(Cube(), Point3d(0, 0, 0), Point3d(10, 0, 0), Point3d(10, 0, 0), 1.0, 1.0)); });
}

D8_EXTRA_TEST(QcDegenerateCurveArray) {
  for (const Body& b : Bodies()) {
    for (const Profile& p : Profiles()) {
      for (const int count : {-1, 0, 1, 2, 5}) {
        for (const auto orient : {dino8::kernel::CurveArrayOrientation::None, dino8::kernel::CurveArrayOrientation::Frame}) {
          Add(std::string("ArrayAlongCurve ") + b.tag + " path=" + p.tag + " count=" + std::to_string(count) + (orient == dino8::kernel::CurveArrayOrientation::Frame ? " frame" : " none"),
              [b, p, count, orient] {
                const std::vector<Brep> out = dino8::kernel::ArrayAlongCurve(b.make(), p.make(), count, orient);
                for (const Brep& x : out) {
                  const int j = Judge(x);
                  if (j != kOk) return j;
                }
                return static_cast<int>(kOk);
              });
        }
      }
    }
  }
}
