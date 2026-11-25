// Render-anim Register rows: the evaluation maths of shader and compositor nodes (dino8/kernel/render_math.h). Every
// expected value below is an independent oracle written out here: a closed form worked by hand (blend arithmetic,
// smoothstep 3t^2 - 2t^3, smootherstep 6t^5 - 15t^4 + 10t^3, rotation about one axis), an exact table (the sRGB transfer
// function value 0.21404114...), or a re-derivation by a different route (the Fresnel reflectance from Snell's law and
// the two amplitude coefficients, not from the implementation's combined form). No test reads a result back from the
// function it checks.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/render_math.h"
#include "test_registry.h"

namespace {

namespace rm = dino8::kernel::rendermath;
constexpr double kPi = 3.14159265358979323846;

bool Near(double a, double b, double tol = 1e-12) { return std::fabs(a - b) <= tol; }
bool NearRgb(const rm::Rgb& c, double r, double g, double b, double tol = 1e-12) {
  return Near(c.r, r, tol) && Near(c.g, g, tol) && Near(c.b, b, tol);
}
bool NearVec(const rm::Vec3& v, double x, double y, double z, double tol = 1e-12) {
  return Near(v.x, x, tol) && Near(v.y, y, tol) && Near(v.z, z, tol);
}
void Chk(bool ok, const std::string& what) { d8test::Check(ok, what.c_str()); }

}  // namespace

// D8-BLENDER-SHDNODE-0049 Mix: blend arithmetic at fac 0.5 with a = 0.8, b = 0.4, worked by hand.
D8_EXTRA_TEST(RenderRowShaderMixBlend) {
  using B = rm::MixBlend;
  auto one = [](B m, double fac, double a, double b) { return rm::MixChannel(m, fac, a, b); };
  Chk(Near(one(B::Mix, 0.5, 0.8, 0.4), 0.6), "mix: lerp(0.8, 0.4, 0.5) = 0.6");
  Chk(Near(one(B::Multiply, 0.5, 0.8, 0.4), 0.56), "multiply: 0.5 * 0.8 + 0.5 * 0.32 = 0.56");
  Chk(Near(one(B::Screen, 0.5, 0.8, 0.4), 0.84), "screen: 1 - (0.5 + 0.5 * 0.6) * 0.2 = 0.84");
  Chk(Near(one(B::Overlay, 0.5, 0.8, 0.4), 0.78), "overlay, bright base: 1 - (0.5 + 0.6) * 0.2 = 0.78");
  Chk(Near(one(B::Overlay, 0.5, 0.2, 0.4), 0.18), "overlay, dark base: 0.2 * (0.5 + 0.4) = 0.18");
  Chk(Near(one(B::Add, 0.5, 0.8, 0.4), 1.0), "add: 0.8 + 0.5 * 0.4 = 1.0");
  Chk(Near(one(B::Subtract, 0.5, 0.8, 0.4), 0.6), "subtract: 0.8 - 0.2 = 0.6");
  Chk(Near(one(B::Difference, 0.5, 0.8, 0.4), 0.6), "difference: 0.4 + 0.5 * 0.4 = 0.6");
  Chk(Near(one(B::Divide, 0.5, 0.8, 0.4), 1.4), "divide: 0.4 + 0.5 * 2 = 1.4");
  Chk(Near(one(B::Divide, 0.5, 0.8, 0.0), 0.8), "divide by zero leaves the base");
  Chk(Near(one(B::Darken, 0.5, 0.8, 0.4), 0.2), "darken: min(0.8, 0.2) = 0.2");
  Chk(Near(one(B::Lighten, 0.5, 0.8, 0.4), 0.8), "lighten: max(0.8, 0.2) = 0.8");
  for (B m : {B::Mix, B::Multiply, B::Screen, B::Overlay, B::Add, B::Subtract, B::Difference, B::Divide})
    Chk(Near(one(m, 0.0, 0.7, 0.3), 0.7), "factor 0 returns the base colour for every blend that mixes by factor");
  Chk(Near(one(B::Mix, 1.0, 0.7, 0.3), 0.3), "mix at factor 1 returns the second colour");
  const rm::Rgb o = rm::MixColors(B::Add, 1.0, {0.9, 0.5, 0.0}, {0.4, 0.5, 0.2}, true);
  Chk(NearRgb(o, 1.0, 1.0, 0.2), "clamped add per channel: (1.3, 1.0, 0.2) -> (1, 1, 0.2)");
  const rm::Rgb u = rm::MixColors(B::Add, 1.0, {0.9, 0.5, 0.0}, {0.4, 0.5, 0.2}, false);
  Chk(NearRgb(u, 1.3, 1.0, 0.2), "unclamped add keeps the value above 1 (scene-linear)");
}

