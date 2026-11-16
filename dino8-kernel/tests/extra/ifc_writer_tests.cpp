// IFC4 STEP writer (include/dino8/kernel/ifc_writer.h). The kernel test checks the text with a small line scanner and hand-derived
// quantities; the stand-alone Python oracle dino8-app/tests/ifc_oracle.py parses the same kind of file with a separate STEP
// tokenizer (run by the IFCEXPORT app section on a file written from real app components).
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "dino8/kernel/ifc_writer.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

struct Scan {
  std::map<int, std::string> entity_of;                  // id -> ENTITY NAME
  std::map<std::string, int> count;                      // ENTITY NAME -> number
  std::map<int, std::string> body;                       // id -> text between the outermost parentheses
  int dangling = 0;                                      // references to an id that is not defined
  int duplicate_ids = 0;
  bool framed = false;                                   // ISO-10303-21 header and END-ISO-10303-21 trailer present
  std::vector<std::pair<int, int>> refs;                 // (from, to)
};

Scan Parse(const std::string& text) {
  Scan s;
  s.framed = text.rfind("ISO-10303-21;\n", 0) == 0 && text.size() > 40 && text.compare(text.size() - 18, 18, "END-ISO-10303-21;\n") == 0;
  std::istringstream in(text);
  std::string line;
  bool data = false;
  while (std::getline(in, line)) {
    if (line == "DATA;") { data = true; continue; }
    if (line == "ENDSEC;") { data = false; continue; }
    if (!data || line.empty() || line[0] != '#') continue;
    size_t eq = line.find('=');
    const int id = std::atoi(line.c_str() + 1);
    size_t par = line.find('(', eq);
    const std::string name = line.substr(eq + 1, par - eq - 1);
    if (s.entity_of.count(id)) ++s.duplicate_ids;
    s.entity_of[id] = name;
    s.count[name]++;
    s.body[id] = line.substr(par);
  }
  for (auto& kv : s.body) {
    const std::string& b = kv.second;
    bool in_str = false;
    for (size_t i = 0; i < b.size(); ++i) {
      if (b[i] == '\'') in_str = !in_str;
      if (!in_str && b[i] == '#') s.refs.push_back({kv.first, std::atoi(b.c_str() + i + 1)});
    }
  }
  for (auto& r : s.refs)
    if (!s.entity_of.count(r.second)) ++s.dangling;
  return s;
}

// Value of the quantity entity named `name` among the entities the element `el` reaches through its IFCRELDEFINESBYPROPERTIES.
double QuantityOf(const Scan& s, int el, const std::string& name, bool* found) {
  *found = false;
  for (auto& kv : s.entity_of) {
    if (kv.second != "IFCRELDEFINESBYPROPERTIES") continue;
    const std::string& b = s.body.at(kv.first);
    bool related = false;
    int qset = -1;
    for (auto& r : s.refs)
      if (r.first == kv.first) {
        if (r.second == el) related = true;
        else qset = r.second;
      }
    if (!related || qset < 0 || s.entity_of.at(qset) != "IFCELEMENTQUANTITY") continue;
    (void)b;
    for (auto& r : s.refs) {
      if (r.first != qset) continue;
      const std::string& qb = s.body.at(r.second);
      if (qb.find("'" + name + "'") == 1 || qb.find("('" + name + "'") == 0) {
        // IFCQUANTITYxxx('Name',$,$,value,$): the fourth argument
        size_t p = qb.find(",$,$,");
        if (p == std::string::npos) continue;
        *found = true;
        return std::atof(qb.c_str() + p + 5);
      }
    }
  }
  return 0;
}

int FirstOf(const Scan& s, const std::string& entity, int nth = 0) {
  for (auto& kv : s.entity_of)
    if (kv.second == entity && nth-- == 0) return kv.first;
  return -1;
}

