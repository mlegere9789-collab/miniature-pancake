#include "arch/ArchComponents.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "app/Application.h"
#include "dino8/kernel/boolean.h"
#include "doc/SceneObject.h"
#include "util/json_mini.h"

namespace dino8::arch {

using app::ObjectKind;
using app::SceneObject;

const char* ArchTypeName(ArchType t) {
  switch (t) {
    case ArchType::Wall: return "Wall";
    case ArchType::Door: return "Door";
    case ArchType::Window: return "Window";
    case ArchType::Slab: return "Slab";
    case ArchType::Roof: return "Roof";
    case ArchType::Stair: return "Stair";
    case ArchType::Column: return "Column";
    case ArchType::Beam: return "Beam";
    case ArchType::Bolt: return "Bolt";
    case ArchType::Nut: return "Nut";
    case ArchType::Washer: return "Washer";
    case ArchType::IBeam: return "IBeam";
    case ArchType::Channel: return "Channel";
    case ArchType::Angle: return "Angle";
    case ArchType::Duct: return "Duct";
    case ArchType::Pipe: return "Pipe";
    case ArchType::Conduit: return "Conduit";
  }
  return "Wall";
}

bool ParseArchType(const std::string& s, ArchType& out) {
  static const std::pair<const char*, ArchType> kTable[] = {
      {"Wall", ArchType::Wall}, {"Door", ArchType::Door}, {"Window", ArchType::Window},
      {"Slab", ArchType::Slab}, {"Roof", ArchType::Roof}, {"Stair", ArchType::Stair},
      {"Column", ArchType::Column}, {"Beam", ArchType::Beam},
      {"Bolt", ArchType::Bolt}, {"Nut", ArchType::Nut}, {"Washer", ArchType::Washer},
      {"IBeam", ArchType::IBeam}, {"Channel", ArchType::Channel}, {"Angle", ArchType::Angle},
      {"Duct", ArchType::Duct}, {"Pipe", ArchType::Pipe}, {"Conduit", ArchType::Conduit},
  };
  for (const auto& [name, type] : kTable) if (s == name) { out = type; return true; }
  return false;
}

std::vector<std::string> ArchTypeNames() {
  return {"Wall", "Door", "Window", "Slab", "Roof", "Stair", "Column", "Beam",
          "Bolt", "Nut", "Washer", "IBeam", "Channel", "Angle", "Duct", "Pipe", "Conduit"};
}

// ---------------------------------------------------------------------------
// Standard-size tables (STARTER SETS - see the header's own doc comment on
// MechSizeRow: a handful of common sizes for testing/demonstration, not a
// real ISO/AISC standards database). Dimensions are in metres.
// ---------------------------------------------------------------------------

// Bolt: d0 = shank (nominal thread) diameter, d1 = head across-flats width
// (the hex head is approximated here as a circle circumscribing the
// across-flats hexagon, i.e. diameter d1/cos(30deg) ~= d1*1.1547, purely so
// Build() can use a simple cylinder rather than a hex prism - a real head
// is hexagonal, not round), d2 = head height. Approximate ISO 4014 values.
const std::vector<MechSizeRow>& BoltSizes() {
  static const std::vector<MechSizeRow> kRows = {
      {"M6", 0.006, 0.010, 0.004, 0}, {"M8", 0.008, 0.013, 0.0053, 0},
      {"M10", 0.010, 0.017, 0.0064, 0}, {"M12", 0.012, 0.019, 0.0075, 0},
  };
  return kRows;
}

// Nut: d0 = thread diameter (unused by Build(), kept for the BOM label),
// d1 = across-flats width (same circle approximation as Bolt's head),
// d2 = nut height/thickness. Approximate ISO 4032 values.
const std::vector<MechSizeRow>& NutSizes() {
  static const std::vector<MechSizeRow> kRows = {
      {"M6", 0.006, 0.010, 0.005, 0}, {"M8", 0.008, 0.013, 0.0065, 0},
      {"M10", 0.010, 0.017, 0.008, 0}, {"M12", 0.012, 0.019, 0.010, 0},
  };
  return kRows;
}

// Washer: d0 = inner (bore) diameter, d1 = outer diameter, d2 = thickness.
// Approximate ISO 7089 flat-washer values.
const std::vector<MechSizeRow>& WasherSizes() {
  static const std::vector<MechSizeRow> kRows = {
      {"M6", 0.0064, 0.0125, 0.0016, 0}, {"M8", 0.0084, 0.0170, 0.0016, 0},
      {"M10", 0.0105, 0.0210, 0.0020, 0}, {"M12", 0.0130, 0.0240, 0.0025, 0},
  };
  return kRows;
}

// IBeam: d0 = overall depth, d1 = flange width, d2 = flange thickness,
// d3 = web thickness. Plausible small-section dimensions for a parametric
// generator, not sourced from a specific AISC/Eurocode designation.
const std::vector<MechSizeRow>& IBeamSizes() {
  static const std::vector<MechSizeRow> kRows = {
      {"IB100", 0.100, 0.055, 0.0057, 0.0041}, {"IB150", 0.150, 0.075, 0.0070, 0.0044},
      {"IB200", 0.200, 0.100, 0.0085, 0.0056}, {"IB300", 0.300, 0.150, 0.0107, 0.0071},
  };
  return kRows;
}

// Channel (C-shape): d0 = overall depth, d1 = flange width, d2 = flange
// thickness, d3 = web thickness. Same "plausible, not sourced" caveat as
// IBeamSizes().
const std::vector<MechSizeRow>& ChannelSizes() {
  static const std::vector<MechSizeRow> kRows = {
      {"C100", 0.100, 0.050, 0.0055, 0.0050}, {"C150", 0.150, 0.065, 0.0064, 0.0060},
      {"C200", 0.200, 0.075, 0.0075, 0.0075},
  };
  return kRows;
}

// Angle (equal or unequal L-shape): d0 = leg 1 length, d1 = leg 2 length,
// d2 = thickness (both legs share one thickness, the common case). Same
// "plausible, not sourced" caveat.
const std::vector<MechSizeRow>& AngleSizes() {
  static const std::vector<MechSizeRow> kRows = {
      {"L50x50", 0.050, 0.050, 0.005, 0}, {"L75x75", 0.075, 0.075, 0.006, 0},
      {"L100x75", 0.100, 0.075, 0.008, 0},
  };
  return kRows;
}

}  // namespace dino8::arch