// D8-BLENDER-SHDNODE-0065 RGB to BW
D8_EXTRA_TEST(RenderRowShaderRgbToBw) {
  Chk(Near(rm::RgbToBw({1, 1, 1}), 1.0), "white has luminance 1");
  Chk(Near(rm::RgbToBw({0, 0, 0}), 0.0), "black has luminance 0");
  Chk(Near(rm::RgbToBw({1, 0, 0}), 0.2126), "red primary weight 0.2126");
  Chk(Near(rm::RgbToBw({0, 1, 0}), 0.7152), "green primary weight 0.7152");
  Chk(Near(rm::RgbToBw({0, 0, 1}), 0.0722), "blue primary weight 0.0722");
  Chk(Near(rm::RgbToBw({0.5, 0.5, 0.5}), 0.5), "grey 0.5 stays 0.5");
  Chk(Near(rm::RgbToBw({0.25, 0.5, 1.0}), 0.2126 * 0.25 + 0.7152 * 0.5 + 0.0722, 1e-12), "linear in the channels");
}

// D8-BLENDER-SHDNODE-0037 Invert Color
D8_EXTRA_TEST(RenderRowShaderInvertColor) {
  Chk(NearRgb(rm::InvertColor({0.2, 0.5, 1.0}, 1.0), 0.8, 0.5, 0.0), "fac 1 gives 1 - c");
  Chk(NearRgb(rm::InvertColor({0.2, 0.5, 1.0}, 0.0), 0.2, 0.5, 1.0), "fac 0 leaves the colour");
  Chk(NearRgb(rm::InvertColor({0.2, 0.5, 1.0}, 0.25), 0.35, 0.5, 0.75), "fac 0.25: lerp(0.2, 0.8, 0.25) = 0.35");
  const rm::Rgb twice = rm::InvertColor(rm::InvertColor({0.3, 0.6, 0.9}, 1.0), 1.0);
  Chk(NearRgb(twice, 0.3, 0.6, 0.9), "inverting twice is the identity");
}

// D8-BLENDER-SHDNODE-0032 Gamma
D8_EXTRA_TEST(RenderRowShaderGamma) {
  Chk(NearRgb(rm::Gamma({0.5, 0.25, 1.0}, 2.0), 0.25, 0.0625, 1.0), "gamma 2 squares each channel");
  Chk(NearRgb(rm::Gamma({0.25, 0.0625, 0.81}, 0.5), 0.5, 0.25, 0.9), "gamma 0.5 is the square root");
  Chk(NearRgb(rm::Gamma({0.3, 0.6, 0.9}, 1.0), 0.3, 0.6, 0.9), "gamma 1 is the identity");
  Chk(NearRgb(rm::Gamma({0.0, -0.5, 0.5}, 2.2), 0.0, -0.5, std::pow(0.5, 2.2)), "zero and negative channels pass through");
}

// D8-BLENDER-SHDNODE-0007 Brightness/Contrast
D8_EXTRA_TEST(RenderRowShaderBrightContrast) {
  Chk(NearRgb(rm::BrightnessContrast({0.5, 0.5, 0.5}, 0.1, 0.2), 0.6, 0.6, 0.6), "a = 1.2, b = 0.1 - 0.1: 1.2 * 0.5 = 0.6");
  Chk(NearRgb(rm::BrightnessContrast({0.2, 0.4, 0.6}, 0.0, 0.0), 0.2, 0.4, 0.6), "zero brightness and contrast is the identity");
  Chk(NearRgb(rm::BrightnessContrast({0.1, 0.1, 0.1}, -0.5, 0.0), 0.0, 0.0, 0.0), "negative results floor at 0");
  Chk(NearRgb(rm::BrightnessContrast({0.2, 0.2, 0.2}, 0.3, -0.4), 0.62, 0.62, 0.62), "a = 0.6, b = 0.3 + 0.2: 0.12 + 0.5 = 0.62");
  Chk(Near(rm::BrightnessContrast({0.5, 0.5, 0.5}, 0.0, 0.6).r, 0.5), "contrast pivots about mid grey 0.5");
  Chk(Near(rm::BrightnessContrast({1.0, 1.0, 1.0}, 0.0, 1.0).g, 1.5), "contrast 1, c = 1: 2 * 1 - 0.5 = 1.5");
}

