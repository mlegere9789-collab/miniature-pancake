// G0 / G1 / G2 continuity options shared by Loft, Patch and NetworkSrf, and the
// adjacent-surface-edge pick they all use.
//
// The three commands keep their old behaviour exactly when no continuity token
// is typed. With `Continuity=Position|Tangency|Curvature` (G0 / G1 / G2), and
// optionally `AngleTol=<degrees>` / `CurvTol=<1/length>`, after the curves are
// selected they ask for a point near an EDGE of an existing surface, hand the
// adjacent surface and that edge to the kernel (NurbsSurface::FitPatch /
// FitCurveNetwork / LoftMatched, surface.h) and print what the kernel MEASURED
// on the finished surface - or the kernel's stated reason for refusing.
#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "commands/cmd_common.h"
#include "dino8/kernel/surface.h"

namespace dino8::app {

struct ContinuityRequest {
  bool requested = false;
  kernel::MatchContinuity continuity = kernel::MatchContinuity::Position;
  double angle_tol = 1e-3;  // radians (the kernel's own default)
  double curv_tol = 1e-2;   // 1 / length (the kernel's own default)
  const char* Name() const {
    return continuity == kernel::MatchContinuity::Curvature ? "G2" : continuity == kernel::MatchContinuity::Tangent ? "G1" : "G0";
  }
};

// Applies one `Continuity=` / `AngleTol=` / `CurvTol=` token (case-insensitive) to `r`. Returns true when the token
// names one of those options (even if its value was refused, in which case `err` says why and `r` is unchanged).
bool ParseContinuityToken(const std::string& token, ContinuityRequest& r, std::string& err);

// An edge of an existing surface: the isoparametric edge (`fixed_direction`: 0 = U fixed, 1 = V fixed; `at_min`: the
// fixed parameter sits at the domain minimum) of the NURBS form of face `face` of object `id`. For a trimmed face
// this is the edge of the UNDERLYING (untrimmed) surface nearest the pick.
struct AdjacentEdge {
  ObjectId id = kNoObject;
  int face = 0;
  kernel::NurbsSurface surface;
  int fixed_direction = 0;
  bool at_min = true;
  double pick_distance = 0.0;  // from the pick point to the chosen edge
  std::string Describe() const {
    return "object " + std::to_string(id) + (face ? " face " + std::to_string(face) : std::string()) + ", " +
           (fixed_direction == 0 ? "U" : "V") + (at_min ? "=min" : "=max") + " edge";
  }
};

// The surface edge nearest `p` among the visible, unlocked Surface / Brep objects not in `exclude`. nullopt, with
// `why`, if there is none.
std::optional<AdjacentEdge> PickAdjacentEdge(CommandContext& ctx, Point3d p, const std::vector<ObjectId>& exclude, std::string& why);

// "gap 1.2e-07, tangent angle 3.4e-05 rad (0.0019 deg), cross-edge curvature difference 1.1e-04, Gaussian difference 2.2e-05"
std::string FormatEdgeMeasure(const kernel::NurbsSurface::EdgeContinuityMeasure& m);

// "%.3g" - a measured value, short but not rounded to a lie.
std::string Sig3(double v);

inline const char* ContinuityWhat(kernel::MatchContinuity c) {
  return c == kernel::MatchContinuity::Position ? "position" : c == kernel::MatchContinuity::Tangent ? "tangent plane" : "tangent plane and curvature";
}

inline std::string ContinuityMeasuredNote(const ContinuityRequest& req, const kernel::NurbsSurface::EdgeContinuityMeasure& m) {
  std::string s = "MEASURED on the finished surface at " + std::to_string(m.samples) + " probes along the edge: " + FormatEdgeMeasure(m) +
                  "; requested " + req.Name();
  if (req.continuity == kernel::MatchContinuity::Position) {
    s += " (position only: the tangent angle and curvature above are reported, not constrained)";
  } else {
    s += ": angle tolerance " + Sig3(req.angle_tol) + " rad";
    if (req.continuity == kernel::MatchContinuity::Curvature) s += ", curvature tolerance " + Sig3(req.curv_tol);
  }
  return s;
}

// History On: a continuity result is NOT recorded. A rebuild would have to hold the adjacent surface (face and edge) as an
// input, which the rebuild registry (history_rebuild.h) does not; so the record is refused, with this said once.
inline void NoHistoryNote(CommandContext& ctx, const char* command, const ContinuityRequest& req) {
  if (!ctx.App().State().history_recording) return;
  ctx.Print(std::string(command) + ": no history recorded - a " + req.Name() +
            " rebuild would need the adjacent surface edge as a recorded input, which the history rebuild registry does not hold; "
            "run it without Continuity= to record it");
}

// Select curves -> (optionally) click an adjacent surface edge -> build. Without a continuity token the selection goes
// straight to `plain`, as the plain SelectThenActCommand it replaces did; `extra` carries every typed token that is not a
// continuity option (Loft's Style= / Cap=), unparsed.
class ContinuityPickCommand : public Command {
 public:
  using Plain = std::function<void(CommandContext&, const std::vector<ObjectId>&, const std::vector<std::string>& extra)>;
  using Matched = std::function<void(CommandContext&, const std::vector<ObjectId>&, const ContinuityRequest&, const AdjacentEdge&,
                                     const std::vector<std::string>& extra)>;
  ContinuityPickCommand(std::string name, std::string prompt, int min_count, Plain plain, Matched matched)
      : name_(std::move(name)), prompt_(std::move(prompt)), min_(min_count), plain_(std::move(plain)), matched_(std::move(matched)) {}