namespace dino8::arch {

std::vector<MechSizeRow> MechSizeTable(ArchType t) {
  switch (t) {
    case ArchType::Bolt: return BoltSizes();
    case ArchType::Nut: return NutSizes();
    case ArchType::Washer: return WasherSizes();
    case ArchType::IBeam: return IBeamSizes();
    case ArchType::Channel: return ChannelSizes();
    case ArchType::Angle: return AngleSizes();
    default: return {};
  }
}

std::vector<std::string> MechSizeNames(ArchType t) {
  std::vector<std::string> names;
  for (const MechSizeRow& r : MechSizeTable(t)) names.push_back(r.name);
  return names;
}

MechSizeRow MechSizeAt(ArchType t, int idx) {
  std::vector<MechSizeRow> table = MechSizeTable(t);
  if (table.empty()) return MechSizeRow{"", 0, 0, 0, 0};
  idx = std::clamp(idx, 0, static_cast<int>(table.size()) - 1);
  return table[idx];
}

// See the header's own doc comment: a simplified rule-of-thumb sizing
// heuristic, not a code-compliance calculation.
double MepDiameterFromFlow(double flow_m3_s, double velocity_m_s) {
  double v = velocity_m_s > 1e-9 ? velocity_m_s : 1.0;
  double area = std::max(0.0, flow_m3_s) / v;
  return std::sqrt(4.0 * area / M_PI);
}

}  // namespace dino8::arch

