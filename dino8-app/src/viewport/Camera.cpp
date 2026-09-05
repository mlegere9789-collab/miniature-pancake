#include "viewport/Camera.h"

#include <algorithm>
#include <cmath>

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

Mat4 Mat4::Identity() {
  Mat4 r;
  r.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  return r;
}

Mat4 Mat4::Perspective(double fov_y, double aspect, double n, double f) {
  Mat4 r;
  const double t = 1.0 / std::tan(fov_y / 2.0);
  r.m = {static_cast<float>(t / aspect), 0, 0, 0,
         0, static_cast<float>(t), 0, 0,
         0, 0, static_cast<float>((f + n) / (n - f)), -1,
         0, 0, static_cast<float>(2 * f * n / (n - f)), 0};
  return r;
}

Mat4 Mat4::Ortho(double l, double rr, double b, double t, double n, double f) {
  Mat4 r;
  r.m = {static_cast<float>(2 / (rr - l)), 0, 0, 0,
         0, static_cast<float>(2 / (t - b)), 0, 0,
         0, 0, static_cast<float>(-2 / (f - n)), 0,
         static_cast<float>(-(rr + l) / (rr - l)), static_cast<float>(-(t + b) / (t - b)),
         static_cast<float>(-(f + n) / (f - n)), 1};
  return r;
}

Mat4 Mat4::LookAt(Point3d eye, Point3d target, Vector3d up) {
  Vector3d f = target - eye;
  f.Unitize();
  Vector3d s = ON_CrossProduct(f, up);
  s.Unitize();
  Vector3d u = ON_CrossProduct(s, f);
  Mat4 r;
  r.m = {static_cast<float>(s.x), static_cast<float>(u.x), static_cast<float>(-f.x), 0,
         static_cast<float>(s.y), static_cast<float>(u.y), static_cast<float>(-f.y), 0,
         static_cast<float>(s.z), static_cast<float>(u.z), static_cast<float>(-f.z), 0,
         static_cast<float>(-(s.x * eye.x + s.y * eye.y + s.z * eye.z)),
         static_cast<float>(-(u.x * eye.x + u.y * eye.y + u.z * eye.z)),
         static_cast<float>(f.x * eye.x + f.y * eye.y + f.z * eye.z), 1};
  return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
  Mat4 r;
  for (int c = 0; c < 4; ++c) {
    for (int rr = 0; rr < 4; ++rr) {
      float sum = 0;
      for (int k = 0; k < 4; ++k) sum += m[k * 4 + rr] * o.m[c * 4 + k];
      r.m[c * 4 + rr] = sum;
    }
  }
  return r;
}

Camera::Camera() { SetPerspective(); }

namespace {
void PlaceEye(CameraState& s, Vector3d direction_from_target, double distance, Vector3d up) {
  direction_from_target.Unitize();
  s.eye = s.target + direction_from_target * distance;
  s.up = up;
}
}  // namespace

void Camera::SetTop() {
  state_.perspective = false;
  PlaceEye(state_, Vector3d(0, 0, 1), std::max(Distance(), 1.0), Vector3d(0, 1, 0));
}
void Camera::SetBottom() {
  state_.perspective = false;
  PlaceEye(state_, Vector3d(0, 0, -1), std::max(Distance(), 1.0), Vector3d(0, 1, 0));
}
void Camera::SetFront() {
  state_.perspective = false;
  PlaceEye(state_, Vector3d(0, -1, 0), std::max(Distance(), 1.0), Vector3d(0, 0, 1));
}
void Camera::SetBack() {
  state_.perspective = false;
  PlaceEye(state_, Vector3d(0, 1, 0), std::max(Distance(), 1.0), Vector3d(0, 0, 1));
}
void Camera::SetRight() {
  state_.perspective = false;
  PlaceEye(state_, Vector3d(1, 0, 0), std::max(Distance(), 1.0), Vector3d(0, 0, 1));
}
void Camera::SetLeft() {
  state_.perspective = false;
  PlaceEye(state_, Vector3d(-1, 0, 0), std::max(Distance(), 1.0), Vector3d(0, 0, 1));
}
void Camera::SetPerspective() {
  state_.perspective = true;
  const double d = std::max(Distance(), 1.0);
  PlaceEye(state_, Vector3d(1, -1, 0.8), d, Vector3d(0, 0, 1));
}
void Camera::SetIsometric() {
  state_.perspective = false;
  PlaceEye(state_, Vector3d(1, -1, 1), std::max(Distance(), 1.0), Vector3d(0, 0, 1));
}