  void Begin(CommandContext&) override {
    WantObjects(prompt_, min_);
    options = {{"Continuity", "None", {"None", "Position", "Tangency", "Curvature"}, false, false},
               {"AngleTol", Sig3(req_.angle_tol * 180.0 / 3.14159265358979323846), {}, true, false},
               {"CurvTol", Sig3(req_.curv_tol), {}, true, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    std::string err;
    ParseContinuityToken(n + "=" + v, req_, err);
    if (!err.empty()) { ctx.Warn(name_ + ": " + err); return; }
    options[0].value = !req_.requested ? "None" : req_.continuity == kernel::MatchContinuity::Curvature ? "Curvature"
                       : req_.continuity == kernel::MatchContinuity::Tangent ? "Tangency" : "Position";
    options[1].value = Sig3(req_.angle_tol * 180.0 / 3.14159265358979323846);
    options[2].value = Sig3(req_.curv_tol);
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    while (std::optional<std::string> t = ctx.Engine().TakePendingInput()) {
      std::string err;
      if (ParseContinuityToken(*t, req_, err)) {
        if (!err.empty()) ctx.Warn(name_ + ": " + err);
      } else {
        extra_.push_back(*t);
      }
    }
    if (!req_.requested) {
      plain_(ctx, ids, extra_);
      Finish();
      return;
    }
    ids_ = ids;
    WantPoint("Click the adjacent surface edge to continue (" + std::string(req_.Name()) + ")");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (ids_.empty()) return;
    std::string why;
    std::optional<AdjacentEdge> edge = PickAdjacentEdge(ctx, p, ids_, why);
    if (!edge) { ctx.Warn(name_ + ": " + why); return; }
    matched_(ctx, ids_, req_, *edge, extra_);
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    if (want == Want::Point) {
      ctx.Warn(name_ + ": no adjacent surface edge was picked, so nothing was built (" + std::string(req_.Name()) + " was requested)");
      Finish();
    }
  }

 private:
  std::string name_, prompt_;
  int min_;
  Plain plain_;
  Matched matched_;
  ContinuityRequest req_;
  std::vector<ObjectId> ids_;
  std::vector<std::string> extra_;
};

}  // namespace dino8::app