// D8-BLENDER-SHDNODE-0036 Hue/Saturation/Value
D8_EXTRA_TEST(RenderRowShaderHsv) {
  const rm::Rgb hsv = rm::RgbToHsv({0.8, 0.4, 0.2});
  Chk(NearRgb(hsv, 1.0 / 18.0, 0.75, 0.8), "(0.8, 0.4, 0.2) is hue 20 degrees, s 0.75, v 0.8");
  Chk(NearRgb(rm::RgbToHsv({0, 1, 1}), 0.5, 1.0, 1.0), "cyan is hue 0.5");
  Chk(NearRgb(rm::HsvToRgb({1.0 / 3.0, 1.0, 1.0}), 0.0, 1.0, 0.0), "hue 1/3 is green");
  Chk(NearRgb(rm::HsvToRgb({0.75, 1.0, 0.5}), 0.25, 0.0, 0.5), "hue 270 degrees, v 0.5 is (0.25, 0, 0.5)");
  Chk(NearRgb(rm::HsvToRgb(rm::RgbToHsv({0.3, 0.7, 0.55})), 0.3, 0.7, 0.55), "round trip");
  Chk(NearRgb(rm::HueSaturationValue({1, 0, 0}, 0.5 + 1.0 / 3.0, 1.0, 1.0, 1.0), 0.0, 1.0, 0.0, 1e-9), "hue +120 degrees: red to green");
  Chk(NearRgb(rm::HueSaturationValue({1, 0, 0}, 0.5 + 2.0 / 3.0, 1.0, 1.0, 1.0), 0.0, 0.0, 1.0, 1e-9), "hue +240 degrees: red to blue");
  Chk(NearRgb(rm::HueSaturationValue({0.8, 0.4, 0.2}, 0.5, 1.0, 1.0, 1.0), 0.8, 0.4, 0.2), "defaults leave the colour");
  Chk(NearRgb(rm::HueSaturationValue({0.8, 0.4, 0.2}, 0.5, 0.0, 1.0, 1.0), 0.8, 0.8, 0.8), "saturation 0 gives grey at v");
  Chk(NearRgb(rm::HueSaturationValue({0.8, 0.4, 0.2}, 0.5, 1.0, 0.5, 1.0), 0.4, 0.2, 0.1), "value 0.5 halves the colour");
  Chk(NearRgb(rm::HueSaturationValue({0.8, 0.4, 0.2}, 0.9, 0.0, 3.0, 0.0), 0.8, 0.4, 0.2), "fac 0 returns the input");
}

// D8-BLENDER-SHDNODE-0046 Map Range
D8_EXTRA_TEST(RenderRowShaderMapRange) {
  using M = rm::MapRangeMode;
  Chk(Near(rm::MapRange(5, 0, 10, 100, 200, M::Linear, 4, false), 150.0), "linear: 5 of [0,10] to [100,200] is 150");
  Chk(Near(rm::MapRange(15, 0, 10, 100, 200, M::Linear, 4, false), 250.0), "unclamped extrapolates");
  Chk(Near(rm::MapRange(15, 0, 10, 100, 200, M::Linear, 4, true), 200.0), "clamped stops at the top");
  Chk(Near(rm::MapRange(-5, 0, 10, 100, 200, M::Linear, 4, true), 100.0), "clamped stops at the bottom");
  Chk(Near(rm::MapRange(5, 0, 10, 200, 100, M::Linear, 4, true), 150.0), "reversed target range");
  Chk(Near(rm::MapRange(15, 0, 10, 200, 100, M::Linear, 4, true), 100.0), "reversed target range clamps to its own bounds");
  Chk(Near(rm::MapRange(5, 3, 3, 0, 1, M::Linear, 4, false), 0.0), "empty source range gives 0");
  Chk(Near(rm::MapRange(0.5, 0, 1, 0, 1, M::Stepped, 4, false), 0.5), "stepped 4: floor(0.5 * 5) / 4 = 0.5");
  Chk(Near(rm::MapRange(0.39, 0, 1, 0, 1, M::Stepped, 4, false), 0.25), "stepped 4: floor(1.95) / 4 = 0.25");
  Chk(Near(rm::MapRange(0.99, 0, 1, 0, 1, M::Stepped, 4, false), 1.0), "stepped 4: floor(4.95) / 4 = 1");
  Chk(Near(rm::MapRange(0.5, 0, 1, 0, 1, M::Stepped, 0, false), 0.0), "stepped with 0 steps gives the range start");
  Chk(Near(rm::MapRange(0.25, 0, 1, 0, 1, M::SmoothStep, 4, false), 0.15625), "smoothstep(0.25) = 3/16 - 2/64 = 0.15625");
  Chk(Near(rm::MapRange(0.25, 0, 1, 0, 1, M::SmootherStep, 4, false), 0.103515625), "smootherstep(0.25) = 0.103515625");
  Chk(Near(rm::MapRange(0.75, 1, 0, 0, 1, M::SmoothStep, 4, false), 0.15625), "smoothstep over a reversed source range");
  Chk(Near(rm::MapRange(2.0, 0, 1, 10, 20, M::SmoothStep, 4, false), 20.0), "smoothstep saturates outside the source range");
}