IfcModelSpec SampleModel() {
  IfcModelSpec m;
  m.project_name = "Test project";
  IfcStoreySpec l1, l2;
  l1.name = "Level 1"; l1.elevation = 0.0;
  l2.name = "Level 2"; l2.elevation = 3.0;
  IfcElementSpec wall;
  wall.kind = IfcElementKind::Wall; wall.name = "Wall A"; wall.origin = {2.5, 0, 0}; wall.x_dim = 5.0; wall.y_dim = 0.2; wall.depth = 2.4;
  wall.property_sets.push_back({"Pset_WallCommon", {{"IsExternal", "TRUE", false, 0}, {"FireRating", "REI 60", false, 0}, {"ThermalTransmittance", "", true, 0.34}}});
  IfcElementSpec slab;
  slab.kind = IfcElementKind::Slab; slab.name = "Slab 1"; slab.origin = {3, 2, 0}; slab.axis_z = {0, 0, -1}; slab.x_dim = 6.0; slab.y_dim = 4.0; slab.depth = 0.25;
  l1.elements = {wall, slab};
  IfcElementSpec col;
  col.kind = IfcElementKind::Column; col.name = "Column C1"; col.origin = {0, 0, 3.0}; col.x_dim = 0.4; col.y_dim = 0.3; col.depth = 3.0;
  IfcElementSpec beam;
  beam.kind = IfcElementKind::Beam; beam.name = "Beam B1"; beam.origin = {0, 0, 5.8}; beam.axis_z = {1, 0, 0}; beam.ref_x = {0, 0, 1}; beam.x_dim = 0.2; beam.y_dim = 0.4; beam.depth = 5.5;
  beam.extra_quantities.push_back({IfcQuantityKind::Volume, "NetVolume", 0.43});
  l2.elements = {col, beam};
  m.storeys = {l1, l2};
  return m;
}
}  // namespace

// STEP literals: reals always carry a decimal point, exponents are written 1.E-05, strings double their quotes and escape non-ASCII.
D8_EXTRA_TEST(IfcWriterLiterals) {
  d8test::Check(IfcReal(5.0) == "5." && IfcReal(0.25) == "0.25" && IfcReal(2.4) == "2.4" && IfcReal(-1.5) == "-1.5", "reals: 5. 0.25 2.4 -1.5");
  d8test::Check(IfcReal(0.0) == "0." && IfcReal(-0.0) == "0.", "zero and negative zero are 0.");
  d8test::Check(IfcReal(1e-5) == "1.E-05" && IfcReal(1.5e-7) == "1.5E-07" && IfcReal(2e20) == "2.E+20", "exponents: 1.E-05, 1.5E-07, 2.E+20");
  d8test::Check(IfcReal(0.1 + 0.2) == "0.3", "twelve significant digits hide the 0.30000000000000004 noise");
  d8test::Check(IfcString("Wall A") == "'Wall A'" && IfcString("it's") == "'it''s'" && IfcString("a\\b") == "'a\\\\b'", "strings: plain, doubled quote, doubled backslash");
  d8test::Check(IfcString("M\xC3\xBCller") == "'M\\X2\\00FC\\X0\\ller'" && IfcString("\xE2\x82\xAC") == "'\\X2\\20AC\\X0\\'", "non-ASCII (u-umlaut, euro sign) become \\X2\\....\\X0\\ escapes");
  const std::string alpha = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
  std::set<std::string> seen;
  bool ok = true;
  for (int i = 0; i < 2000; ++i) {
    const std::string g = IfcGuid("seed" + std::to_string(i));
    ok = ok && g.size() == 22 && g[0] >= '0' && g[0] <= '3';
    for (char c : g) ok = ok && alpha.find(c) != std::string::npos;
    seen.insert(g);
  }
  d8test::Check(ok, "2000 GlobalIds are 22 characters of the IFC alphabet with a first character 0..3 (128 bits)");
  d8test::Check(seen.size() == 2000 && IfcGuid("x") == IfcGuid("x"), "GlobalIds are distinct per seed and deterministic");
}