namespace dino8::arch {

// ---------------------------------------------------------------------------
// Geometry: every solid here is one closed watertight mesh box (or a small
// set of them, for Stair's treads / Roof's two slopes), all built from the
// same generalization of cmd_solids.cpp's axis-aligned CubeMesh() to an
// arbitrary local frame (e_i, e_j, e_k) - a box is a box whether its edges
// run along world X/Y/Z or along a wall's own length/thickness/height, so
// one indexing scheme (k*4 + j*2 + i, exactly CubeMesh's) covers every
// component type below.
// ---------------------------------------------------------------------------
namespace {

// ON_3dVector::Unitize() normalizes in place and returns whether it could
// (false for a zero-length vector) - every call site here wants a
// normalized copy of an already-nonzero vector, so wrap that pattern once.
Vector3d UnitOf(Vector3d v) { v.Unitize(); return v; }

kernel::Mesh OrientedBox(Point3d origin, Vector3d ei, Vector3d ej, Vector3d ek,
                          double i0, double i1, double j0, double j1, double k0, double k1) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  const double iv[2] = {i0, i1}, jv[2] = {j0, j1}, kv[2] = {k0, k1};
  for (int k = 0; k < 2; ++k)
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        Point3d p = origin + ei * iv[i] + ej * jv[j] + ek * kv[k];
        r.SetVertex(k * 4 + j * 2 + i, ON_3dPoint(p.x, p.y, p.z));
      }
  const int f[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (int i = 0; i < 6; ++i) r.SetQuad(i, f[i][0], f[i][1], f[i][2], f[i][3]);
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

// Right-handed local frame for a horizontal run from `p0` to `p1`: forward
// along the run, `up` fixed (world Z unless the component overrides it),
// right = up x forward so that (forward, right, up) matches OrientedBox's
// (ei, ej, ek) winding (see the comment on ArchComponent::normal).
struct RunFrame {
  Vector3d forward, right, up;
  double length;
};

RunFrame MakeRunFrame(Point3d p0, Point3d p1, Vector3d up) {
  RunFrame f;
  Vector3d d = p1 - p0;
  f.length = d.Length();
  f.forward = f.length > 1e-9 ? d / f.length : Vector3d(1, 0, 0);
  f.up = up.Length() > 1e-9 ? UnitOf(up) : Vector3d(0, 0, 1);
  f.right = kernel::Vector3d::CrossProduct(f.up, f.forward);
  if (f.right.Length() < 1e-9) f.right = Vector3d(0, 1, 0);  // forward parallel to up: arbitrary but stable
  else f.right = UnitOf(f.right);
  return f;
}

kernel::Mesh BuildWallShell(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  return OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -c.thickness / 2, c.thickness / 2, 0, c.height);
}

// A cutter box for a Door/Window opening hosted on `wall`, positioned at
// the point of `opening.p0` projected onto the wall's centreline, spanning
// `opening.width` along the wall and `z0..z1` vertically, oversized in the
// thickness direction so the boolean cleanly removes material through the
// whole wall regardless of small floating-point misalignment.
kernel::Mesh BuildOpeningCutter(const ArchComponent& wall, const ArchComponent& opening, double z0, double z1) {
  RunFrame f = MakeRunFrame(wall.p0, wall.p1, wall.normal);
  Vector3d rel = opening.p0 - wall.p0;
  double along = rel * f.forward;  // distance of the opening's insertion point from the wall's start
  double half_w = opening.width / 2.0;
  double over = std::max(wall.thickness, 0.1) * 2.0;
  return OrientedBox(wall.p0, f.forward, f.right, f.up, along - half_w, along + half_w, -over / 2, over / 2, z0, z1);
}

kernel::Mesh BuildColumn(const ArchComponent& c) {
  Vector3d up = c.normal.Length() > 1e-9 ? UnitOf(c.normal) : Vector3d(0, 0, 1);
  Vector3d ref = std::fabs(up.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d right = UnitOf(kernel::Vector3d::CrossProduct(up, ref));
  Vector3d forward = UnitOf(kernel::Vector3d::CrossProduct(right, up));
  return OrientedBox(c.p0, forward, right, up, -c.width / 2, c.width / 2, -c.thickness / 2, c.thickness / 2, 0, c.height);
}

kernel::Mesh BuildBeam(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  // The beam's cross-section (width x thickness) sits centred on the
  // p0-p1 line, spanning from -thickness/2 to +thickness/2 so the line
  // itself is the beam's own centreline (matches how Line/Pipe place
  // their profile in the rest of the app).
  return OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -c.width / 2, c.width / 2, -c.thickness / 2, c.thickness / 2);
}

// Frame for a Bolt/Nut/Washer's own axis (p0 = position, p1 = a point off
// p0 whose direction from p0 is the fastener's axis - only the direction
// matters, exactly the convention MakeRunFrame already uses for `up`
// elsewhere). Reuses BuildColumn's own "pick any stable perpendicular"
// construction, just with the fastener's own axis standing in for world Z.
Vector3d AxisOf(const ArchComponent& c) {
  Vector3d d = c.p1 - c.p0;
  return d.Length() > 1e-9 ? UnitOf(d) : Vector3d(0, 0, 1);
}

// Bolt: a cylindrical shank (diameter = table's shank diameter, length =
// c.height) plus a cylindrical head (diameter approximating the hex head's
// across-flats circumscribing circle, height = table's head height) sitting
// on top of it - see BoltSizes()'s own doc comment for why the head is
// round rather than a true hex prism.
kernel::Mesh BuildBolt(const ArchComponent& c) {
  MechSizeRow row = MechSizeAt(ArchType::Bolt, c.size_index);
  Vector3d axis = AxisOf(c);
  double length = std::max(c.height, 1e-6);
  kernel::Mesh shank = kernel::Mesh::Cylinder(c.p0, axis, row.d0 / 2.0, length);
  Point3d head_base = c.p0 + axis * length;
  double head_diam = row.d1 / std::cos(M_PI / 6.0);  // across-flats -> circumscribing circle
  kernel::Mesh head = kernel::Mesh::Cylinder(head_base, axis, head_diam / 2.0, row.d2);
  return kernel::BooleanCombine(shank, head, kernel::BooleanOp::Union);
}

// Nut: a single cylinder approximating the hex nut's across-flats
// circumscribing circle (same round-head approximation as BuildBolt), with
// no threaded bore (a solid approximation, not a functional threaded part).
kernel::Mesh BuildNut(const ArchComponent& c) {
  MechSizeRow row = MechSizeAt(ArchType::Nut, c.size_index);
  Vector3d axis = AxisOf(c);
  double diam = row.d1 / std::cos(M_PI / 6.0);
  return kernel::Mesh::Cylinder(c.p0, axis, diam / 2.0, row.d2);
}

// Washer: outer cylinder minus a concentric inner (bore) cylinder - a real
// hollow ring, unlike Bolt/Nut's solid round-head approximation, since a
// washer's bore is exactly the one feature that has to be there for the
// part to mean anything.
kernel::Mesh BuildWasher(const ArchComponent& c) {
  MechSizeRow row = MechSizeAt(ArchType::Washer, c.size_index);
  Vector3d axis = AxisOf(c);
  kernel::Mesh outer = kernel::Mesh::Cylinder(c.p0, axis, row.d1 / 2.0, row.d2);
  kernel::Mesh inner = kernel::Mesh::Cylinder(c.p0, axis, row.d0 / 2.0, row.d2);
  return kernel::BooleanCombine(outer, inner, kernel::BooleanOp::Difference);
}

// I-beam: union of a web box (centred, full depth, web-thickness wide) and
// two flange boxes (full width, flange-thickness deep) at the top and
// bottom of the depth - the standard "I" cross-section, extruded along
// p0-p1 exactly like BuildBeam extrudes its own rectangular one.
kernel::Mesh BuildIBeam(const ArchComponent& c) {
  MechSizeRow row = MechSizeAt(ArchType::IBeam, c.size_index);
  double depth = row.d0, flange_w = row.d1, flange_t = row.d2, web_t = row.d3;
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  kernel::Mesh web = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -web_t / 2, web_t / 2, -depth / 2 + flange_t, depth / 2 - flange_t);
  kernel::Mesh top = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -flange_w / 2, flange_w / 2, depth / 2 - flange_t, depth / 2);
  kernel::Mesh bot = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -flange_w / 2, flange_w / 2, -depth / 2, -depth / 2 + flange_t);
  kernel::Mesh m = kernel::BooleanCombine(web, top, kernel::BooleanOp::Union);
  return kernel::BooleanCombine(m, bot, kernel::BooleanOp::Union);
}