// D8-BLENDER-SHDNODE-0048 Math
D8_EXTRA_TEST(RenderRowShaderMath) {
  using O = rm::MathOp;
  auto m = [](O op, double a, double b = 0, double c = 0) { return rm::MathNode(op, a, b, c); };
  Chk(Near(m(O::Add, 2, 3), 5), "add");
  Chk(Near(m(O::Subtract, 2, 3), -1), "subtract");
  Chk(Near(m(O::Multiply, 2, 3), 6), "multiply");
  Chk(Near(m(O::Divide, 6, 3), 2), "divide");
  Chk(Near(m(O::Divide, 1, 0), 0), "divide by zero gives 0");
  Chk(Near(m(O::MultiplyAdd, 2, 3, 4), 10), "multiply add");
  Chk(Near(m(O::Power, 2, 10), 1024), "power 2^10");
  Chk(Near(m(O::Power, -2, 3), -8), "negative base, whole exponent");
  Chk(Near(m(O::Power, -2, 0.5), 0), "negative base, fractional exponent gives 0");
  Chk(Near(m(O::Logarithm, 8, 2), 3, 1e-12), "log base 2 of 8");
  Chk(Near(m(O::Logarithm, -1, 2), 0), "log of a negative gives 0");
  Chk(Near(m(O::SquareRoot, 9), 3), "sqrt 9");
  Chk(Near(m(O::SquareRoot, -4), 0), "sqrt of a negative gives 0");
  Chk(Near(m(O::InverseSquareRoot, 4), 0.5), "inverse sqrt 4");
  Chk(Near(m(O::Absolute, -3.5), 3.5), "absolute");
  Chk(Near(m(O::Exponent, 1), 2.71828182845904523536, 1e-12), "exp(1) = e");
  Chk(Near(m(O::Minimum, 2, 3), 2) && Near(m(O::Maximum, 2, 3), 3), "min and max");
  Chk(Near(m(O::LessThan, 1, 2), 1) && Near(m(O::LessThan, 2, 1), 0), "less than");
  Chk(Near(m(O::GreaterThan, 2, 1), 1) && Near(m(O::GreaterThan, 1, 2), 0), "greater than");
  Chk(Near(m(O::Sign, -5), -1) && Near(m(O::Sign, 0), 0) && Near(m(O::Sign, 7), 1), "sign");
  Chk(Near(m(O::Compare, 1.0, 1.05, 0.1), 1) && Near(m(O::Compare, 1.0, 1.2, 0.1), 0), "compare with epsilon");
  Chk(Near(m(O::Round, 2.5), 3) && Near(m(O::Round, -2.5), -2) && Near(m(O::Round, 2.4), 2), "round is floor(a + 0.5)");
  Chk(Near(m(O::Floor, -1.5), -2) && Near(m(O::Ceil, -1.5), -1) && Near(m(O::Truncate, -1.5), -1), "floor, ceil, truncate");
  Chk(Near(m(O::Fraction, -1.25), 0.75), "fraction of -1.25 is 0.75");
  Chk(Near(m(O::Modulo, -7, 3), -1), "truncated modulo -7 mod 3 = -1");
  Chk(Near(m(O::FlooredModulo, -7, 3), 2) && Near(m(O::FlooredModulo, 7, -3), -2), "floored modulo follows the divisor sign");
  Chk(Near(m(O::Modulo, 5, 0), 0) && Near(m(O::FlooredModulo, 5, 0), 0), "modulo by zero gives 0");
  Chk(Near(m(O::Wrap, 5, 3, 1), 1), "wrap 5 into [1, 3): 5 - 2 * 2 = 1");
  Chk(Near(m(O::Wrap, -0.5, 1, 0), 0.5), "wrap -0.5 into [0, 1)");
  Chk(Near(m(O::Snap, 5.3, 2), 4) && Near(m(O::Snap, 5.3, 0), 0), "snap to a grid");
  Chk(Near(m(O::PingPong, 0, 2), 0) && Near(m(O::PingPong, 1, 2), 1) && Near(m(O::PingPong, 2, 2), 2) &&
          Near(m(O::PingPong, 3, 2), 1) && Near(m(O::PingPong, 4, 2), 0),
      "ping-pong is a triangle wave 0, 1, 2, 1, 0 for scale 2");
  Chk(Near(m(O::Sine, kPi / 2), 1) && Near(m(O::Cosine, kPi), -1) && Near(m(O::Tangent, kPi / 4), 1, 1e-12), "trigonometry");
  Chk(Near(m(O::Arctan2, 1, 1), kPi / 4), "arctan2(1, 1) = pi / 4");
  Chk(Near(m(O::ToRadians, 180), kPi) && Near(m(O::ToDegrees, kPi), 180), "degree / radian conversion");
}