// A four-element building (wall, slab on level 1; column, beam on level 2). Hand counts of the entities:
//   fixed header: 5 context + 4 units + UnitAssignment + Project = 11; spatial: 2 placements, Site, Building, 2 RelAggregates = 6;
//   per storey 4 (point, axis, placement, storey) x 2 = 8, plus 1 RelAggregates for the storeys and 1 RelContained per storey = 2;
//   per element 5 placement + 4 profile + 4 solid/shape + 1 element = 14, plus quantities (n + set + rel):
//     wall 6 + 2 = 8, plus one property set (3 properties + set + rel = 5); slab 4 + 2 = 6; column 3 + 2 = 5; beam (3 + 1 extra) + 2 = 6.
//   Total 11 + 6 + 8 + 1 + 2 + 4 * 14 + 8 + 5 + 6 + 5 + 6 = 114 entities.
// Hand quantities: wall 5 x 0.2 x 2.4: Length 5, Width 0.2, Height 2.4, footprint 1.0, side 12.0, volume 2.4; slab 6 x 4 x 0.25:
// perimeter 20, area 24, volume 6.0; column 0.4 x 0.3 x 3: section 0.12, volume 0.36; beam 0.2 x 0.4 x 5.5: section 0.08, volume 0.44.
D8_EXTRA_TEST(IfcWriterBuildingStructure) {
  const std::string text = WriteIfc4(SampleModel());
  Scan s = Parse(text);
  d8test::Check(s.framed && text.find("FILE_SCHEMA(('IFC4'));") != std::string::npos, "ISO-10303-21 frame, IFC4 schema");
  d8test::Check(s.entity_of.size() == 114 && s.duplicate_ids == 0, "114 entities with unique ids");
  d8test::Check(s.entity_of.begin()->first == 1 && s.entity_of.rbegin()->first == 114, "ids run consecutively from #1 to #114");
  d8test::Check(s.dangling == 0 && !s.refs.empty(), "every #id used is defined (no dangling reference)");
  d8test::Check(s.count["IFCPROJECT"] == 1 && s.count["IFCSITE"] == 1 && s.count["IFCBUILDING"] == 1 && s.count["IFCBUILDINGSTOREY"] == 2, "one project, site, building and two storeys");
  d8test::Check(s.count["IFCWALL"] == 1 && s.count["IFCSLAB"] == 1 && s.count["IFCCOLUMN"] == 1 && s.count["IFCBEAM"] == 1, "one wall, slab, column, beam");
  d8test::Check(s.count["IFCEXTRUDEDAREASOLID"] == 4 && s.count["IFCRECTANGLEPROFILEDEF"] == 4 && s.count["IFCPRODUCTDEFINITIONSHAPE"] == 4 && s.count["IFCSHAPEREPRESENTATION"] == 4, "four extruded solids over four rectangle profiles");
  d8test::Check(s.count["IFCELEMENTQUANTITY"] == 4 && s.count["IFCQUANTITYLENGTH"] == 3 + 2 + 1 + 1 && s.count["IFCQUANTITYAREA"] == 2 + 1 + 1 + 1 && s.count["IFCQUANTITYVOLUME"] == 1 + 1 + 1 + 2, "quantity counts: length 3+2+1+1 = 7, area 2+1+1+1 = 5, volume 1+1+1+2 = 5");
  d8test::Check(s.count["IFCPROPERTYSET"] == 1 && s.count["IFCPROPERTYSINGLEVALUE"] == 3 && s.count["IFCRELDEFINESBYPROPERTIES"] == 5, "one property set with three values; five RelDefinesByProperties (4 quantity sets + 1 pset)");
  d8test::Check(s.count["IFCRELAGGREGATES"] == 3 && s.count["IFCRELCONTAINEDINSPATIALSTRUCTURE"] == 2, "three aggregations (project/site/building/storeys), two containment relations");
  d8test::Check(s.count["IFCSIUNIT"] == 4 && s.count["IFCUNITASSIGNMENT"] == 1, "SI units: length, area, volume, angle");

  const int wall = FirstOf(s, "IFCWALL"), slab = FirstOf(s, "IFCSLAB"), col = FirstOf(s, "IFCCOLUMN"), beam = FirstOf(s, "IFCBEAM");
  bool f = false;
  d8test::Check(Near(QuantityOf(s, wall, "Length", &f), 5.0) && f && Near(QuantityOf(s, wall, "Width", &f), 0.2) && Near(QuantityOf(s, wall, "Height", &f), 2.4), "wall Length 5, Width 0.2, Height 2.4");
  d8test::Check(Near(QuantityOf(s, wall, "GrossFootprintArea", &f), 1.0) && Near(QuantityOf(s, wall, "GrossSideArea", &f), 12.0) && Near(QuantityOf(s, wall, "GrossVolume", &f), 2.4), "wall footprint 1.0, side area 12.0, volume 2.4");
  d8test::Check(Near(QuantityOf(s, slab, "Perimeter", &f), 20.0) && Near(QuantityOf(s, slab, "GrossArea", &f), 24.0) && Near(QuantityOf(s, slab, "GrossVolume", &f), 6.0) && Near(QuantityOf(s, slab, "Depth", &f), 0.25), "slab perimeter 20, area 24, volume 6.0, depth 0.25");
  d8test::Check(Near(QuantityOf(s, col, "CrossSectionArea", &f), 0.12) && Near(QuantityOf(s, col, "GrossVolume", &f), 0.36) && Near(QuantityOf(s, col, "Length", &f), 3.0), "column section 0.12, volume 0.36, length 3");
  d8test::Check(Near(QuantityOf(s, beam, "CrossSectionArea", &f), 0.08) && Near(QuantityOf(s, beam, "GrossVolume", &f), 0.44) && Near(QuantityOf(s, beam, "NetVolume", &f), 0.43) && f, "beam section 0.08, gross volume 0.44, the extra NetVolume 0.43 is written");
  QuantityOf(s, wall, "NoSuchQuantity", &f);
  d8test::Check(!f, "a quantity that was not written is not found");
  d8test::Check(text == WriteIfc4(SampleModel()), "the same model always gives the same bytes");
  d8test::Check(text.find("IFCBUILDINGSTOREY(") != std::string::npos && text.find(",3.)\n") == std::string::npos && text.find("'Level 2'") != std::string::npos, "storey names are written");
}