// Channel (C-shape): union of a web box (full depth, at one side of the
// width) and two flange boxes (full width, flange-thickness deep) at top
// and bottom - an I-beam whose web sits at one edge instead of the centre.
kernel::Mesh BuildChannel(const ArchComponent& c) {
  MechSizeRow row = MechSizeAt(ArchType::Channel, c.size_index);
  double depth = row.d0, flange_w = row.d1, flange_t = row.d2, web_t = row.d3;
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  kernel::Mesh web = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -flange_w / 2, -flange_w / 2 + web_t, -depth / 2 + flange_t, depth / 2 - flange_t);
  kernel::Mesh top = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -flange_w / 2, flange_w / 2, depth / 2 - flange_t, depth / 2);
  kernel::Mesh bot = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -flange_w / 2, flange_w / 2, -depth / 2, -depth / 2 + flange_t);
  kernel::Mesh m = kernel::BooleanCombine(web, top, kernel::BooleanOp::Union);
  return kernel::BooleanCombine(m, bot, kernel::BooleanOp::Union);
}

// Angle (L-shape): union of two boxes of the same `thickness`, one running
// the full length of leg 1 (vertical, in the up direction) and one running
// the full length of leg 2 (horizontal, in the right direction), sharing
// the corner at (right=0, up=0).
kernel::Mesh BuildAngle(const ArchComponent& c) {
  MechSizeRow row = MechSizeAt(ArchType::Angle, c.size_index);
  double leg1 = row.d0, leg2 = row.d1, t = row.d2;
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  kernel::Mesh vert = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, 0, t, 0, leg1);
  kernel::Mesh horiz = OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, 0, leg2, 0, t);
  return kernel::BooleanCombine(vert, horiz, kernel::BooleanOp::Union);
}