// D8-BLENDER-SHDNODE-0096 Vector Math
D8_EXTRA_TEST(RenderRowShaderVectorMath) {
  using O = rm::VecOp;
  using V = rm::Vec3;
  Chk(NearVec(rm::VectorMath(O::Add, {1, 2, 3}, {4, 5, 6}), 5, 7, 9), "add");
  Chk(NearVec(rm::VectorMath(O::Subtract, {1, 2, 3}, {4, 5, 6}), -3, -3, -3), "subtract");
  Chk(NearVec(rm::VectorMath(O::Multiply, {1, 2, 3}, {4, 5, 6}), 4, 10, 18), "component multiply");
  Chk(NearVec(rm::VectorMath(O::Divide, {4, 6, 8}, {2, 0, 4}), 2, 0, 2), "divide, a zero component gives 0");
  Chk(NearVec(rm::VectorMath(O::Cross, {1, 0, 0}, {0, 1, 0}), 0, 0, 1), "i x j = k");
  Chk(NearVec(rm::VectorMath(O::Cross, {0, 1, 0}, {1, 0, 0}), 0, 0, -1), "j x i = -k");
  Chk(NearVec(rm::VectorMath(O::Cross, {1, 2, 3}, {4, 5, 6}), -3, 6, -3), "(1,2,3) x (4,5,6) = (-3, 6, -3)");
  Chk(Near(rm::Dot({1, 2, 3}, {4, 5, 6}), 32), "dot product 4 + 10 + 18");
  Chk(Near(rm::Length({3, 4, 12}), 13), "length of (3, 4, 12)");
  Chk(Near(rm::Length(V{1, 1, 1} - V{4, 5, 1}), 5), "distance is the length of the difference");
  Chk(NearVec(rm::VectorMath(O::Scale, {1, 2, 3}, {}, 2.0), 2, 4, 6), "scale");
  Chk(NearVec(rm::VectorMath(O::Normalize, {0, 3, 4}), 0, 0.6, 0.8), "normalise");
  Chk(NearVec(rm::VectorMath(O::Normalize, {0, 0, 0}), 0, 0, 0), "normalise of zero is zero");
  Chk(NearVec(rm::VectorMath(O::Reflect, {1, -1, 0}, {0, 2, 0}), 1, 1, 0), "reflect about the (normalised) normal");
  Chk(NearVec(rm::VectorMath(O::Project, {3, 4, 0}, {1, 0, 0}), 3, 0, 0), "project onto an axis");
  Chk(NearVec(rm::VectorMath(O::Project, {3, 4, 0}, {0, 0, 0}), 0, 0, 0), "project onto zero is zero");
  Chk(NearVec(rm::VectorMath(O::Absolute, {-1, 2, -3}), 1, 2, 3), "absolute");
  Chk(NearVec(rm::VectorMath(O::Minimum, {1, 5, 3}, {2, 4, 3}), 1, 4, 3) && NearVec(rm::VectorMath(O::Maximum, {1, 5, 3}, {2, 4, 3}), 2, 5, 3), "min, max");
  Chk(NearVec(rm::VectorMath(O::Floor, {1.5, -1.5, 2}), 1, -2, 2) && NearVec(rm::VectorMath(O::Ceil, {1.5, -1.5, 2}), 2, -1, 2), "floor, ceil");
  Chk(NearVec(rm::VectorMath(O::Fraction, {1.25, -1.25, 3}), 0.25, 0.75, 0), "fraction");
  Chk(NearVec(rm::VectorMath(O::Modulo, {-7, 7.5, 5}, {3, 2, 0}), -1, 1.5, 0), "modulo, zero divisor gives 0");
  Chk(NearVec(rm::VectorMath(O::Snap, {5.3, -1.2, 7}, {2, 0.5, 0}), 4, -1.5, 0), "snap per component");
}

