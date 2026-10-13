// Unit test for SceneObject::EnsureAdaptiveDisplay (doc/SceneObject.h/.cpp),
// the hysteresis-gated half of view-dependent adaptive tessellation (see
// PARITY_MAP.md's "View-dependent adaptive tessellation" item and
// tests/test_adaptive_tessellation.cpp for the other half, the pure
// LodScaleForPixelSize zoom-to-scale-factor mapping). Viewport::DrawObjects
// is the only caller: this test drives EnsureAdaptiveDisplay directly and
// checks Display().built_curve_tolerance/built_surface_tolerance (the
// tolerance that actually produced whatever is cached right now) to prove
// a real re-tessellation happens once the requested tolerance drifts past
// the hysteresis band, and does NOT happen for a trivial drift - the two
// things "no re-tessellation on zoom" and "every frame re-tessellates
// regardless of zoom" would each separately mean this feature isn't real.
#include <cmath>
#include <cstdio>

#include "doc/SceneObject.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "viewport/AdaptiveTessellation.h"
#include "viewport/Camera.h"

using dino8::app::Camera;
using dino8::app::DistanceLodFactor;
using dino8::app::SceneObject;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
bool Near(double a, double b) { return std::fabs(a - b) < 1e-12; }
}  // namespace

int main() {
  // --- Curve: only curve_tolerance drift should matter. ---
  {
    SceneObject o = SceneObject::MakeCurve(NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(10, 0, 0)}, 1));
    Check(o.Display().built_curve_tolerance < 0.0, "a fresh curve's cache starts with no recorded build tolerance");

    o.EnsureAdaptiveDisplay(0.02, 0.05);
    Check(Near(o.Display().built_curve_tolerance, 0.02), "first call builds at exactly the requested tolerance");
    Check(!o.Display().triangles.empty() || !o.Display().lines.empty(), "the curve actually has display geometry now");

    // Well inside the 1.35x hysteresis band (0.021 / 0.02 = 1.05): must NOT
    // force a rebuild - otherwise ordinary per-frame float jitter in the
    // camera would re-tessellate every object every frame.
    o.EnsureAdaptiveDisplay(0.021, 0.0505);
    Check(Near(o.Display().built_curve_tolerance, 0.02),
          "a trivial (1.05x) tolerance drift does not force a re-tessellation");

    // Past the hysteresis band (0.05 / 0.02 = 2.5): a real "Zoom Out" must
    // force a rebuild at the new tolerance.
    o.EnsureAdaptiveDisplay(0.05, 0.0505);
    Check(Near(o.Display().built_curve_tolerance, 0.05),
          "a real (2.5x) tolerance drift forces a re-tessellation at the new tolerance");

    // And it works in the zoomed-in direction too (0.01 / 0.05 = 0.2, past
    // the band on the other side).
    o.EnsureAdaptiveDisplay(0.01, 0.0505);
    Check(Near(o.Display().built_curve_tolerance, 0.01),
          "a real drift toward a finer tolerance also forces a re-tessellation");
  }

  // --- Surface: surface_tolerance drift, independent of curve_tolerance. ---
  {
    SceneObject o = SceneObject::MakeSurface(NurbsSurface::FromControlGrid(
        {Point3d(0, 0, 0), Point3d(10, 0, 0), Point3d(0, 10, 0), Point3d(10, 10, 0)}, 2, 2, 1, 1));
    o.EnsureAdaptiveDisplay(0.02, 0.05);
    Check(Near(o.Display().built_surface_tolerance, 0.05), "a surface's first call builds at the requested surface tolerance");

    // curve_tolerance drifting a lot while surface_tolerance barely moves:
    // only the surface_tolerance side is relevant to a Surface, but
    // EnsureAdaptiveDisplay still must not spuriously refuse to rebuild
    // when the tolerance that DOES matter for this object also drifts.
    o.EnsureAdaptiveDisplay(0.02, 0.2);  // 0.2 / 0.05 = 4x, past the band
    Check(Near(o.Display().built_surface_tolerance, 0.2), "a real surface_tolerance drift re-tessellates a Surface object");

    // SetMeshSurfaceParameters' per-object override pins the effective
    // tolerance regardless of zoom - EnsureAdaptiveDisplay must compare
    // against (and EnsureDisplay must record) the EFFECTIVE tolerance, or
    // a pinned object would spuriously "drift" every time the viewport's
    // zoom-scaled request differs from the pin and re-tessellate for
    // nothing every frame.
    o.custom_mesh_tolerance = 0.03;
    o.InvalidateDisplay();
    o.EnsureAdaptiveDisplay(0.02, 0.2);
    Check(Near(o.Display().built_surface_tolerance, 0.03), "a custom per-object tolerance override wins over the requested one");
    o.EnsureAdaptiveDisplay(0.02, 6.0);  // an extreme zoomed-out request - still pinned
    Check(Near(o.Display().built_surface_tolerance, 0.03),
          "a pinned object does not re-tessellate just because the (irrelevant) requested tolerance moved");
  }

  // --- Per-object distance-based LOD, composed with a real Camera. ---
  // Viewport::DrawObjects' own adaptive_tolerances_for lambda does exactly
  // this: a base tolerance times DistanceLodFactor(object_distance,
  // camera.Distance(), camera.State().perspective), fed straight into
  // EnsureAdaptiveDisplay - reproduced here against a real Camera (not a
  // GL/ImGui one, just the pure navigation/state class) so this is a real
  // integration check of the actual composition the viewport uses, not
  // just DistanceLodFactor in isolation (test_adaptive_tessellation.cpp
  // already covers that function on its own).
  {
    Camera camera;
    camera.State().target = Point3d(0, 0, 0);
    camera.State().eye = Point3d(0, -50, 0);  // target distance = 50
    camera.State().perspective = true;
    const double target_distance = camera.Distance();  // 50

    auto make_surface = [] {
      return SceneObject::MakeSurface(NurbsSurface::FromControlGrid(
          {Point3d(0, 0, 0), Point3d(10, 0, 0), Point3d(0, 10, 0), Point3d(10, 10, 0)}, 2, 2, 1, 1));
    };
    SceneObject near_object = make_surface();   // sits well in front of the target, toward the eye
    SceneObject far_object = make_surface();    // sits well behind the target, away from the eye
    const double base_tolerance = 0.05;

    const double near_distance = (Point3d(0, -40, 0) - camera.State().eye).Length();  // 10: close to the eye
    const double far_distance = (Point3d(0, 150, 0) - camera.State().eye).Length();    // 200: far past the target
    const double near_factor = DistanceLodFactor(near_distance, target_distance, camera.State().perspective);
    const double far_factor = DistanceLodFactor(far_distance, target_distance, camera.State().perspective);
    Check(near_factor < 1.0, "a real Camera: an object nearer than the target gets a sub-1.0 distance factor");
    Check(far_factor > 1.0, "a real Camera: an object farther than the target gets a super-1.0 distance factor");

    near_object.EnsureAdaptiveDisplay(0.02, base_tolerance * near_factor);
    far_object.EnsureAdaptiveDisplay(0.02, base_tolerance * far_factor);
    Check(near_object.Display().built_surface_tolerance < base_tolerance,
          "the near object actually builds at a FINER tolerance than the plain zoom-level value");
    Check(far_object.Display().built_surface_tolerance > base_tolerance,
          "the far object actually builds at a COARSER tolerance than the plain zoom-level value");
    Check(near_object.Display().built_surface_tolerance < far_object.Display().built_surface_tolerance,
          "the near and far objects genuinely end up with two different built tolerances, not one shared value");

    // An object sitting exactly at the target distance is unaffected: it
    // builds at precisely the plain zoom-level tolerance, same as before
    // this feature existed.
    SceneObject target_object = make_surface();
    const double target_object_distance = (Point3d(0, 0, 0) - camera.State().eye).Length();  // 50, == target_distance
    const double target_factor = DistanceLodFactor(target_object_distance, target_distance, camera.State().perspective);
    target_object.EnsureAdaptiveDisplay(0.02, base_tolerance * target_factor);
    Check(Near(target_object.Display().built_surface_tolerance, base_tolerance),
          "an object right at the camera's own target distance builds at exactly the plain zoom-level tolerance");

    // Orthographic: the SAME near/far objects must now build at the
    // IDENTICAL tolerance as each other and as the plain base value - a
    // true parallel projection has no distance falloff to correct for at
    // all, so this feature must not invent one.
    camera.State().perspective = false;
    const double ortho_near_factor = DistanceLodFactor(near_distance, camera.Distance(), camera.State().perspective);
    const double ortho_far_factor = DistanceLodFactor(far_distance, camera.Distance(), camera.State().perspective);
    SceneObject ortho_near = make_surface();
    SceneObject ortho_far = make_surface();
    ortho_near.EnsureAdaptiveDisplay(0.02, base_tolerance * ortho_near_factor);
    ortho_far.EnsureAdaptiveDisplay(0.02, base_tolerance * ortho_far_factor);
    Check(Near(ortho_near.Display().built_surface_tolerance, base_tolerance),
          "orthographic: the near object builds at the plain base tolerance, no distance correction");
    Check(Near(ortho_far.Display().built_surface_tolerance, base_tolerance),
          "orthographic: the far object builds at the plain base tolerance too - identical, not just close");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