// Duct/Pipe/Conduit: a straight run extruded along p0-p1, exactly like
// BuildBeam, except Pipe/Conduit (and a round Duct) use a circular
// cross-section (kernel::Mesh::Cylinder) instead of a box. Built SOLID, not
// as a hollow shell/pipe wall - the same simplification BuildBeam/
// BuildColumn already make for a structural member, not a claim that a
// real duct or pipe has no wall thickness or bore.
kernel::Mesh BuildDuct(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  if (c.duct_round) return kernel::Mesh::Cylinder(c.p0, f.forward, c.diameter / 2.0, f.length);
  return OrientedBox(c.p0, f.forward, f.right, f.up, 0, f.length, -c.width / 2, c.width / 2, -c.thickness / 2, c.thickness / 2);
}

kernel::Mesh BuildRoundRun(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  return kernel::Mesh::Cylinder(c.p0, f.forward, c.diameter / 2.0, f.length);
}

kernel::Mesh BuildSlab(const ArchComponent& c) {
  Vector3d up = c.normal.Length() > 1e-9 ? UnitOf(c.normal) : Vector3d(0, 0, 1);
  Vector3d ref = std::fabs(up.z) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(1, 0, 0);
  Vector3d ei = UnitOf(kernel::Vector3d::CrossProduct(ref, up));
  Vector3d ej = UnitOf(kernel::Vector3d::CrossProduct(up, ei));
  Vector3d rel = c.p1 - c.p0;
  double iu = rel * ei, jv = rel * ej;
  return OrientedBox(c.p0, ei, ej, up, 0, iu, 0, jv, -c.thickness, 0);
}

// Roof over the footprint rectangle p0..p1 (in the world-XY plane): a
// gable (two sloped rectangles meeting at a ridge running parallel to the
// footprint's longer side, with triangular gable ends) or a shed (one
// sloped rectangle, full height on one eave, `ridge_height` on the other).
// Built as one open shell (a roof is a covering, not a solid Rhino has no
// equivalent primitive for either - see AUDIT.md) rather than a closed
// mesh, so BuildGeometry() below adds it as a Mesh object directly instead
// of routing it through OrientedBox/BooleanCombine.
kernel::Mesh BuildRoof(const ArchComponent& c) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  double x0 = std::min(c.p0.x, c.p1.x), x1 = std::max(c.p0.x, c.p1.x);
  double y0 = std::min(c.p0.y, c.p1.y), y1 = std::max(c.p0.y, c.p1.y);
  double z = c.p0.z;
  double xm = (x0 + x1) / 2.0;
  if (c.roof_style == 1) {
    // Shed: single slope from the y0 eave (z) up to the y1 eave (z + ridge_height).
    r.SetVertex(0, ON_3dPoint(x0, y0, z));
    r.SetVertex(1, ON_3dPoint(x1, y0, z));
    r.SetVertex(2, ON_3dPoint(x1, y1, z + c.ridge_height));
    r.SetVertex(3, ON_3dPoint(x0, y1, z + c.ridge_height));
    r.SetQuad(0, 0, 1, 2, 3);
  } else {
    // Gable: ridge line at x=xm, z+ridge_height, running the full y span;
    // two sloped rectangles (west eave to ridge, ridge to east eave) plus
    // two triangular gable ends closing the y0/y1 faces.
    r.SetVertex(0, ON_3dPoint(x0, y0, z));
    r.SetVertex(1, ON_3dPoint(x1, y0, z));
    r.SetVertex(2, ON_3dPoint(x1, y1, z));
    r.SetVertex(3, ON_3dPoint(x0, y1, z));
    r.SetVertex(4, ON_3dPoint(xm, y0, z + c.ridge_height));
    r.SetVertex(5, ON_3dPoint(xm, y1, z + c.ridge_height));
    r.SetQuad(0, 0, 4, 5, 3);   // west slope (x0 eave to ridge)
    r.SetQuad(1, 4, 1, 2, 5);   // east slope (ridge to x1 eave)
    r.SetTriangle(2, 0, 1, 4);  // south gable triangle
    r.SetTriangle(3, 3, 5, 2);  // north gable triangle
  }
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

