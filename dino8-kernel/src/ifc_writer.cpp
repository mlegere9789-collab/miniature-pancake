#include "dino8/kernel/ifc_writer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace dino8::kernel {

std::string IfcReal(double v) {
  if (!std::isfinite(v)) v = 0;
  if (v == 0) return "0.";  // also folds -0
  char b[64];
  std::snprintf(b, sizeof b, "%.12g", v);
  std::string s = b;
  const size_t e = s.find('e');
  std::string mant = e == std::string::npos ? s : s.substr(0, e);
  std::string exp = e == std::string::npos ? "" : s.substr(e + 1);
  if (mant.find('.') == std::string::npos) mant += ".";
  if (exp.empty()) return mant;
  // "1e-05" -> "1.E-05"; a leading '+' is dropped, as STEP exponents are "E+05" or "E-05" with an explicit sign allowed.
  return mant + "E" + exp;
}

std::string IfcString(const std::string& s) {
  std::string out = "'";
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      if (c == '\'') out += "''";
      else if (c == '\\') out += "\\\\";
      else if (c >= 0x20 && c != 0x7f) out += static_cast<char>(c);
      else out += '?';
      ++i;
      continue;
    }
    uint32_t cp = 0;
    int len = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else { out += '?'; ++i; continue; }
    if (i + len > s.size()) { out += '?'; break; }
    bool ok = true;
    for (int k = 1; k < len; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3F);
    }
    i += len;
    if (!ok || cp > 0xFFFF) { out += '?'; continue; }
    char b[16];
    std::snprintf(b, sizeof b, "\\X2\\%04X\\X0\\", static_cast<unsigned>(cp));
    out += b;
  }
  return out + "'";
}

std::string IfcGuid(const std::string& seed) {
  // Two 64-bit FNV-1a style streams with different offsets, finished with a splitmix64 round, give 128 deterministic bits.
  uint64_t h1 = 0xcbf29ce484222325ULL, h2 = 0x84222325cbf29ce4ULL;
  for (unsigned char c : seed) {
    h1 = (h1 ^ c) * 0x100000001b3ULL;
    h2 = (h2 ^ (c + 0x9eu)) * 0x100000001b3ULL;
  }
  auto mix = [](uint64_t z) {
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  };
  const uint64_t hi = mix(h1), lo = mix(h2 ^ hi);
  static const char* kAlpha = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
  // 128 bits = 2 + 21 * 6: the first character carries the top two bits.
  std::string out(22, '0');
  out[0] = kAlpha[(hi >> 62) & 0x3];
  for (int k = 1; k < 22; ++k) {
    const int shift = 128 - 2 - 6 * k;  // bit position of the lowest bit of this 6-bit group within the 128-bit value
    uint64_t v;
    if (shift >= 64) v = (hi >> (shift - 64)) & 0x3F;
    else if (shift + 6 <= 64) v = (lo >> shift) & 0x3F;
    else v = ((lo >> shift) | (hi << (64 - shift))) & 0x3F;
    out[k] = kAlpha[v];
  }
  return out;
}

std::vector<IfcQuantity> IfcBaseQuantities(const IfcElementSpec& e) {
  using K = IfcQuantityKind;
  const double a = e.x_dim * e.y_dim;
  switch (e.kind) {
    case IfcElementKind::Wall:
      return {{K::Length, "Length", e.x_dim}, {K::Length, "Width", e.y_dim}, {K::Length, "Height", e.depth}, {K::Area, "GrossFootprintArea", a},
              {K::Area, "GrossSideArea", e.x_dim * e.depth}, {K::Volume, "GrossVolume", a * e.depth}};
    case IfcElementKind::Slab:
      return {{K::Length, "Depth", e.depth}, {K::Length, "Perimeter", 2.0 * (e.x_dim + e.y_dim)}, {K::Area, "GrossArea", a}, {K::Volume, "GrossVolume", a * e.depth}};
    case IfcElementKind::Column:
    case IfcElementKind::Beam:
      return {{K::Length, "Length", e.depth}, {K::Area, "CrossSectionArea", a}, {K::Volume, "GrossVolume", a * e.depth}};
  }
  return {};
}

