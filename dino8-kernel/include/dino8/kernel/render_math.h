#pragma once

// Render / material / compositing node maths: the pure, deterministic evaluation functions behind the shader and
// compositor nodes of the render-anim Register rows. Header-only and free of the document model, so the same code
// can run in the software raytracer, the compositor and the node-graph evaluator. Every function states the formula
// it implements; colours are scene-linear RGB (double) unless a function says otherwise.

#include <algorithm>
#include <cmath>
#include <vector>

namespace dino8::kernel::rendermath {

struct Rgb {
  double r = 0, g = 0, b = 0;
};

struct Vec3 {
  double x = 0, y = 0, z = 0;
};

inline double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double Lerp(double a, double b, double t) { return a * (1.0 - t) + b * t; }
inline Rgb Lerp(const Rgb& a, const Rgb& b, double t) { return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t)}; }

// ---- Colour transfer (compositor Convert Colorspace: sRGB <-> scene-linear Rec.709) --------------------------------
// IEC 61966-2-1 transfer function and its inverse on one channel.
inline double SrgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
inline double LinearToSrgb(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }
inline Rgb SrgbToLinear(const Rgb& c) { return {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b)}; }
inline Rgb LinearToSrgb(const Rgb& c) { return {LinearToSrgb(c.r), LinearToSrgb(c.g), LinearToSrgb(c.b)}; }

// ---- Compositor Exposure: colour * 2^stops ---------------------------------------------------------------------
inline Rgb Exposure(const Rgb& c, double stops) {
  const double k = std::exp2(stops);
  return {c.r * k, c.g * k, c.b * k};
}

// ---- RGB to BW: relative luminance with the Rec.709 primaries of the scene-linear working space ------------------
inline double RgbToBw(const Rgb& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

// ---- Invert Color: lerp(colour, 1 - colour, fac) ------------------------------------------------------------------
inline Rgb InvertColor(const Rgb& c, double fac) {
  return Lerp(c, Rgb{1.0 - c.r, 1.0 - c.g, 1.0 - c.b}, fac);
}

// ---- Gamma: colour^gamma per channel; a non-positive channel stays as it is --------------------------------------
inline Rgb Gamma(const Rgb& c, double gamma) {
  auto g = [gamma](double v) { return v > 0.0 ? std::pow(v, gamma) : v; };
  return {g(c.r), g(c.g), g(c.b)};
}

// ---- Brightness / Contrast (shader node): max((1 + contrast) * c + brightness - contrast / 2, 0) -------------------
inline Rgb BrightnessContrast(const Rgb& c, double bright, double contrast) {
  const double a = 1.0 + contrast, b = bright - contrast * 0.5;
  auto f = [a, b](double v) { return std::max(a * v + b, 0.0); };
  return {f(c.r), f(c.g), f(c.b)};
}

// ---- HSV conversion (hue in 0..1) and the Hue/Saturation/Value node ---------------------------------------------
inline Rgb RgbToHsv(const Rgb& c) {
  const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
  double h = 0.0, s = 0.0;
  if (mx != 0.0) s = d / mx;
  if (d != 0.0) {
    if (mx == c.r) h = (c.g - c.b) / d;
    else if (mx == c.g) h = 2.0 + (c.b - c.r) / d;
    else h = 4.0 + (c.r - c.g) / d;
    h /= 6.0;
    if (h < 0.0) h += 1.0;
  }
  return {h, s, mx};  // (h, s, v) packed in (r, g, b)
}
inline Rgb HsvToRgb(const Rgb& hsv) {
  double h = hsv.r - std::floor(hsv.r);
  const double s = hsv.g, v = hsv.b;
  const double h6 = h * 6.0;
  const int i = static_cast<int>(std::floor(h6)) % 6;
  const double f = h6 - std::floor(h6), p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
  switch (i) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
  }
}
// hue 0.5 is "no shift": the hue is rotated by (hue - 0.5) of a full turn; saturation is clamped to 0..1; the result
// is mixed with the input by fac and floored at 0.
inline Rgb HueSaturationValue(const Rgb& c, double hue, double saturation, double value, double fac) {
  Rgb hsv = RgbToHsv(c);
  hsv.r = hsv.r + hue + 0.5;
  hsv.r -= std::floor(hsv.r);
  hsv.g = Clamp(hsv.g * saturation, 0.0, 1.0);
  hsv.b *= value;
  const Rgb o = Lerp(c, HsvToRgb(hsv), fac);
  return {std::max(o.r, 0.0), std::max(o.g, 0.0), std::max(o.b, 0.0)};
}