std::vector<kernel::Mesh> BuildStairTreads(const ArchComponent& c) {
  RunFrame f = MakeRunFrame(c.p0, c.p1, c.normal);
  std::vector<kernel::Mesh> treads;
  for (int i = 0; i < std::max(1, c.step_count); ++i) {
    double along0 = i * c.run, along1 = (i + 1) * c.run;
    double z1 = (i + 1) * c.rise;
    treads.push_back(OrientedBox(c.p0, f.forward, f.right, f.up, along0, along1, -c.stair_width / 2, c.stair_width / 2, 0, z1));
  }
  return treads;
}

}  // namespace

// ---------------------------------------------------------------------------
// Building/rebuilding: places (or replaces) `c`'s objects in `doc` and
// returns their ids. Door/Window add nothing of their own - their only
// effect is the cut they contribute to their host Wall's shell.
// ---------------------------------------------------------------------------
namespace {

std::vector<ObjectId> BuildGeometry(Document& doc, ArchComponent& c, const std::vector<ArchComponent>& all) {
  std::vector<ObjectId> ids;
  auto add_mesh = [&](kernel::Mesh m, const std::string& name) {
    SceneObject o = SceneObject::MakeMesh(m);
    o.name = name;
    ids.push_back(doc.Add(std::move(o)));
  };
  switch (c.type) {
    case ArchType::Wall: {
      kernel::Mesh shell = BuildWallShell(c);
      for (const ArchComponent& other : all) {
        if (other.host != static_cast<ObjectId>(c.id)) continue;
        if (other.type != ArchType::Door && other.type != ArchType::Window) continue;
        double z0 = other.type == ArchType::Window ? other.sill_height : 0.0;
        double z1 = other.type == ArchType::Window ? other.sill_height + other.height : other.height;
        z1 = std::min(z1, c.height);
        if (z1 <= z0) continue;
        try {
          kernel::Mesh cutter = BuildOpeningCutter(c, other, z0, z1);
          shell = kernel::BooleanCombine(shell, cutter, kernel::BooleanOp::Difference);
        } catch (const std::exception&) {
          // Opening geometry degenerate (e.g. wall rebuilt shorter than the
          // opening's position) - leave the shell uncut rather than fail
          // the whole Wall rebuild.
        }
      }
      add_mesh(shell, "Wall");
      break;
    }
    case ArchType::Door:
    case ArchType::Window:
      break;  // no geometry of their own; see the host Wall's case above
    case ArchType::Slab: add_mesh(BuildSlab(c), "Slab"); break;
    case ArchType::Roof: add_mesh(BuildRoof(c), "Roof"); break;
    case ArchType::Column: add_mesh(BuildColumn(c), "Column"); break;
    case ArchType::Beam: add_mesh(BuildBeam(c), "Beam"); break;
    case ArchType::Stair: {
      int i = 0;
      for (kernel::Mesh& tread : BuildStairTreads(c)) add_mesh(tread, "Stair tread " + std::to_string(++i));
      break;
    }
    case ArchType::Bolt: add_mesh(BuildBolt(c), "Bolt"); break;
    case ArchType::Nut: add_mesh(BuildNut(c), "Nut"); break;
    case ArchType::Washer: add_mesh(BuildWasher(c), "Washer"); break;
    case ArchType::IBeam: add_mesh(BuildIBeam(c), "IBeam"); break;
    case ArchType::Channel: add_mesh(BuildChannel(c), "Channel"); break;
    case ArchType::Angle: add_mesh(BuildAngle(c), "Angle"); break;
    case ArchType::Duct: add_mesh(BuildDuct(c), "Duct"); break;
    case ArchType::Pipe: add_mesh(BuildRoundRun(c), "Pipe"); break;
    case ArchType::Conduit: add_mesh(BuildRoundRun(c), "Conduit"); break;
  }
  return ids;
}

}  // namespace