double Camera::Distance() const { return (state_.eye - state_.target).Length(); }

Vector3d Camera::Forward() const {
  Vector3d f = state_.target - state_.eye;
  f.Unitize();
  return f;
}

Vector3d Camera::Right() const {
  Vector3d r = ON_CrossProduct(Forward(), state_.up);
  if (r.Length() < 1e-9) r = Vector3d(1, 0, 0);
  r.Unitize();
  return r;
}

Vector3d Camera::Up() const {
  Vector3d u = ON_CrossProduct(Right(), Forward());
  u.Unitize();
  return u;
}

void Camera::Orbit(double dx, double dy) {
  // Rhino orbits around the world Z axis for horizontal drags and around
  // the view's own right axis for vertical drags, keeping Z "up".
  const double yaw = -dx * 0.008;
  const double pitch = -dy * 0.008;
  Vector3d offset = state_.eye - state_.target;
  // Yaw about world Z.
  {
    const double c = std::cos(yaw), s = std::sin(yaw);
    offset = Vector3d(offset.x * c - offset.y * s, offset.x * s + offset.y * c, offset.z);
  }
  // Pitch about the right axis, clamped so we never flip over the pole.
  {
    Vector3d right = ON_CrossProduct(Vector3d(0, 0, 1), offset);
    if (right.Length() < 1e-9) right = Vector3d(1, 0, 0);
    right.Unitize();
    const double len = offset.Length();
    Vector3d dir = offset / len;
    const double current_pitch = std::asin(std::clamp(dir.z, -1.0, 1.0));
    const double new_pitch = std::clamp(current_pitch + pitch, -1.55, 1.55);
    const double delta = new_pitch - current_pitch;
    // Rodrigues rotation of dir about `right` by delta.
    const double c = std::cos(delta), s = std::sin(delta);
    Vector3d rotated = dir * c + ON_CrossProduct(right, dir) * s + right * (ON_DotProduct(right, dir) * (1 - c));
    offset = rotated * len;
  }
  state_.eye = state_.target + offset;
  state_.up = Vector3d(0, 0, 1);
  state_.perspective = state_.perspective;  // orbiting a parallel view keeps it parallel
}

void Camera::Pan(double dx, double dy, int w, int h) {
  const double pixel = PixelSize(h);
  (void)w;
  const Vector3d shift = Right() * (-dx * pixel) + Up() * (dy * pixel);
  state_.eye = state_.eye + shift;
  state_.target = state_.target + shift;
}

void Camera::Dolly(double steps) {
  const double factor = std::pow(0.85, steps);
  if (state_.perspective) {
    Vector3d offset = state_.eye - state_.target;
    const double d = std::max(offset.Length() * factor, 0.01);
    offset.Unitize();
    state_.eye = state_.target + offset * d;
  } else {
    state_.ortho_height = std::max(state_.ortho_height * factor, 0.001);
  }
}

void Camera::DollyToward(double steps, Point3d world_point) {
  // Zoom so the point under the cursor stays under the cursor: move the
  // target toward that point by the same proportion the distance shrinks.
  const double factor = std::pow(0.85, steps);
  const Vector3d to_point = world_point - state_.target;
  const Vector3d shift = to_point * (1.0 - factor);
  state_.target = state_.target + shift;
  state_.eye = state_.eye + shift;
  Dolly(steps);
}

void Camera::RotateAboutViewAxis(double degrees) {
  const double a = degrees * ON_PI / 180.0;
  const Vector3d f = Forward();
  const Vector3d u = Up();
  const Vector3d r = Right();
  state_.up = u * std::cos(a) + r * std::sin(a);
  (void)f;
}

