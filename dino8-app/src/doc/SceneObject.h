// A single object in a Dino 8 document: one piece of kernel geometry plus
// the attributes every CAD object carries (name, layer, color, visibility,
// lock state, group, user text) and a lazily-rebuilt display cache the
// viewports draw from.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/types.h"

namespace dino8::app {

using ObjectId = std::uint64_t;
constexpr ObjectId kNoObject = 0;

enum class ObjectKind { Point, Curve, Surface, Brep, Mesh, SubD };

const char* ObjectKindName(ObjectKind kind);

struct Color {
  float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
  static Color FromBytes(int r, int g, int b) {
    return Color{r / 255.f, g / 255.f, b / 255.f, 1.f};
  }
};

// Surface-analysis display (Rhino's Zebra / EMap / CurvatureAnalysis /
// DraftAngleAnalysis). Zebra and EMap are pure shader effects; Curvature
// and DraftAngle colour the display mesh per vertex (DisplayCache::colors).
enum class AnalysisMode { None, Zebra, EMap, Curvature, DraftAngle };
enum class ZebraDirection { Horizontal, Vertical };
enum class CurvatureStyle { Gaussian, Mean };

const char* AnalysisModeName(AnalysisMode mode);

struct AnalysisSettings {
  AnalysisMode mode = AnalysisMode::None;
  // Zebra
  ZebraDirection zebra_direction = ZebraDirection::Horizontal;
  float zebra_density = 4.0f;  // stripe frequency over the reflection vector (2*density stripes)
  // CurvatureAnalysis
  CurvatureStyle curvature_style = CurvatureStyle::Gaussian;
  bool auto_range = true;
  double range_min = -1.0, range_max = 1.0;  // used when !auto_range
  // DraftAngleAnalysis: draft angle = 90 deg - angle(normal, pull direction),
  // so a vertical wall is 0, a cap facing the pull is +90, an undercut < 0.
  kernel::Vector3d draft_direction{0, 0, 1};
  double draft_min = -5.0, draft_max = 5.0;  // degrees, blue .. red

  bool SameColoring(const AnalysisSettings& o) const {
    return mode == o.mode && curvature_style == o.curvature_style && auto_range == o.auto_range &&
           range_min == o.range_min && range_max == o.range_max && draft_direction == o.draft_direction &&
           draft_min == o.draft_min && draft_max == o.draft_max;
  }
};

// Tessellated / sampled geometry ready for the renderer. Positions are in
// world space; triangles carry per-vertex normals for shading.
struct DisplayCache {
  std::vector<float> triangles;  // x,y,z,nx,ny,nz per vertex, 3 vertices per tri
  std::vector<float> lines;      // x,y,z per vertex, 2 vertices per segment
  std::vector<float> points;     // x,y,z per point
  std::vector<float> control_polygon;  // x,y,z per vertex, 2 per segment (when shown)
  std::vector<float> control_points;   // x,y,z per control point (when shown)
  // Brep / surface / mesh edges for ShowEdges: all edges, and the naked
  // ones (brep edges with a single trim, mesh edges with a single face).
  std::vector<float> edges;        // x,y,z pairs
  std::vector<float> naked_edges;  // x,y,z pairs
  // Per-vertex analysis colour (r,g,b per triangle vertex), empty when no
  // colour-based analysis is active. Rebuilt lazily by EnsureAnalysisColors.
  std::vector<float> colors;
  AnalysisSettings colors_settings;  // settings the colours were computed with
  bool colors_valid = false;
  // Value range the colours were normalised over (for the command report).
  double colors_min = 0, colors_max = 0;
  kernel::BoundingBox bbox{};
  bool has_bbox = false;
  bool dirty = true;
};

class SceneObject {
 public:
  SceneObject();
  ~SceneObject();
  SceneObject(const SceneObject& other);
  SceneObject& operator=(const SceneObject& other);
  SceneObject(SceneObject&&) noexcept;
  SceneObject& operator=(SceneObject&&) noexcept;

  // Construction helpers (each produces an object of the matching kind).
  static SceneObject MakePoint(kernel::Point3d point);
  static SceneObject MakeCurve(const kernel::NurbsCurve& curve);
  static SceneObject MakeSurface(const kernel::NurbsSurface& surface);
  static SceneObject MakeBrep(const kernel::Brep& brep);
  static SceneObject MakeMesh(const kernel::Mesh& mesh);
  static SceneObject MakeSubD(const kernel::SubD& subd);

  ObjectId id = kNoObject;
  std::string name;
  int layer_index = 0;
  ObjectKind kind = ObjectKind::Point;
  Color color;
  bool color_by_layer = true;
  bool visible = true;
  bool locked = false;
  bool selected = false;
  bool show_control_points = false;
  bool highlight_edges = false;  // ShowEdges: draw brep/mesh edges thick, naked edges in a second colour
  AnalysisSettings analysis;     // per-object surface analysis (None = use the app-wide fallback)
  int group_id = -1;
  std::string material_name;
  std::map<std::string, std::string> user_text;

  // Exactly one of these is non-null / meaningful, matching `kind`.
  kernel::Point3d point{0, 0, 0};
  std::unique_ptr<kernel::NurbsCurve> curve;
  std::unique_ptr<kernel::NurbsSurface> surface;
  std::unique_ptr<kernel::Brep> brep;
  std::unique_ptr<kernel::Mesh> mesh;
  std::unique_ptr<kernel::SubD> subd;

  // Applies a transform to the geometry in place and invalidates display.
  void Transform(const ON_Xform& xform);

  // World-space bounding box (computed from the display cache).
  kernel::BoundingBox BoundingBox() const;

  // Rebuilds the display cache if dirty. `curve_tolerance` and
  // `surface_tolerance` are chord tolerances for curve sampling and
  // surface tessellation, in model units.
  void EnsureDisplay(double curve_tolerance, double surface_tolerance) const;
  void InvalidateDisplay() { cache_.dirty = true; cache_.colors_valid = false; }
  const DisplayCache& Display() const { return cache_; }

  // Fills Display().colors for a Curvature / DraftAngle analysis (no-op for
  // other modes: colors is cleared). Recomputes only when `settings` differ
  // from the last computation or the display mesh was rebuilt. Call after
  // EnsureDisplay().
  void EnsureAnalysisColors(const AnalysisSettings& settings) const;

  // Human-readable summary used by What/List/Properties.
  std::string Describe() const;

 private:
  mutable DisplayCache cache_;
  void CopyFrom(const SceneObject& other);
};

}  // namespace dino8::app