// ---------------------------------------------------------------------------
// Persistence (Document::UserText()["dino8.arch"] as a JSON array) -
// same trick as sketch/Constraints.cpp.
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kUserTextKey = "dino8.arch";
}

std::vector<ArchComponent> LoadArch(const Document& doc) {
  std::vector<ArchComponent> out;
  auto it = const_cast<Document&>(doc).UserText().find(kUserTextKey);
  if (it == const_cast<Document&>(doc).UserText().end() || it->second.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(it->second, root, err) || !root.IsArray()) return out;
  for (size_t i = 0; i < root.Size(); ++i) {
    const json::Value& v = root[i];
    ArchComponent c;
    c.id = static_cast<int>(v["id"].number);
    ArchType t;
    if (!ParseArchType(v["type"].AsString(), t)) continue;
    c.type = t;
    c.p0 = Point3d(v["p0x"].number, v["p0y"].number, v["p0z"].number);
    c.p1 = Point3d(v["p1x"].number, v["p1y"].number, v["p1z"].number);
    c.normal = Vector3d(v["nx"].number, v["ny"].number, v["nz"].number);
    if (c.normal.Length() < 1e-9) c.normal = Vector3d(0, 0, 1);
    c.height = v["height"].number;
    c.thickness = v["thickness"].number;
    c.width = v["width"].number;
    c.sill_height = v["sill"].number;
    c.ridge_height = v["ridge"].number;
    c.roof_style = static_cast<int>(v["roof_style"].number);
    // dino8.arch is stored in the document's user-text like dino8.constraints
    // and TableData - a crafted/corrupted .3dm can set "steps" to anything.
    // The interactive UI path (cmd_arch.cpp) clamps with std::max(1, ...);
    // this file-load path only floored it (via BuildStairTreads' own
    // std::max(1, c.step_count) at the loop bound), no ceiling - an absurd
    // value would still drive that many heap-allocated tread meshes/scene
    // objects. Clamp here too, matching the Table.cpp rows/cols fix.
    c.step_count = std::clamp(static_cast<int>(v["steps"].number), 1, 500);
    c.rise = v["rise"].number;
    c.run = v["run"].number;
    c.stair_width = v["stair_width"].number;
    // size_index/diameter/duct_round/flow_rate are new fields (Bolt/Nut/
    // Washer/IBeam/Channel/Angle/Duct/Pipe/Conduit): SaveArch always writes
    // them now, but a .3dm saved before this increment has no such keys in
    // its JSON, so operator[] returns a default (0-valued) Value for them -
    // diameter and duct_round need a non-zero fallback so an old file's
    // Wall/Beam/etc. records (which never use these fields anyway) don't
    // leave a freshly-loaded Duct/Pipe/Conduit with a zero-size cylinder.
    c.size_index = static_cast<int>(v["size_index"].number);
    double diam = v["diameter"].number;
    c.diameter = diam > 0 ? diam : 0.15;
    const json::Value& dr = v["duct_round"];
    c.duct_round = dr.type == json::Value::Type::Number ? static_cast<int>(dr.number) : 1;
    c.flow_rate = v["flow_rate"].number;
    c.host = static_cast<ObjectId>(v["host"].number);
    const json::Value& objs = v["objects"];
    for (size_t j = 0; j < objs.Size(); ++j) c.objects.push_back(static_cast<ObjectId>(objs[j].number));
    out.push_back(c);
  }
  return out;
}

void SaveArch(Document& doc, const std::vector<ArchComponent>& list) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < list.size(); ++i) {
    const ArchComponent& c = list[i];
    out << (i ? "," : "") << "{\"id\":" << c.id << ",\"type\":\"" << ArchTypeName(c.type) << "\""
        << ",\"p0x\":" << c.p0.x << ",\"p0y\":" << c.p0.y << ",\"p0z\":" << c.p0.z
        << ",\"p1x\":" << c.p1.x << ",\"p1y\":" << c.p1.y << ",\"p1z\":" << c.p1.z
        << ",\"nx\":" << c.normal.x << ",\"ny\":" << c.normal.y << ",\"nz\":" << c.normal.z
        << ",\"height\":" << c.height << ",\"thickness\":" << c.thickness << ",\"width\":" << c.width
        << ",\"sill\":" << c.sill_height << ",\"ridge\":" << c.ridge_height << ",\"roof_style\":" << c.roof_style
        << ",\"steps\":" << c.step_count << ",\"rise\":" << c.rise << ",\"run\":" << c.run << ",\"stair_width\":" << c.stair_width
        << ",\"size_index\":" << c.size_index << ",\"diameter\":" << c.diameter
        << ",\"duct_round\":" << c.duct_round << ",\"flow_rate\":" << c.flow_rate
        << ",\"host\":" << c.host << ",\"objects\":[";
    for (size_t j = 0; j < c.objects.size(); ++j) out << (j ? "," : "") << c.objects[j];
    out << "]}";
  }
  out << "]";
  doc.UserText()[kUserTextKey] = out.str();
}