namespace {

struct Vec {
  double x, y, z;
};
Vec Norm(Vec v, Vec fallback) {
  const double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return l > 1e-12 ? Vec{v.x / l, v.y / l, v.z / l} : fallback;
}
double Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

const char* QtoName(IfcElementKind k) {
  switch (k) {
    case IfcElementKind::Wall: return "Qto_WallBaseQuantities";
    case IfcElementKind::Slab: return "Qto_SlabBaseQuantities";
    case IfcElementKind::Column: return "Qto_ColumnBaseQuantities";
    case IfcElementKind::Beam: return "Qto_BeamBaseQuantities";
  }
  return "Qto_BaseQuantities";
}
const char* EntityName(IfcElementKind k) {
  switch (k) {
    case IfcElementKind::Wall: return "IFCWALL";
    case IfcElementKind::Slab: return "IFCSLAB";
    case IfcElementKind::Column: return "IFCCOLUMN";
    case IfcElementKind::Beam: return "IFCBEAM";
  }
  return "IFCBUILDINGELEMENTPROXY";
}
const char* PredefinedType(IfcElementKind k) {
  switch (k) {
    case IfcElementKind::Wall: return ".STANDARD.";
    case IfcElementKind::Slab: return ".FLOOR.";
    case IfcElementKind::Column: return ".COLUMN.";
    case IfcElementKind::Beam: return ".BEAM.";
  }
  return "$";
}

class Out {
 public:
  // Appends one entity and returns its id.
  int Add(const std::string& body) {
    const int id = ++n_;
    text_ << "#" << id << "=" << body << ";\n";
    return id;
  }
  std::string Text() const { return text_.str(); }

 private:
  int n_ = 0;
  std::ostringstream text_;
};

std::string Ref(int id) { return "#" + std::to_string(id); }
std::string RefList(const std::vector<int>& ids) {
  std::string s = "(";
  for (size_t i = 0; i < ids.size(); ++i) s += (i ? "," : "") + Ref(ids[i]);
  return s + ")";
}
std::string Pt(double x, double y, double z) { return "IFCCARTESIANPOINT((" + IfcReal(x) + "," + IfcReal(y) + "," + IfcReal(z) + "))"; }
std::string Dir(double x, double y, double z) { return "IFCDIRECTION((" + IfcReal(x) + "," + IfcReal(y) + "," + IfcReal(z) + "))"; }

}  // namespace