// ---- Mix node blend modes (per channel, fac is the blend factor) ------------------------------------------------
enum class MixBlend { Mix, Darken, Multiply, Lighten, Screen, Overlay, Add, Subtract, Difference, Divide };
inline double MixChannel(MixBlend m, double fac, double a, double b) {
  const double facm = 1.0 - fac;
  switch (m) {
    case MixBlend::Mix: return Lerp(a, b, fac);
    case MixBlend::Darken: return std::min(a, b * fac);
    case MixBlend::Multiply: return facm * a + fac * a * b;
    case MixBlend::Lighten: return std::max(a, b * fac);
    case MixBlend::Screen: return 1.0 - (facm + fac * (1.0 - b)) * (1.0 - a);
    case MixBlend::Overlay:
      return a < 0.5 ? a * (facm + 2.0 * fac * b) : 1.0 - (facm + 2.0 * fac * (1.0 - b)) * (1.0 - a);
    case MixBlend::Add: return a + fac * b;
    case MixBlend::Subtract: return a - fac * b;
    case MixBlend::Difference: return facm * a + fac * std::fabs(a - b);
    case MixBlend::Divide: return b != 0.0 ? facm * a + fac * a / b : a;
  }
  return a;
}
inline Rgb MixColors(MixBlend m, double fac, const Rgb& a, const Rgb& b, bool clampResult = false) {
  Rgb o{MixChannel(m, fac, a.r, b.r), MixChannel(m, fac, a.g, b.g), MixChannel(m, fac, a.b, b.b)};
  if (clampResult) o = {Clamp(o.r, 0, 1), Clamp(o.g, 0, 1), Clamp(o.b, 0, 1)};
  return o;
}

// ---- Map Range (float) -------------------------------------------------------------------------------------------
enum class MapRangeMode { Linear, Stepped, SmoothStep, SmootherStep };
inline double MapRange(double v, double fromMin, double fromMax, double toMin, double toMax, MapRangeMode mode,
                       double steps, bool clampResult) {
  double result;
  if (fromMax == fromMin) {
    result = 0.0;
  } else {
    double f = (v - fromMin) / (fromMax - fromMin);
    switch (mode) {
      case MapRangeMode::Linear: break;
      case MapRangeMode::Stepped: f = steps > 0.0 ? std::floor(f * (steps + 1.0)) / steps : 0.0; break;
      case MapRangeMode::SmoothStep:
        f = Clamp(f, 0.0, 1.0);
        f = f * f * (3.0 - 2.0 * f);
        break;
      case MapRangeMode::SmootherStep:
        f = Clamp(f, 0.0, 1.0);
        f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
        break;
    }
    result = toMin + f * (toMax - toMin);
  }
  if (clampResult) result = Clamp(result, std::min(toMin, toMax), std::max(toMin, toMax));
  return result;
}

