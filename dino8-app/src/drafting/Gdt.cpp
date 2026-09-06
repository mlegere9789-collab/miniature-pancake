#include "drafting/Gdt.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace dino8::app::drafting {

namespace {

kernel::NurbsCurve Poly(const std::vector<kernel::Point3d>& pts) {
  ON_Polyline pl;
  for (const kernel::Point3d& p : pts) pl.Append(p);
  ON_PolylineCurve pc(pl);
  ON_NurbsCurve nc;
  pc.GetNurbForm(nc);
  kernel::NurbsCurve out;
  out.raw() = nc;
  return out;
}

kernel::NurbsCurve Arc(kernel::Point3d center, double radius, double a0, double a1, const ON_Plane& pl, int n = 24) {
  ON_Plane local(center, pl.xaxis, pl.yaxis);
  ON_Arc arc(local, radius, (a1 - a0) * ON_PI / 180.0);
  arc.Rotate(a0 * ON_PI / 180.0, pl.zaxis, center);
  ON_ArcCurve ac(arc);
  ON_NurbsCurve nc;
  ac.GetNurbForm(nc);
  kernel::NurbsCurve out;
  out.raw() = nc;
  return out;
}

// Maps a unit-square (u, v) in [0,1] to world space on `plane`, `size` tall.
struct Frame {
  kernel::Point3d origin;
  const ON_Plane& plane;
  double size;
  kernel::Point3d At(double u, double v) const { return origin + plane.xaxis * (u * size) + plane.yaxis * (v * size); }
};

}  // namespace

const char* GdtSymbolName(GdtSymbol s) {
  switch (s) {
    case GdtSymbol::Flatness: return "Flatness";
    case GdtSymbol::Straightness: return "Straightness";
    case GdtSymbol::Circularity: return "Circularity";
    case GdtSymbol::Cylindricity: return "Cylindricity";
    case GdtSymbol::Profile: return "ProfileLine";
    case GdtSymbol::ProfileSurface: return "ProfileSurface";
    case GdtSymbol::Perpendicularity: return "Perpendicularity";
    case GdtSymbol::Angularity: return "Angularity";
    case GdtSymbol::Parallelism: return "Parallelism";
    case GdtSymbol::Position: return "Position";
    case GdtSymbol::Concentricity: return "Concentricity";
    case GdtSymbol::Symmetry: return "Symmetry";
    case GdtSymbol::Runout: return "Runout";
    case GdtSymbol::TotalRunout: return "TotalRunout";
  }
  return "Flatness";
}

std::vector<std::string> GdtSymbolNames() {
  return {"Flatness", "Straightness", "Circularity", "Cylindricity", "ProfileLine", "ProfileSurface",
          "Perpendicularity", "Angularity", "Parallelism", "Position", "Concentricity", "Symmetry", "Runout", "TotalRunout"};
}

bool ParseGdtSymbol(const std::string& text, GdtSymbol& out) {
  std::string t = text;
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const std::map<std::string, GdtSymbol> kMap = {
      {"flatness", GdtSymbol::Flatness}, {"straightness", GdtSymbol::Straightness},
      {"circularity", GdtSymbol::Circularity}, {"roundness", GdtSymbol::Circularity},
      {"cylindricity", GdtSymbol::Cylindricity},
      {"profile", GdtSymbol::Profile}, {"profileline", GdtSymbol::Profile}, {"profileofaline", GdtSymbol::Profile},
      {"profilesurface", GdtSymbol::ProfileSurface}, {"profileofasurface", GdtSymbol::ProfileSurface},
      {"perpendicularity", GdtSymbol::Perpendicularity}, {"angularity", GdtSymbol::Angularity},
      {"parallelism", GdtSymbol::Parallelism}, {"position", GdtSymbol::Position},
      {"concentricity", GdtSymbol::Concentricity}, {"coaxiality", GdtSymbol::Concentricity},
      {"symmetry", GdtSymbol::Symmetry}, {"runout", GdtSymbol::Runout}, {"circularrunout", GdtSymbol::Runout},
      {"totalrunout", GdtSymbol::TotalRunout},
  };
  auto it = kMap.find(t);
  if (it == kMap.end()) return false;
  out = it->second;
  return true;
}

