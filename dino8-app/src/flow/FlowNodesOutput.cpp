// Display and Output node categories, plus the registry bootstrap.
#include "flow/FlowGraph.h"

namespace dino8::flow {

// Defined in the sibling FlowNodes*.cpp translation units.
void RegisterCoreNodes();
void RegisterVectorNodes();
void RegisterCurveNodes();
void RegisterSurfaceNodes();
void RegisterSolverNodes();

namespace {

PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

void AddDisplay() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Custom Preview"; d.nick = "Preview"; d.category = "Display"; d.subcategory = "Preview";
    d.description = "Colours geometry for the viewport preview.";
    d.inputs = {In("Geometry", Kind::Any, Access::List), In("Colour", Kind::Colour, Access::Item, Value::ColourV(Colour{0.86f, 0.42f, 0.18f, 1.f}))};
    d.outputs = {Out("Geometry", Kind::Any)};
    d.eval = [](EvalContext& c) { c.OutList(0, c.List(0)); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Text Tag"; d.nick = "Tag"; d.category = "Display"; d.subcategory = "Preview";
    d.description = "Draws a text label at a point in the viewport.";
    d.inputs = {In("Location", Kind::Point, Access::Item, Value::Point(Point3d(0, 0, 0))), In("Text", Kind::Text, Access::Item, Value::Text("Tag"))};
    d.outputs = {};
    d.special = NodeDef::Special::TextTag;
    d.eval = [](EvalContext&) {};
    r.Add(d);
  }
}

void AddOutput() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Bake"; d.nick = "Bake"; d.category = "Output"; d.subcategory = "Document";
    d.description = "Bakes geometry into the document as real objects.";
    d.inputs = {In("Geometry", Kind::Any, Access::List), In("Layer", Kind::Text, Access::Item, Value::Text("")), In("Name", Kind::Text, Access::Item, Value::Text(""))};
    d.outputs = {};
    d.special = NodeDef::Special::Bake;
    d.eval = [](EvalContext&) {};
    r.Add(d);
  }
}

}  // namespace

void RegisterBuiltinNodes() {
  static bool done = false;
  if (done) return;
  done = true;
  RegisterCoreNodes();
  RegisterVectorNodes();
  RegisterCurveNodes();
  RegisterSurfaceNodes();
  RegisterSolverNodes();
  AddDisplay();
  AddOutput();
}

}  // namespace dino8::flow