std::string WriteIfc4(const IfcModelSpec& m) {
  Out o;
  const std::string seed = m.project_name + "|" + m.site_name + "|" + m.building_name;

  // Context and units.
  const int origin = o.Add(Pt(0, 0, 0));
  const int dz = o.Add(Dir(0, 0, 1));
  const int dx = o.Add(Dir(1, 0, 0));
  const int world = o.Add("IFCAXIS2PLACEMENT3D(" + Ref(origin) + "," + Ref(dz) + "," + Ref(dx) + ")");
  const int ctx = o.Add("IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05," + Ref(world) + ",$)");
  const int u_len = o.Add("IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)");
  const int u_area = o.Add("IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)");
  const int u_vol = o.Add("IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)");
  const int u_ang = o.Add("IFCSIUNIT(*,.PLANEANGLEUNIT.,$,.RADIAN.)");
  const int units = o.Add("IFCUNITASSIGNMENT(" + RefList({u_len, u_area, u_vol, u_ang}) + ")");
  const int project = o.Add("IFCPROJECT('" + IfcGuid(seed + "|project") + "',$," + IfcString(m.project_name) + ",$,$,$,$," + RefList({ctx}) + "," + Ref(units) + ")");

  // Spatial structure: site -> building -> storeys, each placed relative to its parent.
  const int site_pl = o.Add("IFCLOCALPLACEMENT($," + Ref(world) + ")");
  const int site = o.Add("IFCSITE('" + IfcGuid(seed + "|site") + "',$," + IfcString(m.site_name) + ",$,$," + Ref(site_pl) + ",$,$,.ELEMENT.,$,$,$,$,$)");
  const int bld_pl = o.Add("IFCLOCALPLACEMENT(" + Ref(site_pl) + "," + Ref(world) + ")");
  const int building = o.Add("IFCBUILDING('" + IfcGuid(seed + "|building") + "',$," + IfcString(m.building_name) + ",$,$," + Ref(bld_pl) + ",$,$,.ELEMENT.,$,$,$)");
  o.Add("IFCRELAGGREGATES('" + IfcGuid(seed + "|agg-site") + "',$,$,$," + Ref(project) + "," + RefList({site}) + ")");
  o.Add("IFCRELAGGREGATES('" + IfcGuid(seed + "|agg-building") + "',$,$,$," + Ref(site) + "," + RefList({building}) + ")");

  std::vector<int> storey_ids;
  int storey_index = 0;
  struct StoreyInfo {
    int id, placement;
    double elevation;
    const IfcStoreySpec* spec;
  };
  std::vector<StoreyInfo> infos;
  for (const IfcStoreySpec& st : m.storeys) {
    const int sp = o.Add(Pt(0, 0, st.elevation));
    const int ax = o.Add("IFCAXIS2PLACEMENT3D(" + Ref(sp) + "," + Ref(dz) + "," + Ref(dx) + ")");
    const int pl = o.Add("IFCLOCALPLACEMENT(" + Ref(bld_pl) + "," + Ref(ax) + ")");
    const int id = o.Add("IFCBUILDINGSTOREY('" + IfcGuid(seed + "|storey|" + std::to_string(storey_index)) + "',$," + IfcString(st.name) + ",$,$," + Ref(pl) + ",$,$,.ELEMENT.," + IfcReal(st.elevation) + ")");
    storey_ids.push_back(id);
    infos.push_back({id, pl, st.elevation, &st});
    ++storey_index;
  }
  if (!storey_ids.empty()) o.Add("IFCRELAGGREGATES('" + IfcGuid(seed + "|agg-storeys") + "',$,$,$," + Ref(building) + "," + RefList(storey_ids) + ")");

  // Elements.
  for (const StoreyInfo& si : infos) {
    std::vector<int> contained;
    int ei = 0;
    for (const IfcElementSpec& e : si.spec->elements) {
      const std::string eseed = seed + "|el|" + std::to_string(si.id) + "|" + std::to_string(ei++);
      const Vec z = Norm({e.axis_z.x, e.axis_z.y, e.axis_z.z}, {0, 0, 1});
      Vec x = {e.ref_x.x, e.ref_x.y, e.ref_x.z};
      const double d = Dot(x, z);
      x = Norm({x.x - d * z.x, x.y - d * z.y, x.z - d * z.z}, std::fabs(z.x) < 0.9 ? Vec{1, 0, 0} : Vec{0, 1, 0});
      if (std::fabs(Dot(x, z)) > 0.5) x = std::fabs(z.x) < 0.9 ? Vec{1, 0, 0} : Vec{0, 1, 0};  // ref_x was parallel to the axis

      // Placement relative to the storey (the storey sits at its elevation).
      const int loc = o.Add(Pt(e.origin.x, e.origin.y, e.origin.z - si.elevation));
      const int zd = o.Add(Dir(z.x, z.y, z.z));
      const int xd = o.Add(Dir(x.x, x.y, x.z));
      const int ax = o.Add("IFCAXIS2PLACEMENT3D(" + Ref(loc) + "," + Ref(zd) + "," + Ref(xd) + ")");
      const int pl = o.Add("IFCLOCALPLACEMENT(" + Ref(si.placement) + "," + Ref(ax) + ")");

      // Body: rectangle profile centred on the origin, extruded along local z.
      const int p2 = o.Add("IFCCARTESIANPOINT((0.,0.))");
      const int p2x = o.Add("IFCDIRECTION((1.,0.))");
      const int prof_ax = o.Add("IFCAXIS2PLACEMENT2D(" + Ref(p2) + "," + Ref(p2x) + ")");
      const int prof = o.Add("IFCRECTANGLEPROFILEDEF(.AREA.,$," + Ref(prof_ax) + "," + IfcReal(e.x_dim) + "," + IfcReal(e.y_dim) + ")");
      const int sol_ax = o.Add("IFCAXIS2PLACEMENT3D(" + Ref(origin) + ",$,$)");
      const int sol = o.Add("IFCEXTRUDEDAREASOLID(" + Ref(prof) + "," + Ref(sol_ax) + "," + Ref(dz) + "," + IfcReal(e.depth) + ")");
      const int rep = o.Add("IFCSHAPEREPRESENTATION(" + Ref(ctx) + ",'Body','SweptSolid'," + RefList({sol}) + ")");
      const int shape = o.Add("IFCPRODUCTDEFINITIONSHAPE($,$," + RefList({rep}) + ")");
      const int el = o.Add(std::string(EntityName(e.kind)) + "('" + IfcGuid(eseed) + "',$," + IfcString(e.name) + ",$,$," + Ref(pl) + "," + Ref(shape) + ",$," + PredefinedType(e.kind) + ")");
      contained.push_back(el);

      // Quantities: the derived base quantities, then the caller's extras, in one IfcElementQuantity.
      std::vector<IfcQuantity> qs = IfcBaseQuantities(e);
      qs.insert(qs.end(), e.extra_quantities.begin(), e.extra_quantities.end());
      std::vector<int> qids;
      for (const IfcQuantity& q : qs) {
        const char* ent = q.kind == IfcQuantityKind::Length ? "IFCQUANTITYLENGTH" : q.kind == IfcQuantityKind::Area ? "IFCQUANTITYAREA" : "IFCQUANTITYVOLUME";
        qids.push_back(o.Add(std::string(ent) + "(" + IfcString(q.name) + ",$,$," + IfcReal(q.value) + ",$)"));
      }
      const int qset = o.Add("IFCELEMENTQUANTITY('" + IfcGuid(eseed + "|qto") + "',$," + IfcString(QtoName(e.kind)) + ",$,$," + RefList(qids) + ")");
      o.Add("IFCRELDEFINESBYPROPERTIES('" + IfcGuid(eseed + "|qto-rel") + "',$,$,$," + RefList({el}) + "," + Ref(qset) + ")");

      int pi = 0;
      for (const IfcPropertySetSpec& ps : e.property_sets) {
        std::vector<int> props;
        for (const IfcProperty& p : ps.properties) {
          const std::string val = p.is_real ? "IFCREAL(" + IfcReal(p.real) + ")" : "IFCLABEL(" + IfcString(p.text) + ")";
          props.push_back(o.Add("IFCPROPERTYSINGLEVALUE(" + IfcString(p.name) + ",$," + val + ",$)"));
        }
        const int pset = o.Add("IFCPROPERTYSET('" + IfcGuid(eseed + "|pset|" + std::to_string(pi)) + "',$," + IfcString(ps.name) + ",$," + RefList(props) + ")");
        o.Add("IFCRELDEFINESBYPROPERTIES('" + IfcGuid(eseed + "|pset-rel|" + std::to_string(pi)) + "',$,$,$," + RefList({el}) + "," + Ref(pset) + ")");
        ++pi;
      }
    }
    if (!contained.empty()) o.Add("IFCRELCONTAINEDINSPATIALSTRUCTURE('" + IfcGuid(seed + "|contained|" + std::to_string(si.id)) + "',$,$,$," + RefList(contained) + "," + Ref(si.id) + ")");
  }

  std::string head;
  head += "ISO-10303-21;\nHEADER;\n";
  head += "FILE_DESCRIPTION(('ViewDefinition [CoordinationView]'),'2;1');\n";
  head += "FILE_NAME(" + IfcString(m.file_name) + "," + IfcString(m.timestamp) + ",(" + IfcString(m.author) + "),(''),'Dino 8','Dino 8','');\n";
  head += "FILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n";
  return head + o.Text() + "ENDSEC;\nEND-ISO-10303-21;\n";
}

}  // namespace dino8::kernel
