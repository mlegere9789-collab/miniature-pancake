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
  static Mat4 LookAt(kernel::Point3d eye, kernel::Point3d target, kernel::Vector3d up);
  Mat4 operator*(const Mat4& other) const;
  const float* Data() const { return m.data(); }
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
  void Pan(double dx_pixels, double dy_pixels, int viewport_width, int viewport_height);
  void Dolly(double wheel_steps);             // zoom toward target
  void DollyToward(double wheel_steps, kernel::Point3d world_point);  // zoom about cursor
  void RotateAboutViewAxis(double degrees);   // TiltView
  void ZoomExtents(const kernel::BoundingBox& box, double aspect);

  // Matrices for the current state.
  Mat4 ViewMatrix() const;
  Mat4 ProjectionMatrix(double aspect) const;
  double NearFar(double& far_z) const;

  // Basis vectors of the view.
  kernel::Vector3d Forward() const;
  kernel::Vector3d Right() const;
  kernel::Vector3d Up() const;
  double Distance() const;

  // Picking: normalized device coords in [-1,1] -> world ray.
  Ray ScreenRay(double ndc_x, double ndc_y, double aspect) const;
  // World -> normalized device coordinates. Returns false if behind camera.
  bool Project(kernel::Point3d world, double aspect, double& ndc_x, double& ndc_y, double& depth) const;

  // Size in world units of one pixel at the target distance.
  double PixelSize(int viewport_height) const;

 private:
  CameraState state_;
};

}  // namespace dino8::app