// D8-BLENDER-SHDNODE-0031 Fresnel: reflectance re-derived from Snell's law and the s and p amplitude coefficients.
D8_EXTRA_TEST(RenderRowShaderFresnel) {
  auto reference = [](double thetaDeg, double n2) {
    const double ti = thetaDeg * kPi / 180.0, n1 = 1.0;
    const double st = n1 * std::sin(ti) / n2;
    if (st >= 1.0) return 1.0;
    const double tt = std::asin(st);
    const double rs = (n1 * std::cos(ti) - n2 * std::cos(tt)) / (n1 * std::cos(ti) + n2 * std::cos(tt));
    const double rp = (n2 * std::cos(ti) - n1 * std::cos(tt)) / (n2 * std::cos(ti) + n1 * std::cos(tt));
    return 0.5 * (rs * rs + rp * rp);
  };
  for (double n : {1.33, 1.5, 2.4})
    for (double deg : {0.0, 15.0, 30.0, 45.0, 56.3, 60.0, 75.0, 85.0, 89.0}) {
      char label[96];
      std::snprintf(label, sizeof label, "fresnel eta %.2f at %.1f degrees matches Snell + amplitude coefficients", n, deg);
      Chk(Near(rm::FresnelNode(n, std::cos(deg * kPi / 180.0), false), reference(deg, n), 1e-12), label);
    }
  Chk(Near(rm::FresnelNode(1.5, 1.0, false), 0.04, 1e-12), "normal incidence on glass 1.5 reflects 4 percent");
  Chk(Near(rm::FresnelNode(1.0, 0.6, false), 0.0, 1e-12), "equal indices reflect nothing");
  Chk(Near(rm::FresnelNode(1.5, 0.0, false), 1.0, 1e-12), "grazing incidence reflects everything");
  Chk(Near(rm::FresnelNode(1.5, -1.0, false), 0.04, 1e-12), "the sign of the cosine is ignored");
  // Back face: light leaving the denser medium, eta = 1 / 1.5; critical angle asin(1 / 1.5) = 41.81 degrees.
  Chk(Near(rm::FresnelNode(1.5, std::cos(50.0 * kPi / 180.0), true), 1.0), "back face beyond the critical angle: total reflection");
  Chk(Near(rm::FresnelNode(1.5, std::cos(30.0 * kPi / 180.0), true), reference(30.0, 1.0 / 1.5), 1e-12), "back face below the critical angle");
  Chk(Near(rm::FresnelNode(1.5, 1.0, true), 0.04, 1e-12), "back face at normal incidence is also 4 percent");
  // Brewster angle tan(theta) = 1.5: the p component vanishes, so the unpolarised reflectance is rs^2 / 2.
  const double tb = std::atan(1.5), tt = std::asin(std::sin(tb) / 1.5);
  const double rs = (std::cos(tb) - 1.5 * std::cos(tt)) / (std::cos(tb) + 1.5 * std::cos(tt));
  Chk(Near(rm::FresnelNode(1.5, std::cos(tb), false), 0.5 * rs * rs, 1e-12), "at the Brewster angle only the s component reflects");
}