void Camera::ZoomExtents(const kernel::BoundingBox& box, double aspect) {
  const Point3d center((box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2,
                       (box.min.z + box.max.z) / 2);
  const double radius = std::max(0.5 * (box.max - box.min).Length(), 1e-3);
  const Vector3d dir = Forward();
  const Vector3d r = Right(), u = Up();
  // Exact extents of the box in the view plane (tighter than the bounding sphere).
  double half_w = 0, half_h = 0;
  for (int c = 0; c < 8; ++c) {
    const Point3d p((c & 1) ? box.max.x : box.min.x, (c & 2) ? box.max.y : box.min.y, (c & 4) ? box.max.z : box.min.z);
    const Vector3d d = p - center;
    half_w = std::max(half_w, std::fabs(ON_DotProduct(d, r)));
    half_h = std::max(half_h, std::fabs(ON_DotProduct(d, u)));
  }
  half_w = std::max(half_w, 1e-3);
  half_h = std::max(half_h, 1e-3);
  state_.target = center;
  if (state_.perspective) {
    const double fov = 2.0 * std::atan(18.0 / state_.lens_mm);  // 36mm sensor
    const double tan_v = std::tan(fov / 2.0), tan_h = tan_v * std::max(aspect, 0.1);
    // Smallest eye distance along -dir from the center so every box corner is inside the frustum.
    double distance = 1e-3;
    for (int c = 0; c < 8; ++c) {
      const Point3d p((c & 1) ? box.max.x : box.min.x, (c & 2) ? box.max.y : box.min.y, (c & 4) ? box.max.z : box.min.z);
      const Vector3d d = p - center;
      const double dz = ON_DotProduct(d, dir);
      distance = std::max(distance, std::fabs(ON_DotProduct(d, r)) / tan_h - dz);
      distance = std::max(distance, std::fabs(ON_DotProduct(d, u)) / tan_v - dz);
    }
    distance = std::max(distance * 1.08, radius * 0.5);
    state_.eye = center - dir * distance;
  } else {
    const double a = std::max(aspect, 0.1);
    state_.ortho_height = 2.0 * std::max(half_h, half_w / a) * 1.1;
    state_.eye = center - dir * std::max(radius * 4.0, 10.0);
  }
}

double Camera::NearFar(double& far_z) const {
  const double d = Distance();
  far_z = std::max(d * 20.0, 1000.0);
  return std::max(d * 0.001, 0.01);
}

Mat4 Camera::ViewMatrix() const { return Mat4::LookAt(state_.eye, state_.target, state_.up); }

Mat4 Camera::ProjectionMatrix(double aspect) const {
  double far_z;
  const double near_z = NearFar(far_z);
  if (state_.perspective) {
    const double fov = 2.0 * std::atan(18.0 / state_.lens_mm);
    return Mat4::Perspective(fov, aspect, near_z, far_z);
  }
  const double h = state_.ortho_height / 2.0;
  const double w = h * aspect;
  return Mat4::Ortho(-w, w, -h, h, -far_z, far_z);
}

Ray Camera::ScreenRay(double ndc_x, double ndc_y, double aspect) const {
  Ray ray;
  const Vector3d f = Forward(), r = Right(), u = Up();
  if (state_.perspective) {
    const double fov = 2.0 * std::atan(18.0 / state_.lens_mm);
    const double ty = std::tan(fov / 2.0);
    Vector3d dir = f + r * (ndc_x * ty * aspect) + u * (ndc_y * ty);
    dir.Unitize();
    ray.origin = state_.eye;
    ray.direction = dir;
  } else {
    const double h = state_.ortho_height / 2.0;
    ray.origin = state_.eye + r * (ndc_x * h * aspect) + u * (ndc_y * h) - f * 1000.0;
    ray.direction = f;
  }
  return ray;
}

bool Camera::Project(Point3d p, double aspect, double& ndc_x, double& ndc_y, double& depth) const {
  const Vector3d rel = p - state_.eye;
  const double z = ON_DotProduct(rel, Forward());
  const double x = ON_DotProduct(rel, Right());
  const double y = ON_DotProduct(rel, Up());
  if (state_.perspective) {
    if (z <= 1e-9) return false;
    const double fov = 2.0 * std::atan(18.0 / state_.lens_mm);
    const double ty = std::tan(fov / 2.0);
    ndc_x = x / (z * ty * aspect);
    ndc_y = y / (z * ty);
  } else {
    const double h = state_.ortho_height / 2.0;
    ndc_x = x / (h * aspect);
    ndc_y = y / h;
  }
  depth = z;
  return true;
}

double Camera::PixelSize(int viewport_height) const {
  if (viewport_height <= 0) return 1.0;
  if (state_.perspective) {
    const double fov = 2.0 * std::atan(18.0 / state_.lens_mm);
    return 2.0 * Distance() * std::tan(fov / 2.0) / viewport_height;
  }
  return state_.ortho_height / viewport_height;
}

}  // namespace dino8::app