void AppendGdtGlyph(GdtSymbol symbol, kernel::Point3d origin, const ON_Plane& plane, double size, std::vector<kernel::NurbsCurve>& out) {
  const Frame f{origin, plane, size};
  switch (symbol) {
    case GdtSymbol::Flatness: {
      // Parallelogram.
      out.push_back(Poly({f.At(0, 0), f.At(0.85, 0), f.At(1, 1), f.At(0.15, 1), f.At(0, 0)}));
      break;
    }
    case GdtSymbol::Straightness: {
      out.push_back(Poly({f.At(0, 0.5), f.At(1, 0.5)}));
      break;
    }
    case GdtSymbol::Circularity: {
      out.push_back(Arc(f.At(0.5, 0.5), size * 0.42, 0, 360, plane));
      break;
    }
    case GdtSymbol::Cylindricity: {
      out.push_back(Arc(f.At(0.5, 0.5), size * 0.42, 0, 360, plane));
      const kernel::Vector3d d = plane.xaxis * (size * 0.30) - plane.yaxis * (size * 0.30);
      out.push_back(Poly({f.At(0.5, 0.5) - d, f.At(0.5, 0.5) + d}));
      out.push_back(Poly({f.At(0.5 - 0.30, 0.5 + 0.30), f.At(0.5 + 0.30, 0.5 - 0.30)}));
      break;
    }
    case GdtSymbol::Profile: {
      // Half-circle arc capping a straight base (profile of a line).
      out.push_back(Poly({f.At(0.02, 0), f.At(0.98, 0)}));
      out.push_back(Arc(f.At(0.5, 0), size * 0.48, 0, 180, plane));
      break;
    }
    case GdtSymbol::ProfileSurface: {
      out.push_back(Poly({f.At(0.02, 0), f.At(0.98, 0)}));
      out.push_back(Arc(f.At(0.5, 0), size * 0.48, 0, 180, plane));
      out.push_back(Poly({f.At(0.02, 0), f.At(0.02, 0.18)}));
      out.push_back(Poly({f.At(0.98, 0), f.At(0.98, 0.18)}));
      break;
    }
    case GdtSymbol::Perpendicularity: {
      out.push_back(Poly({f.At(0.15, 0), f.At(0.15, 1)}));
      out.push_back(Poly({f.At(0.0, 0.85), f.At(1.0, 0.85)}));
      break;
    }
    case GdtSymbol::Angularity: {
      out.push_back(Poly({f.At(0, 0), f.At(1, 0)}));
      out.push_back(Poly({f.At(0.05, 0), f.At(0.55, 1)}));
      break;
    }
    case GdtSymbol::Parallelism: {
      out.push_back(Poly({f.At(0.15, 0), f.At(0.65, 1)}));
      out.push_back(Poly({f.At(0.45, 0), f.At(0.95, 1)}));
      break;
    }
    case GdtSymbol::Position: {
      out.push_back(Arc(f.At(0.5, 0.5), size * 0.45, 0, 360, plane));
      out.push_back(Poly({f.At(0.05, 0.5), f.At(0.95, 0.5)}));
      out.push_back(Poly({f.At(0.5, 0.05), f.At(0.5, 0.95)}));
      break;
    }
    case GdtSymbol::Concentricity: {
      out.push_back(Arc(f.At(0.5, 0.5), size * 0.45, 0, 360, plane));
      out.push_back(Arc(f.At(0.5, 0.5), size * 0.20, 0, 360, plane));
      break;
    }
    case GdtSymbol::Symmetry: {
      out.push_back(Poly({f.At(0, 0.35), f.At(1, 0.35)}));
      out.push_back(Poly({f.At(0, 0.65), f.At(1, 0.65)}));
      break;
    }
    case GdtSymbol::Runout:
    case GdtSymbol::TotalRunout: {
      // Single arrow (runout) touching a diagonal reference line; a second
      // parallel arrow for total runout.
      auto arrow = [&](double ov) {
        const kernel::Point3d tip = f.At(0.15, ov), tail = f.At(0.95, ov + 0.7);
        out.push_back(Poly({tail, tip}));
        const kernel::Vector3d dir = (tip - tail);
        kernel::Vector3d d = dir; d.Unitize();
        const kernel::Vector3d side = ON_CrossProduct(plane.zaxis, d) * (size * 0.12);
        out.push_back(Poly({tip + d * (size * 0.22) + side, tip, tip + d * (size * 0.22) - side, tip + d * (size * 0.22) + side}));
      };
      arrow(0.0);
      if (symbol == GdtSymbol::TotalRunout) arrow(0.35);
      break;
    }
  }
}

