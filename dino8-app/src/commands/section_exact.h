// The exact (B-rep) route shared by every command that cuts an object with a plane: Section/Contour and their
// history rebuild (cmd_curves2.cpp), ClippingSections / ExtractClippingSections / ExtractClippingSlices /
// ClippingDrawings (cmd_viewtools.cpp) and SectionView / UpdateSectionViews (cmd_drafting2.cpp).
//
// A B-rep object (a primitive, a polysurface, a Boolean result) is sectioned by kernel::SectionBrepByPlaneJoined():
// the plane is intersected with every face's own surface and the pieces are joined into one NURBS curve per physical
// contour (a true circle for a cylinder or sphere cut, no chord error). The caller falls back to slicing the render
// mesh only when this returns nothing (a mesh/surface/SubD object, a face type the surface intersector cannot seed,
// a plane that misses the body or only grazes it), so nothing the mesh route used to find is lost.
#pragma once

#include <vector>

#include "commands/cmd_common.h"
#include "dino8/kernel/surface_intersect.h"

namespace dino8::app {

// user_text tag the clipping commands put on every curve they make so the route is visible in the document.
inline constexpr const char* kSectionRouteTag = "SectionRoute";
inline constexpr const char* kSectionRouteExact = "exact B-rep";
inline constexpr const char* kSectionRouteMesh = "mesh slice";

// The exact section curves of `o` with `plane`; empty when `o` is not a B-rep or the exact route finds nothing
// longer than `min_length` (a grazing touch is not a contour).
inline std::vector<kernel::NurbsCurve> ExactBrepSectionCurves(const SceneObject& o, const ON_Plane& plane, double min_length) {
  std::vector<kernel::NurbsCurve> out;
  if (o.kind != ObjectKind::Brep || !o.brep) return out;
  try {
    kernel::IntersectOptions opt;
    opt.tolerance = 1e-5;
    opt.mesh_tolerance = 0.02;
    for (kernel::BrepSectionContour& c : kernel::SectionBrepByPlaneJoined(o.brep->raw(), plane, opt)) {
      if (c.curve.Length() <= min_length) continue;
      out.push_back(std::move(c.curve));
    }
  } catch (const std::exception&) {
    out.clear();
  }
  return out;
}

// Count suffix for the one-line command reports saying which route made the curves.
inline std::string SectionRouteSummary(int exact, int mesh) {
  return " (" + std::to_string(exact) + " from the exact B-rep section, " + std::to_string(mesh) + " from mesh slices)";
}

}  // namespace dino8::app