// ---- Math node (float): the operations whose definition is a closed form ------------------------------------------
enum class MathOp {
  Add, Subtract, Multiply, Divide, MultiplyAdd, Power, Logarithm, SquareRoot, InverseSquareRoot, Absolute, Exponent,
  Minimum, Maximum, LessThan, GreaterThan, Sign, Compare, Round, Floor, Ceil, Truncate, Fraction, Modulo, FlooredModulo,
  Wrap, Snap, PingPong, Sine, Cosine, Tangent, Arctan2, ToRadians, ToDegrees
};
inline double MathNode(MathOp op, double a, double b = 0.0, double c = 0.0) {
  constexpr double kPi = 3.14159265358979323846;
  switch (op) {
    case MathOp::Add: return a + b;
    case MathOp::Subtract: return a - b;
    case MathOp::Multiply: return a * b;
    case MathOp::Divide: return b != 0.0 ? a / b : 0.0;
    case MathOp::MultiplyAdd: return a * b + c;
    case MathOp::Power: {
      if (a >= 0.0) return std::pow(a, b);
      const double frac = std::fabs(std::fmod(b, 1.0));  // a negative base is defined only for whole exponents
      return (frac > 0.999 || frac < 0.001) ? std::pow(a, std::floor(b + 0.5)) : 0.0;
    }
    case MathOp::Logarithm: return (a > 0.0 && b > 0.0) ? std::log(a) / std::log(b) : 0.0;
    case MathOp::SquareRoot: return a >= 0.0 ? std::sqrt(a) : 0.0;
    case MathOp::InverseSquareRoot: return a > 0.0 ? 1.0 / std::sqrt(a) : 0.0;
    case MathOp::Absolute: return std::fabs(a);
    case MathOp::Exponent: return std::exp(a);
    case MathOp::Minimum: return std::min(a, b);
    case MathOp::Maximum: return std::max(a, b);
    case MathOp::LessThan: return a < b ? 1.0 : 0.0;
    case MathOp::GreaterThan: return a > b ? 1.0 : 0.0;
    case MathOp::Sign: return a > 0.0 ? 1.0 : (a < 0.0 ? -1.0 : 0.0);
    case MathOp::Compare: return std::fabs(a - b) <= std::max(c, 1e-5) ? 1.0 : 0.0;
    case MathOp::Round: return std::floor(a + 0.5);
    case MathOp::Floor: return std::floor(a);
    case MathOp::Ceil: return std::ceil(a);
    case MathOp::Truncate: return std::trunc(a);
    case MathOp::Fraction: return a - std::floor(a);
    case MathOp::Modulo: return b != 0.0 ? std::fmod(a, b) : 0.0;  // truncated: the sign follows a
    case MathOp::FlooredModulo: return b != 0.0 ? a - std::floor(a / b) * b : 0.0;  // the sign follows b
    case MathOp::Wrap: {  // inputs (value, max, min): a wrapped into [c, b)
      const double range = b - c;
      return range != 0.0 ? a - range * std::floor((a - c) / range) : c;
    }
    case MathOp::Snap: return b != 0.0 ? std::floor(a / b) * b : 0.0;
    case MathOp::PingPong: {  // triangle wave of period 2b between 0 and b
      if (b == 0.0) return 0.0;
      const double t = (a - b) / (b * 2.0);
      return std::fabs((t - std::floor(t)) * b * 2.0 - b);
    }
    case MathOp::Sine: return std::sin(a);
    case MathOp::Cosine: return std::cos(a);
    case MathOp::Tangent: return std::tan(a);
    case MathOp::Arctan2: return std::atan2(a, b);
    case MathOp::ToRadians: return a * kPi / 180.0;
    case MathOp::ToDegrees: return a * 180.0 / kPi;
  }
  return 0.0;
}

// ---- Vector Math node ------------------------------------------------------------------------------------------
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 Normalize(const Vec3& a) {
  const double l = Length(a);
  return l != 0.0 ? a * (1.0 / l) : Vec3{};
}