// D8-BLENDER-SHDNODE-0047 Mapping
D8_EXTRA_TEST(RenderRowShaderMapping) {
  using T = rm::MappingType;
  const rm::Vec3 loc{1, 2, 3}, rot{0, 0, kPi / 2}, scl{2, 2, 2};
  Chk(NearVec(rm::MappingNode(T::Point, {1, 0, 0}, loc, rot, scl), 1, 4, 3, 1e-12), "point: (1,0,0) scaled 2, turned 90 about Z -> (0,2,0), plus (1,2,3)");
  Chk(NearVec(rm::MappingNode(T::Texture, {1, 4, 3}, loc, rot, scl), 1, 0, 0, 1e-12), "texture is the inverse of point");
  Chk(NearVec(rm::MappingNode(T::Vector, {1, 0, 0}, loc, rot, scl), 0, 2, 0, 1e-12), "vector ignores the location");
  Chk(NearVec(rm::MappingNode(T::Point, {5, -6, 7}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}), 5, -6, 7), "identity transform");
  // Euler XYZ order: X first, then Y, then Z. (0,1,0) -> X 90: (0,0,1) -> Z 90: unchanged. Z first would give (-1,0,0).
  Chk(NearVec(rm::MappingNode(T::Vector, {0, 1, 0}, {}, {kPi / 2, 0, kPi / 2}, {1, 1, 1}), 0, 0, 1, 1e-12), "Euler XYZ applies X before Z");
  Chk(NearVec(rm::MappingNode(T::Vector, {1, 0, 0}, {}, {0, kPi / 2, 0}, {1, 1, 1}), 0, 0, -1, 1e-12), "90 degrees about Y takes +X to -Z");
  Chk(NearVec(rm::MappingNode(T::Vector, {0, 1, 0}, {}, {kPi / 2, 0, 0}, {1, 1, 1}), 0, 0, 1, 1e-12), "90 degrees about X takes +Y to +Z");
  const rm::Vec3 p{0.3, -0.7, 1.9}, r{0.4, -1.1, 0.8}, s{2, 0.5, 3};
  const rm::Vec3 fwd = rm::MappingNode(T::Point, p, loc, r, s);
  Chk(NearVec(rm::MappingNode(T::Texture, fwd, loc, r, s), p.x, p.y, p.z, 1e-12), "texture(point(p)) = p for a general Euler rotation");
  const double inv = 1.0 / std::sqrt(0.25 + 1.0);
  Chk(NearVec(rm::MappingNode(T::Normal, {1, 1, 0}, {}, {0, 0, 0}, {2, 1, 1}), 0.5 * inv, 1.0 * inv, 0.0, 1e-12), "normal divides by the scale and renormalises");
}

// D8-BLENDER-SHDNODE-0092 Color Ramp
D8_EXTRA_TEST(RenderRowShaderColorRamp) {
  using I = rm::RampInterp;
  const std::vector<rm::RampStop> ramp{{0.0, {0, 0, 0}}, {0.5, {1, 0, 0}}, {1.0, {1, 1, 1}}};
  Chk(NearRgb(rm::ColorRamp(ramp, I::Linear, 0.25), 0.5, 0, 0), "linear halfway between black and red");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Linear, 0.75), 1.0, 0.5, 0.5), "linear halfway between red and white");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Linear, 0.5), 1, 0, 0), "linear on a stop returns the stop");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Linear, -1.0), 0, 0, 0), "below the first stop the first colour extends");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Linear, 2.0), 1, 1, 1), "above the last stop the last colour extends");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Constant, 0.25), 0, 0, 0), "constant holds the lower stop");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Constant, 0.5), 1, 0, 0), "constant switches exactly at the stop");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Constant, 0.99), 1, 0, 0), "constant holds red up to the last stop");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Ease, 0.25), 0.5, 0, 0), "ease at t = 0.5 is 0.5");
  Chk(NearRgb(rm::ColorRamp(ramp, I::Ease, 0.125), 0.15625, 0, 0), "ease at t = 0.25 is 3/16 - 2/64 = 0.15625");
  Chk(NearRgb(rm::ColorRamp({}, I::Linear, 0.3), 0, 0, 0), "an empty ramp is black");
  Chk(NearRgb(rm::ColorRamp({{0.4, {0.2, 0.4, 0.6}}}, I::Linear, 0.9), 0.2, 0.4, 0.6), "a single stop is a constant colour");
}