// Containment and placement: the elements of each storey are contained in that storey only, element placements are relative to their
// storey (a world z of 3.0 on level 2 is written as 0.), and degenerate input (an empty model, a storey without elements, an
// axis parallel to the reference direction) still gives a consistent file.
D8_EXTRA_TEST(IfcWriterContainmentAndPlacement) {
  const std::string text = WriteIfc4(SampleModel());
  Scan s = Parse(text);
  const int storey1 = FirstOf(s, "IFCBUILDINGSTOREY", 0), storey2 = FirstOf(s, "IFCBUILDINGSTOREY", 1);
  int rel1 = -1, rel2 = -1;
  for (auto& kv : s.entity_of)
    if (kv.second == "IFCRELCONTAINEDINSPATIALSTRUCTURE") {
      int last = -1, n = 0;
      for (auto& r : s.refs)
        if (r.first == kv.first) { last = r.second; ++n; }
      if (last == storey1) rel1 = n - 1;
      if (last == storey2) rel2 = n - 1;
    }
  d8test::Check(rel1 == 2 && rel2 == 2, "level 1 contains 2 elements, level 2 contains 2 elements");
  // The column's point is (0, 0, 3.0 - 3.0) relative to level 2's placement; the wall's is (2.5, 0, 0).
  d8test::Check(text.find("=IFCCARTESIANPOINT((0.,0.,0.));") != std::string::npos && text.find("IFCCARTESIANPOINT((2.5,0.,0.))") != std::string::npos, "element placements are relative to the storey elevation");
  d8test::Check(text.find("IFCCARTESIANPOINT((0.,0.,3.))") != std::string::npos && text.find("IFCBUILDINGSTOREY('") != std::string::npos, "the storey itself is placed at its elevation 3.");
  d8test::Check(text.find("IFCDIRECTION((0.,0.,-1.))") != std::string::npos, "the slab extrudes downward from its top face");

  IfcModelSpec empty;
  Scan e = Parse(WriteIfc4(empty));
  d8test::Check(e.framed && e.dangling == 0 && e.count["IFCPROJECT"] == 1 && e.count["IFCBUILDINGSTOREY"] == 0 && e.count["IFCRELAGGREGATES"] == 2, "an empty model still has project, site, building and a closed reference graph");
  IfcModelSpec bare;
  bare.storeys.push_back({"Empty level", 1.5, {}});
  Scan b = Parse(WriteIfc4(bare));
  d8test::Check(b.dangling == 0 && b.count["IFCBUILDINGSTOREY"] == 1 && b.count["IFCRELCONTAINEDINSPATIALSTRUCTURE"] == 0, "a storey without elements has no containment relation");
  IfcModelSpec par = SampleModel();
  par.storeys[1].elements[0].ref_x = {0, 0, 1};  // parallel to the column axis
  Scan p = Parse(WriteIfc4(par));
  d8test::Check(p.dangling == 0 && p.entity_of.size() == 114, "a reference direction parallel to the axis is replaced, the file stays consistent");
  IfcModelSpec q = SampleModel();
  q.project_name = "O'Neil \xC3\xA9";
  q.storeys[0].elements[0].name = "Wall 'quoted'";
  const std::string qt = WriteIfc4(q);
  d8test::Check(qt.find("'O''Neil \\X2\\00E9\\X0\\'") != std::string::npos && qt.find("'Wall ''quoted'''") != std::string::npos && Parse(qt).dangling == 0, "quotes and non-ASCII in names are escaped and do not break the reference scan");
}