namespace {
void RemoveObjects(Document& doc, const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) doc.Remove(id);
}
}  // namespace

int AddArchComponent(Document& doc, ArchComponent c) {
  std::vector<ArchComponent> list = LoadArch(doc);
  int next_id = 1;
  for (const ArchComponent& e : list) next_id = std::max(next_id, e.id + 1);
  c.id = next_id;
  list.push_back(c);
  // Doors/Windows contribute a cut to their host Wall but never own
  // geometry themselves; a freshly added Wall (or one whose opening list
  // just grew) needs its shell rebuilt with every current opening.
  ArchComponent& stored = list.back();
  stored.objects = BuildGeometry(doc, stored, list);
  if ((c.type == ArchType::Door || c.type == ArchType::Window) && c.host != 0) {
    for (ArchComponent& wall : list) {
      if (wall.id == static_cast<int>(c.host) && wall.type == ArchType::Wall) {
        RemoveObjects(doc, wall.objects);
        wall.objects = BuildGeometry(doc, wall, list);
      }
    }
  }
  SaveArch(doc, list);
  return stored.id;
}

bool DeleteArchComponent(Document& doc, int id) {
  std::vector<ArchComponent> list = LoadArch(doc);
  auto it = std::find_if(list.begin(), list.end(), [id](const ArchComponent& c) { return c.id == id; });
  if (it == list.end()) return false;
  RemoveObjects(doc, it->objects);
  const bool was_wall = it->type == ArchType::Wall;
  list.erase(it);
  if (was_wall) {
    // Any Door/Window hosted on the deleted wall no longer has a shell to
    // cut into; drop them too rather than leaving orphaned records.
    list.erase(std::remove_if(list.begin(), list.end(), [id](const ArchComponent& c) {
                 return (c.type == ArchType::Door || c.type == ArchType::Window) && c.host == static_cast<ObjectId>(id);
               }),
               list.end());
  }
  SaveArch(doc, list);
  return true;
}

bool FindArchComponent(const Document& doc, int id, ArchComponent& out) {
  for (const ArchComponent& c : LoadArch(doc)) if (c.id == id) { out = c; return true; }
  return false;
}

bool FindArchComponentByObject(const Document& doc, ObjectId object, ArchComponent& out) {
  for (const ArchComponent& c : LoadArch(doc)) {
    if (std::find(c.objects.begin(), c.objects.end(), object) != c.objects.end()) { out = c; return true; }
  }
  return false;
}

int RebuildArchComponent(Document& doc, const ArchComponent& updated) {
  std::vector<ArchComponent> list = LoadArch(doc);
  auto it = std::find_if(list.begin(), list.end(), [&](const ArchComponent& c) { return c.id == updated.id; });
  if (it == list.end()) return -1;
  RemoveObjects(doc, it->objects);
  *it = updated;
  it->objects = BuildGeometry(doc, *it, list);
  if (it->type == ArchType::Wall) {
    for (ArchComponent& other : list) {
      if (other.id == it->id) continue;
      if ((other.type == ArchType::Door || other.type == ArchType::Window) && other.host == static_cast<ObjectId>(it->id)) {
        // The wall's own rebuild already cut every current opening into
        // its fresh shell (BuildGeometry looks at the whole `list`), so
        // openings need no rebuild of their own here - they own no
        // geometry to begin with.
      }
    }
  }
  SaveArch(doc, list);
  return it->id;
}

void RebuildAll(Document& doc) {
  std::vector<ArchComponent> list = LoadArch(doc);
  for (ArchComponent& c : list) RemoveObjects(doc, c.objects);
  for (ArchComponent& c : list) c.objects = BuildGeometry(doc, c, list);
  SaveArch(doc, list);
}

}  // namespace dino8::arch