// D8-BLENDER-SHDNODE-0081 Gradient Texture
D8_EXTRA_TEST(RenderRowShaderGradient) {
  using G = rm::GradientType;
  Chk(Near(rm::GradientTexture(G::Linear, {0.3, 9, 9}), 0.3), "linear is x");
  Chk(Near(rm::GradientTexture(G::Linear, {-1, 0, 0}), 0) && Near(rm::GradientTexture(G::Linear, {2, 0, 0}), 1), "linear clamps to 0..1");
  Chk(Near(rm::GradientTexture(G::Quadratic, {0.5, 0, 0}), 0.25) && Near(rm::GradientTexture(G::Quadratic, {-0.5, 0, 0}), 0), "quadratic is max(x, 0)^2");
  Chk(Near(rm::GradientTexture(G::Easing, {0.25, 0, 0}), 0.15625), "easing is smoothstep(0.25) = 0.15625");
  Chk(Near(rm::GradientTexture(G::Diagonal, {0.2, 0.6, 5}), 0.4), "diagonal is (x + y) / 2");
  Chk(Near(rm::GradientTexture(G::Spherical, {0.3, 0.4, 0}), 0.5), "spherical is 1 - |p| = 0.5 at radius 0.5");
  Chk(Near(rm::GradientTexture(G::Spherical, {2, 0, 0}), 0), "spherical is 0 outside the unit sphere");
  Chk(Near(rm::GradientTexture(G::QuadraticSphere, {0.3, 0.4, 0}), 0.25), "quadratic sphere is (1 - |p|)^2");
  Chk(Near(rm::GradientTexture(G::Radial, {1, 0, 0}), 0.5), "radial: angle 0 is 0.5");
  Chk(Near(rm::GradientTexture(G::Radial, {0, 1, 0}), 0.75), "radial: +Y is 0.75");
  Chk(Near(rm::GradientTexture(G::Radial, {0, -1, 0}), 0.25), "radial: -Y is 0.25");
  Chk(Near(rm::GradientTexture(G::Radial, {-1, 0, 0}), 1.0), "radial: angle pi is 1");
  Chk(Near(rm::GradientTexture(G::Radial, {1, 1, 0}), 0.625), "radial: 45 degrees is 0.625");
}

// D8-BLENDER-CMPNODE-0017 Convert Colorspace (sRGB <-> scene-linear): exact table values of IEC 61966-2-1.
D8_EXTRA_TEST(RenderRowCompConvertColorspace) {
  Chk(Near(rm::SrgbToLinear(0.5), 0.21404114048223255, 1e-12), "sRGB 0.5 -> linear 0.21404114");
  Chk(Near(rm::SrgbToLinear(128.0 / 255.0), 0.21586050011389926, 1e-12), "sRGB 128/255 -> linear 0.21586050");
  Chk(Near(rm::SrgbToLinear(0.0), 0.0) && Near(rm::SrgbToLinear(1.0), 1.0, 1e-12), "end points are fixed");
  Chk(Near(rm::SrgbToLinear(0.04045), 0.0031308049535603713, 1e-15), "the linear toe ends at 0.04045 -> 0.0031308");
  Chk(Near(rm::SrgbToLinear(0.02), 0.02 / 12.92, 1e-15), "below the knee the transfer is c / 12.92");
  Chk(Near(rm::LinearToSrgb(0.18), 0.46135612950044164, 1e-12), "linear 0.18 -> sRGB 0.46135613");
  Chk(Near(rm::LinearToSrgb(0.002), 0.002 * 12.92, 1e-15), "below 0.0031308 the transfer is 12.92 c");
  for (double c : {0.0, 0.001, 0.02, 0.2, 0.5, 0.9, 1.0})
    Chk(Near(rm::LinearToSrgb(rm::SrgbToLinear(c)), c, 1e-12), "round trip sRGB -> linear -> sRGB");
  Chk(NearRgb(rm::SrgbToLinear(rm::Rgb{0.5, 0.0, 1.0}), 0.21404114048223255, 0.0, 1.0, 1e-12), "applied per channel");
}

// D8-BLENDER-CMPNODE-0036 Exposure
D8_EXTRA_TEST(RenderRowCompExposure) {
  Chk(NearRgb(rm::Exposure({0.5, 0.25, 1.0}, 1.0), 1.0, 0.5, 2.0), "+1 stop doubles");
  Chk(NearRgb(rm::Exposure({0.5, 0.25, 1.0}, -2.0), 0.125, 0.0625, 0.25), "-2 stops quarters");
  Chk(NearRgb(rm::Exposure({0.5, 0.25, 1.0}, 3.0), 4.0, 2.0, 8.0), "+3 stops multiplies by 8");
  Chk(NearRgb(rm::Exposure({0.3, 0.6, 0.9}, 0.0), 0.3, 0.6, 0.9), "0 stops is the identity");
  Chk(NearRgb(rm::Exposure({0.2, 0.2, 0.2}, 0.5), 0.2 * 1.41421356237309505, 0.2 * 1.41421356237309505, 0.2 * 1.41421356237309505), "half a stop is a factor of sqrt 2");
}