enum class VecOp {
  Add, Subtract, Multiply, Divide, Cross, Reflect, Project, Scale, Normalize, Absolute, Minimum, Maximum, Floor, Ceil,
  Fraction, Modulo, Snap
};
// Component-wise unless stated; a is the first vector, b the second, s the Scale input. Divide by a zero component
// gives 0 for that component. Project is the projection of a onto b (0 when b is the zero vector). Reflect reflects
// a about the normal b (b is normalised first).
inline Vec3 VectorMath(VecOp op, const Vec3& a, const Vec3& b = {}, double s = 1.0) {
  auto each = [](const Vec3& u, const Vec3& v, double (*f)(double, double)) {
    return Vec3{f(u.x, v.x), f(u.y, v.y), f(u.z, v.z)};
  };
  switch (op) {
    case VecOp::Add: return a + b;
    case VecOp::Subtract: return a - b;
    case VecOp::Multiply: return {a.x * b.x, a.y * b.y, a.z * b.z};
    case VecOp::Divide:
      return each(a, b, [](double p, double q) { return q != 0.0 ? p / q : 0.0; });
    case VecOp::Cross: return Cross(a, b);
    case VecOp::Reflect: {
      const Vec3 n = Normalize(b);
      return a - n * (2.0 * Dot(a, n));
    }
    case VecOp::Project: {
      const double d = Dot(b, b);
      return d != 0.0 ? b * (Dot(a, b) / d) : Vec3{};
    }
    case VecOp::Scale: return a * s;
    case VecOp::Normalize: return Normalize(a);
    case VecOp::Absolute: return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)};
    case VecOp::Minimum: return each(a, b, [](double p, double q) { return std::min(p, q); });
    case VecOp::Maximum: return each(a, b, [](double p, double q) { return std::max(p, q); });
    case VecOp::Floor: return {std::floor(a.x), std::floor(a.y), std::floor(a.z)};
    case VecOp::Ceil: return {std::ceil(a.x), std::ceil(a.y), std::ceil(a.z)};
    case VecOp::Fraction: return {a.x - std::floor(a.x), a.y - std::floor(a.y), a.z - std::floor(a.z)};
    case VecOp::Modulo:
      return each(a, b, [](double p, double q) { return q != 0.0 ? std::fmod(p, q) : 0.0; });
    case VecOp::Snap:
      return each(a, b, [](double p, double q) { return q != 0.0 ? std::floor(p / q) * q : 0.0; });
  }
  return {};
}

// ---- Fresnel: unpolarised reflectance of a dielectric interface ---------------------------------------------------
// cosTheta is the cosine of the angle between the view direction and the normal (the sign is ignored), eta the ratio
// of the refractive index behind the surface to the one in front. Returns 1 past the critical angle.
inline double FresnelDielectricCos(double cosTheta, double eta) {
  const double c = std::fabs(cosTheta);
  double g = eta * eta - 1.0 + c * c;
  if (g <= 0.0) return 1.0;  // total internal reflection
  g = std::sqrt(g);
  const double A = (g - c) / (g + c);
  const double B = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
  return 0.5 * A * A * (1.0 + B * B);
}
// The Fresnel shader node: the IOR input, flipped when the hit is a back face.
inline double FresnelNode(double ior, double cosTheta, bool backFace) {
  double eta = std::max(ior, 1e-5);
  if (backFace) eta = 1.0 / eta;
  return FresnelDielectricCos(cosTheta, eta);
}