void AppendModifierCircle(kernel::Point3d origin, const ON_Plane& plane, double size, std::vector<kernel::NurbsCurve>& out) {
  out.push_back(Arc(origin, size * 0.55, 0, 360, plane));
}

bool ParseWeldSymbolType(const std::string& text, WeldSymbolType& out) {
  std::string t = text;
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t == "fillet") { out = WeldSymbolType::Fillet; return true; }
  if (t == "groove" || t == "squaregroove" || t == "square") { out = WeldSymbolType::Groove; return true; }
  if (t == "spot" || t == "plug") { out = WeldSymbolType::Spot; return true; }
  return false;
}

void AppendWeldGlyph(kernel::Point3d origin, const ON_Plane& plane, double size, bool above, WeldSymbolType type,
                     std::vector<kernel::NurbsCurve>& out) {
  const double s = above ? 1.0 : -1.0;
  switch (type) {
    case WeldSymbolType::Fillet:
      // AWS A2.4 fillet weld: a right triangle, vertical leg on the reference line.
      out.push_back(Poly({origin, origin + plane.xaxis * size, origin + plane.yaxis * (size * s), origin}));
      break;
    case WeldSymbolType::Groove: {
      // Square-groove weld: two parallel bars square to the reference line.
      const kernel::Point3d a = origin, b = origin + plane.yaxis * (size * s);
      out.push_back(Poly({a, b}));
      const kernel::Point3d a2 = origin + plane.xaxis * (size * 0.3), b2 = a2 + plane.yaxis * (size * s);
      out.push_back(Poly({a2, b2}));
      break;
    }
    case WeldSymbolType::Spot:
      // Spot/plug weld: a circle straddling the reference line.
      out.push_back(Arc(origin + plane.yaxis * (size * 0.5 * s), size * 0.5, 0, 360, plane));
      break;
  }
}

void AppendSurfaceFinishGlyph(kernel::Point3d origin, const ON_Plane& plane, double size, std::vector<kernel::NurbsCurve>& out) {
  out.push_back(Poly({origin + plane.xaxis * (-size * 0.5) + plane.yaxis * (size * 0.6),
                      origin,
                      origin + plane.xaxis * size + plane.yaxis * size}));
}

void AppendDatumTriangle(kernel::Point3d origin, const ON_Plane& plane, double size, std::vector<kernel::NurbsCurve>& out) {
  const kernel::Point3d apex = origin;
  const kernel::Point3d base_l = origin + plane.yaxis * size - plane.xaxis * (size * 0.4);
  const kernel::Point3d base_r = origin + plane.yaxis * size + plane.xaxis * (size * 0.4);
  out.push_back(Poly({apex, base_l, base_r, apex}));
  out.push_back(Poly({origin + plane.yaxis * size, origin + plane.yaxis * (size * 1.9)}));
  const kernel::Point3d box_bl = origin + plane.yaxis * (size * 1.9) - plane.xaxis * (size * 0.6);
  out.push_back(Poly({box_bl, box_bl + plane.xaxis * (size * 1.2), box_bl + plane.xaxis * (size * 1.2) + plane.yaxis * (size * 1.2),
                      box_bl + plane.yaxis * (size * 1.2), box_bl}));
}

}  // namespace dino8::app::drafting
