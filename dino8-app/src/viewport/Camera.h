// Viewport camera with Rhino-style navigation: orbit around a target,
// pan in the view plane, dolly/zoom, perspective or parallel projection,
// and screen<->world ray casting for picking.
#pragma once

#include <array>

#include "doc/Document.h"
#include "dino8/kernel/types.h"

namespace dino8::app {

// Column-major 4x4 float matrix (OpenGL layout).
struct Mat4 {
  std::array<float, 16> m{};
  static Mat4 Identity();
  static Mat4 Perspective(double fov_y_radians, double aspect, double near_z, double far_z);
  static Mat4 Ortho(double left, double right, double bottom, double top, double near_z, double far_z);
  static Mat4 Frustum(double left, double right, double bottom, double top, double near_z, double far_z);
  static Mat4 LookAt(kernel::Point3d eye, kernel::Point3d target, kernel::Vector3d up);
  Mat4 operator*(const Mat4& other) const;
  const float* Data() const { return m.data(); }
  // General 4x4 inverse (cofactor/adjugate method - correct for any
  // invertible matrix, perspective or orthographic alike), used by
  // GlRenderer's SSAO pass to unproject a depth-buffer sample back to
  // view space. Returns Identity if `m` is (numerically) singular.
  Mat4 Inverse() const;
};

struct Ray {
  kernel::Point3d origin;
  kernel::Vector3d direction;  // unit length
};

class Camera {
 public:
  Camera();

  CameraState& State() { return state_; }
  const CameraState& State() const { return state_; }
  void SetState(const CameraState& s) { state_ = s; }

  // Standard views. `Perspective` keeps the current target and distance.
  void SetTop();
  void SetBottom();
  void SetFront();
  void SetBack();
  void SetRight();
  void SetLeft();
  void SetPerspective();
  void SetIsometric();

  // Navigation (deltas in pixels; the camera converts to world units).
  void Orbit(double dx_pixels, double dy_pixels);
  // Same rotation as Orbit, but in exact degrees (RotateView / Spin / Turntable
  // single-step callers that want a precise angle rather than a mouse delta).
  void OrbitDegrees(double yaw_degrees, double pitch_degrees);
  // Turns the camera about the fixed eye point (RotateCamera): the look
  // direction changes and the target moves to keep the same distance, but
  // the eye itself does not move (unlike Orbit/OrbitDegrees, which keep the
  // target fixed and move the eye around it).
  void TurnInPlace(double yaw_degrees, double pitch_degrees);
  void Pan(double dx_pixels, double dy_pixels, int viewport_width, int viewport_height);
  void Dolly(double wheel_steps);             // zoom toward target
  void DollyToward(double wheel_steps, kernel::Point3d world_point);  // zoom about cursor
  void RotateAboutViewAxis(double degrees);   // TiltView
  void ZoomExtents(const kernel::BoundingBox& box, double aspect);

  // Matrices for the current state.
  Mat4 ViewMatrix() const;
  Mat4 ProjectionMatrix(double aspect) const;
  double NearFar(double& far_z) const;
  // RenderBlowup: a projection that maps just the sub-rectangle
  // [ndc_x0,ndc_x1] x [ndc_y0,ndc_y1] of the ORIGINAL full view (normalized
  // device coordinates, -1..1, y up) to the whole output frame -- a true
  // optical zoom into that region (an off-axis frustum in perspective, an
  // asymmetric ortho box in parallel projection), not a post-hoc crop.
  Mat4 BlowupProjectionMatrix(double aspect, double ndc_x0, double ndc_y0, double ndc_x1, double ndc_y1) const;

  // Basis vectors of the view.
  kernel::Vector3d Forward() const;
  kernel::Vector3d Right() const;
  kernel::Vector3d Up() const;
  double Distance() const;
  // The lens convention is openNURBS' 35 mm lens (ON_Viewport::GetCamera35mmLensLength / SetCamera35mmLensLength, the functions the 3dm reader and
  // writer call): a 36 x 24 mm film, the lens being measured against the half extent of the SHORT side of the view, 12 mm. So
  //     tan(half angle of the short side) = 12 / lens
  // whatever the shape of the viewport (a landscape view's short side is its height, a portrait view's its width) and the long side follows from the
  // aspect ratio (width / height).
  static constexpr double kFilmHalfShortMm = 12.0;
  // tan(half angle of the short side) for a lens.
  static double TanHalfShort(double lens_mm) { return kFilmHalfShortMm / lens_mm; }
  // tan(vertical half angle) for a lens in a viewport of the given aspect: 12 / lens for a landscape (or square) view, 12 / (lens * aspect) for a portrait one.
  static double TanHalfFovVertical(double lens_mm, double aspect) {
    const double t = TanHalfShort(lens_mm);
    return aspect >= 1.0 ? t : t / (aspect > 1e-3 ? aspect : 1e-3);
  }
  // The lens that gives a vertical half-angle tangent `tan_v` in a viewport of that aspect (the inverse of TanHalfFovVertical).
  static double LensForTanHalfFovVertical(double tan_v, double aspect) {
    const double t_short = aspect >= 1.0 ? tan_v : tan_v * (aspect > 1e-3 ? aspect : 1e-3);
    return kFilmHalfShortMm / t_short;
  }
  // tan(fov_y / 2) for the current lens in a viewport of the given aspect. Meaningless (but harmless) when the camera is orthographic - callers
  // ignore it in that case.
  double TanHalfFovY(double aspect) const { return TanHalfFovVertical(state_.lens_mm, aspect); }

  // Picking: normalized device coords in [-1,1] -> world ray.
  Ray ScreenRay(double ndc_x, double ndc_y, double aspect) const;
  // World -> normalized device coordinates. Returns false if behind camera.
  bool Project(kernel::Point3d world, double aspect, double& ndc_x, double& ndc_y, double& depth) const;

  // Size in world units of one pixel at the target distance.
  // `aspect` (width / height) matters only in a perspective view of a portrait window, where the short side is the width.
  double PixelSize(int viewport_height, double aspect = 1.0) const;

  // Padding factor ZoomExtents leaves around the geometry (1 = tight;
  // SetZoomExtentsBorder changes it for every viewport).
  static double zoom_extents_border;

 private:
  CameraState state_;
};

}  // namespace dino8::app