// ---- Mapping node ------------------------------------------------------------------------------------------------
enum class MappingType { Point, Texture, Vector, Normal };
// Euler XYZ rotation (radians): the X rotation is applied first, then Y, then Z (matrix Rz * Ry * Rx).
inline Vec3 RotateEulerXyz(const Vec3& v, const Vec3& e) {
  const double cx = std::cos(e.x), sx = std::sin(e.x), cy = std::cos(e.y), sy = std::sin(e.y), cz = std::cos(e.z),
               sz = std::sin(e.z);
  Vec3 p{v.x, v.y * cx - v.z * sx, v.y * sx + v.z * cx};
  p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
  return {p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z};
}
// The inverse rotation: Z, then Y, then X, each by the negated angle.
inline Vec3 RotateEulerXyzInverse(const Vec3& v, const Vec3& e) {
  const double cx = std::cos(-e.x), sx = std::sin(-e.x), cy = std::cos(-e.y), sy = std::sin(-e.y), cz = std::cos(-e.z),
               sz = std::sin(-e.z);
  Vec3 p{v.x * cz - v.y * sz, v.x * sz + v.y * cz, v.z};
  p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
  return {p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};
}
// Point: location + R * (scale * v). Texture: the inverse, (R^-1 * (v - location)) / scale. Vector: R * (scale * v).
// Normal: normalize(R * (v / scale)), the inverse transpose of the point transform.
inline Vec3 MappingNode(MappingType t, const Vec3& v, const Vec3& loc, const Vec3& rotEuler, const Vec3& scale) {
  auto mul = [](const Vec3& a, const Vec3& b) { return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}; };
  auto div = [](const Vec3& a, const Vec3& b) {
    return Vec3{b.x != 0.0 ? a.x / b.x : 0.0, b.y != 0.0 ? a.y / b.y : 0.0, b.z != 0.0 ? a.z / b.z : 0.0};
  };
  switch (t) {
    case MappingType::Point: return RotateEulerXyz(mul(v, scale), rotEuler) + loc;
    case MappingType::Texture: return div(RotateEulerXyzInverse(v - loc, rotEuler), scale);
    case MappingType::Vector: return RotateEulerXyz(mul(v, scale), rotEuler);
    case MappingType::Normal: return Normalize(RotateEulerXyz(div(v, scale), rotEuler));
  }
  return v;
}

// ---- Color Ramp --------------------------------------------------------------------------------------------------
enum class RampInterp { Linear, Constant, Ease };
struct RampStop {
  double pos;
  Rgb color;
};
// Stops must be sorted by position. Outside the first and last stop the end colours extend; Constant holds the colour
// of the lower stop, Linear interpolates, Ease interpolates with t*t*(3 - 2t).
inline Rgb ColorRamp(const std::vector<RampStop>& stops, RampInterp interp, double fac) {
  if (stops.empty()) return {};
  if (fac <= stops.front().pos) return stops.front().color;
  if (fac >= stops.back().pos) return stops.back().color;
  size_t hi = 1;  // the first stop strictly above fac; the one before it is at or below fac
  while (hi < stops.size() && stops[hi].pos <= fac) ++hi;
  const RampStop& a = stops[hi - 1];
  const RampStop& b = stops[hi];
  if (interp == RampInterp::Constant) return a.color;
  double t = (fac - a.pos) / (b.pos - a.pos);
  if (interp == RampInterp::Ease) t = t * t * (3.0 - 2.0 * t);
  return Lerp(a.color, b.color, t);
}

// ---- Gradient Texture ----------------------------------------------------------------------------------------------
enum class GradientType { Linear, Quadratic, Easing, Diagonal, Spherical, QuadraticSphere, Radial };
inline double GradientTexture(GradientType t, const Vec3& p) {
  constexpr double kTwoPi = 6.28318530717958647692;
  double r = 0.0;
  switch (t) {
    case GradientType::Linear: r = p.x; break;
    case GradientType::Quadratic: r = std::max(p.x, 0.0); r *= r; break;
    case GradientType::Easing: {
      const double x = Clamp(p.x, 0.0, 1.0);
      r = x * x * (3.0 - 2.0 * x);
      break;
    }
    case GradientType::Diagonal: r = (p.x + p.y) * 0.5; break;
    case GradientType::Spherical: r = std::max(1.0 - Length(p), 0.0); break;
    case GradientType::QuadraticSphere: r = std::max(1.0 - Length(p), 0.0); r *= r; break;
    case GradientType::Radial: r = std::atan2(p.y, p.x) / kTwoPi + 0.5; break;
  }
  return Clamp(r, 0.0, 1.0);
}

}  // namespace dino8::kernel::rendermath
