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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "doc/SceneObject.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "viewport/AdaptiveTessellation.h"
#include "viewport/Camera.h"

using dino8::app::Camera;
using dino8::app::DistanceLodFactor;
using dino8::app::SceneObject;
using dino8::app::LodScaleForPixelSize;
using dino8::app::ScreenSpaceErrorPixels;
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


  // --- Multi-level cache: zooming back to a level already tessellated swaps it in, no re-tessellation. ---
  // The same construction the Sphere command uses (cmd_solids.cpp AddBrep(ON_BrepSphere(...))).
  auto sphere_of = [](double radius) {
    dino8::kernel::Brep k;
    ON_Brep* b = ON_BrepSphere(ON_Sphere(ON_3dPoint(0, 0, 0), radius));
    k.raw() = *b;
    delete b;
    return SceneObject::MakeBrep(k);
  };
  auto sphere_object = [&] { return sphere_of(10.0); };
  auto tris = [](const SceneObject& o) { return o.Display().triangles.size() / 18; };
  {
    SceneObject o = sphere_object();
    SceneObject::ResetLodCounters();
    o.EnsureAdaptiveDisplay(0.02, 0.05, 0.05);                       // level A (base)
    const std::size_t tris_a = tris(o);
    o.EnsureAdaptiveDisplay(0.002, 0.005, 0.05);                     // 10x finer: level B
    const std::size_t tris_b = tris(o);
    Check(tris_b > 2 * tris_a, "10x finer tolerance builds a much denser mesh (measured triangle counts differ >2x)");
    std::printf("     sphere r=10: %zu triangles at surface tol 0.05, %zu at 0.005\n", tris_a, tris_b);
    SceneObject::AdaptiveLodCounters c0 = SceneObject::LodCounters();
    Check(c0.rebuilds == 1, "going from level A to B re-tessellated exactly once");
    o.EnsureAdaptiveDisplay(0.02, 0.05, 0.05);                       // back to A
    Check(tris(o) == tris_a && Near(o.Display().built_surface_tolerance, 0.05), "zooming back to level A restores A's exact mesh");
    Check(SceneObject::LodCounters().rebuilds == c0.rebuilds, "...without re-tessellating (rebuild counter unchanged)");
    Check(SceneObject::LodCounters().stash_hits == c0.stash_hits + 1, "...it came from the stash (stash hit counter +1)");
    Check(o.LodInfo().stashed_levels == 1, "level B is now the stashed one");
    // Two viewports at different zoom alternating on one object: after warm-up, zero tessellation.
    SceneObject::ResetLodCounters();
    for (int i = 0; i < 20; ++i) {
      o.EnsureAdaptiveDisplay(0.002, 0.005, 0.05);
      o.EnsureAdaptiveDisplay(0.02, 0.05, 0.05);
    }
    c0 = SceneObject::LodCounters();
    Check(c0.rebuilds == 0 && c0.stash_hits == 40, "20 alternations between two viewports' zoom levels: 0 re-tessellations, 40 stash swaps");
    // Geometry change invalidates every stashed level.
    o.InvalidateDisplay();
    Check(o.LodInfo().stashed_levels == 0, "InvalidateDisplay drops the stashed levels (they describe the old geometry)");
    o.EnsureAdaptiveDisplay(0.002, 0.005, 0.05);
    o.EnsureAdaptiveDisplay(0.02, 0.05, 0.05);
    Check(SceneObject::LodCounters().rebuilds == 1, "after invalidation the old levels are NOT reused: the first return to level A rebuilds");
    // Capacity: more distinct levels than slots evicts the least recently used.
    SceneObject p = sphere_object();
    for (double t : {0.05, 0.01, 0.002, 0.0004, 0.00008}) p.EnsureAdaptiveDisplay(t * 0.4, t, 0.0);
    Check(p.LodInfo().stashed_levels <= SceneObject::kLodStashCapacity, "the stash never holds more than kLodStashCapacity levels");
  }

  // --- Screen-space error, MEASURED against the exact sphere. ---
  // For every display triangle, the worst chord sagitta (radius minus the
  // distance of an edge midpoint from the centre) in model units. The
  // viewport asks for tolerance = base * LodScaleForPixelSize(pixel); the
  // error that leaves on screen is sagitta / pixel_size.
  {
    auto worst_sagitta = [](const SceneObject& o) {
      const std::vector<float>& t = o.Display().triangles;
      double worst = 0.0;
      for (std::size_t v = 0; v + 17 < t.size(); v += 18) {
        for (int e = 0; e < 3; ++e) {
          const float* a = &t[v + 6 * e];
          const float* b = &t[v + 6 * ((e + 1) % 3)];
          const double mx = (a[0] + b[0]) / 2.0, my = (a[1] + b[1]) / 2.0, mz = (a[2] + b[2]) / 2.0;
          worst = std::max(worst, 10.0 - std::sqrt(mx * mx + my * my + mz * mz));
        }
      }
      return worst;
    };
    const double base = 0.05;
    struct Row { double pixel; };
    std::printf("     zoom (units/px) -> tolerance, triangles, measured worst sagitta, on-screen error (px):\n");
    for (double pixel : {0.1, 0.02, 0.005, 0.001}) {
      SceneObject o = sphere_object();
      const double tol = base * LodScaleForPixelSize(pixel);
      o.EnsureAdaptiveDisplay(0.02, tol, base);
      const double sag = worst_sagitta(o);
      std::printf("     %8.4f -> tol %.5f  %7zu tris  sagitta %.5f  %.2f px\n", pixel, tol, tris(o), sag, sag / pixel);
      Check(sag <= 2.5 * o.Display().built_surface_tolerance,
            "measured chord sagitta of the built mesh stays within 2.5x of the tolerance it was built for (the mesher's own constant is ~2.1x)");
      Check(sag / pixel <= 2.5, "MEASURED on-screen chord error stays under 2.5 px from the reference zoom down to 100x deeper");
    }
    {
      // What the OLD clamp (scale floor 0.2, default 12000-triangle face cap) did at the deepest zoom, measured the same way.
      SceneObject old = sphere_object();
      old.EnsureDisplay(0.02, 0.05 * 0.2);
      const double sag_old = worst_sagitta(old);
      std::printf("     OLD clamp at 0.001 units/px: tol 0.01000 %7zu tris  sagitta %.5f  %.2f px\n", tris(old), sag_old, sag_old / 0.001);
      SceneObject now = sphere_object();
      now.EnsureAdaptiveDisplay(0.02, 0.05 * LodScaleForPixelSize(0.001), 0.05);
      Check(worst_sagitta(now) < 0.25 * sag_old, "at the deepest zoom the new floor cuts the measured chord error to under a quarter of the old clamp's");
    }
    Check(LodScaleForPixelSize(0.001) < 0.2, "deep-zoom scale now goes below the old 0.2 floor");
    Check(std::fabs(ScreenSpaceErrorPixels(0.05, 0.1) - 0.5) < 1e-12, "reference zoom leaves a 0.5 px error (0.05 / 0.1)");
  }

  // --- Triangle budget: a refinement that saturates the budget records a floor and stops rebuilding. ---
  {
    // r=200: already at tolerance 0.01 the mesh wants far more than the budget.
    SceneObject o = sphere_of(200.0);
    SceneObject::ResetLodCounters();
    o.EnsureAdaptiveDisplay(0.02, 0.0002, 0.05);  // absurdly fine for a r=200 sphere
    const std::size_t n1 = tris(o);
    std::printf("     budgeted: %zu triangles (budget %zu), floor %g\n", n1, SceneObject::kAdaptiveTriangleBudget, o.LodInfo().surface_floor);
    Check(n1 <= SceneObject::kAdaptiveTriangleBudget, "the refined mesh never exceeds the triangle budget");
    Check(n1 * 10 >= SceneObject::kAdaptiveTriangleBudget * 9, "...it saturates it (>= 90%) - the mesher's per-face cap was raised to the budget");
    Check(SceneObject::LodCounters().budget_clamps == 1 && Near(o.LodInfo().surface_floor, 0.0002), "the saturating request is recorded as the budget floor (1 clamp)");
    const unsigned long long rebuilds_before = SceneObject::LodCounters().rebuilds;
    o.EnsureAdaptiveDisplay(0.02, 0.0001, 0.05);  // zoom in further: finer than the floor
    o.EnsureAdaptiveDisplay(0.02, 0.00004, 0.05);
    Check(SceneObject::LodCounters().rebuilds == rebuilds_before && Near(o.Display().built_surface_tolerance, 0.0002),
          "requests finer than the floor are raised to it: no further re-tessellation of the same capped mesh");
    SceneObject u = sphere_of(200.0);
    u.custom_mesh_tolerance = 0.05;  // SetMeshSurfaceParameters pin: the user's own choice, never budget-limited
    u.EnsureAdaptiveDisplay(0.02, 0.0002, 0.05);
    Check(u.LodInfo().surface_floor == 0.0 && Near(u.Display().built_surface_tolerance, 0.05), "a pinned (custom tolerance) object is never budget-limited");
    // Without a base tolerance (the Render/RenderView path) the budget machinery is off.
    SceneObject r = sphere_of(10.0);
    r.EnsureAdaptiveDisplay(0.02, 0.0005, 0.0);
    Check(r.LodInfo().surface_floor == 0.0, "no base tolerance given: no budget floor");
  }

  // --- Toggle off: fixed tolerance, exact. ---
  {
    SceneObject o = sphere_object();
    o.EnsureAdaptiveDisplay(0.02, 0.05, 0.05, true);
    o.EnsureAdaptiveDisplay(0.021, 0.0505, 0.05, true);
    Check(Near(o.Display().built_surface_tolerance, 0.05), "adaptive on: a 1% change stays inside the hysteresis band");
    o.EnsureAdaptiveDisplay(0.02, 0.05, 0.0, false);
    Check(Near(o.Display().built_surface_tolerance, 0.05) && Near(o.Display().built_curve_tolerance, 0.02),
          "adaptive off: the mesh is at exactly the Options tolerances");
    o.EnsureAdaptiveDisplay(0.02, 0.0505, 0.0, false);
    Check(Near(o.Display().built_surface_tolerance, 0.0505), "adaptive off: no hysteresis - even a 1% tolerance change is honoured exactly");
  }

  // --- Per-object display-mode override: the name API is the one door to all ten flags. ---
  {
    SceneObject o = SceneObject::MakeCurve(NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 0, 0)}, 1));
    Check(!o.HasDisplayOverride() && std::string(o.DisplayOverrideName()) == "UseViewport", "a fresh object follows the viewport");
    int ok = 0;
    for (const std::string& name : SceneObject::DisplayOverrideNames()) {
      if (!o.SetDisplayOverride(name)) continue;
      const int set = o.force_wireframe + o.force_shaded + o.force_rendered + o.force_ghosted + o.force_xray + o.force_technical +
                      o.force_artistic + o.force_pen + o.force_arctic + o.force_monochrome;
      if (name == "UseViewport" ? set == 0 : (set == 1 && name == o.DisplayOverrideName())) ++ok;
    }
    Check(ok == static_cast<int>(SceneObject::DisplayOverrideNames().size()),
          "every one of the 11 names (UseViewport + 10 modes) sets exactly its own flag and reads back under the same name");
    Check(o.SetDisplayOverride("x-ray") && o.force_xray && !o.force_pen, "names are case-insensitive and accept X-Ray / xray");
    Check(!o.SetDisplayOverride("RayTraced") && o.force_xray, "RayTraced is refused and leaves the current override untouched");
    SceneObject copy = o;
    Check(copy.force_xray, "the override survives a copy (undo/redo snapshots)");

    // Per-VIEWPORT scope (Rhino: "their display in other viewports won't be affected").
    SceneObject v = SceneObject::MakeCurve(NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(1, 0, 0)}, 1));
    Check(v.SetViewportDisplayOverride("Top", "Pen") && v.DisplayOverrideNameFor("Top") == "Pen" && v.DisplayOverrideNameFor("Front") == "UseViewport",
          "a per-viewport override applies in that viewport only");
    Check(v.DisplayOverrideFlagsFor("Top").pen && !v.DisplayOverrideFlagsFor("Front").pen && !v.HasDisplayOverride() && v.HasAnyDisplayOverride(),
          "...its flags are visible only for that viewport, and it is not an all-viewports override");
    v.SetDisplayOverride("Technical");
    Check(v.DisplayOverrideFlagsFor("Top").pen && !v.DisplayOverrideFlagsFor("Top").technical && v.DisplayOverrideFlagsFor("Front").technical,
          "a viewport's own entry wins over the all-viewports override there; other viewports use the all-viewports one");
    SceneObject vcopy = v;
    Check(vcopy.DisplayOverrideNameFor("Top") == "Pen" && vcopy.DisplayOverrideNameFor("Right") == "Technical", "both scopes survive a copy (undo/redo)");
    Check(v.SetViewportDisplayOverride("Top", "UseViewport") && v.DisplayOverrideNameFor("Top") == "Technical", "UseViewport removes the viewport entry (the all-viewports override then applies)");
    Check(!v.SetViewportDisplayOverride("Top", "RayTraced") && v.viewport_display_overrides.empty(), "RayTraced is refused at viewport scope too");
    v.SetViewportDisplayOverride("Front", "X-Ray");
    v.ClearDisplayOverrides();
    Check(!v.HasAnyDisplayOverride(), "ClearDisplayOverrides (ClearAllObjectDisplayModes) removes both scopes");
    Check(SceneObject::CanonicalDisplayOverrideName("UseView") == "UseViewport" && SceneObject::CanonicalDisplayOverrideName("xray") == "X-Ray",
          "Rhino's UseView spelling is accepted");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
