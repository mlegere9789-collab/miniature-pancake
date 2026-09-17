// Params, Maths, Sets/Lists node categories.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

#include "flow/FlowGraph.h"

namespace dino8::flow {

namespace {

PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

void AddParams() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "Number Slider"; d.nick = "Slider"; d.category = "Params"; d.subcategory = "Input";
    d.description = "A draggable numeric value.";
    d.outputs = {Out("N", Kind::Number)};
    d.special = NodeDef::Special::Slider;
    d.body_width = 180;
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Panel"; d.nick = "Panel"; d.category = "Params"; d.subcategory = "Input";
    d.description = "Displays or supplies a block of text.";
    d.outputs = {Out("Text", Kind::Text)};
    d.special = NodeDef::Special::Panel;
    d.body_width = 160;
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Boolean Toggle"; d.nick = "Toggle"; d.category = "Params"; d.subcategory = "Input";
    d.description = "A True/False value.";
    d.outputs = {Out("B", Kind::Boolean)};
    d.special = NodeDef::Special::Toggle;
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Colour Swatch"; d.nick = "Colour"; d.category = "Params"; d.subcategory = "Input";
    d.description = "An RGB colour.";
    d.outputs = {Out("Colour", Kind::Colour)};
    d.special = NodeDef::Special::Colour;
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Construct Point"; d.nick = "Pt"; d.category = "Params"; d.subcategory = "Geometry";
    d.inputs = {In("X", Kind::Number, Access::Item, Value::Number(0)), In("Y", Kind::Number, Access::Item, Value::Number(0)), In("Z", Kind::Number, Access::Item, Value::Number(0))};
    d.outputs = {Out("Pt", Kind::Point)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Point(Point3d(c.Num(0), c.Num(1), c.Num(2)))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Referenced Point"; d.nick = "RefPt"; d.category = "Params"; d.subcategory = "Geometry";
    d.description = "References a Point object in the document by id.";
    d.outputs = {Out("Pt", Kind::Point)};
    d.special = NodeDef::Special::Reference;
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Referenced Curve"; d.nick = "RefCrv"; d.category = "Params"; d.subcategory = "Geometry";
    d.description = "References a Curve object in the document by id.";
    d.outputs = {Out("Crv", Kind::Curve)};
    d.special = NodeDef::Special::Reference;
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Referenced Surface"; d.nick = "RefSrf"; d.category = "Params"; d.subcategory = "Geometry";
    d.description = "References a Surface/Brep object in the document by id.";
    d.outputs = {Out("Srf", Kind::Surface)};
    d.special = NodeDef::Special::Reference;
    r.Add(d);
  }
}

void AddMaths() {
  Registry& r = Registry::Get();
  auto binop = [&](const char* name, const char* nick, std::function<double(double, double)> f) {
    NodeDef d; d.name = name; d.nick = nick; d.category = "Maths"; d.subcategory = "Operators";
    d.inputs = {In("A", Kind::Number, Access::Item, Value::Number(0)), In("B", Kind::Number, Access::Item, Value::Number(0))};
    d.outputs = {Out("Result", Kind::Number)};
    d.eval = [f](EvalContext& c) { c.Out(0, Value::Number(f(c.Num(0), c.Num(1)))); };
    r.Add(d);
  };
  binop("Addition", "A+B", [](double a, double b) { return a + b; });
  binop("Subtraction", "A-B", [](double a, double b) { return a - b; });
  binop("Multiplication", "A*B", [](double a, double b) { return a * b; });
  binop("Division", "A/B", [](double a, double b) { return b != 0 ? a / b : 0.0; });
  binop("Power", "A^B", [](double a, double b) { return std::pow(a, b); });
  binop("Modulus", "A%B", [](double a, double b) { return b != 0 ? std::fmod(a, b) : 0.0; });
  binop("Minimum", "Min", [](double a, double b) { return std::min(a, b); });
  binop("Maximum", "Max", [](double a, double b) { return std::max(a, b); });

  auto unary = [&](const char* name, const char* nick, std::function<double(double)> f) {
    NodeDef d; d.name = name; d.nick = nick; d.category = "Maths"; d.subcategory = "Trig";
    d.inputs = {In("X", Kind::Number, Access::Item, Value::Number(0))};
    d.outputs = {Out("Result", Kind::Number)};
    d.eval = [f](EvalContext& c) { c.Out(0, Value::Number(f(c.Num(0)))); };
    r.Add(d);
  };
  unary("Sine", "Sin", [](double x) { return std::sin(x); });
  unary("Cosine", "Cos", [](double x) { return std::cos(x); });
  unary("Tangent", "Tan", [](double x) { return std::tan(x); });
  unary("Absolute", "Abs", [](double x) { return std::fabs(x); });
  unary("Negate", "Neg", [](double x) { return -x; });
  unary("Sqrt", "Sqrt", [](double x) { return x >= 0 ? std::sqrt(x) : 0.0; });
  unary("Radians", "Rad", [](double x) { return x * ON_PI / 180.0; });
  unary("Degrees", "Deg", [](double x) { return x * 180.0 / ON_PI; });

  {
    NodeDef d; d.name = "Range"; d.nick = "Range"; d.category = "Maths"; d.subcategory = "Sequence";
    d.description = "A range of numbers.";
    d.inputs = {In("Domain Start", Kind::Number, Access::Item, Value::Number(0)), In("Domain End", Kind::Number, Access::Item, Value::Number(10)), In("Steps", Kind::Integer, Access::Item, Value::Integer(10))};
    d.outputs = {Out("Range", Kind::Number)};
    d.eval = [](EvalContext& c) {
      const double a = c.Num(0), b = c.Num(1);
      const int n = std::max(1, c.Int(2));
      std::vector<Value> out;
      for (int i = 0; i < n; ++i) out.push_back(Value::Number(a + (b - a) * i / n));
      c.OutList(0, out);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Series"; d.nick = "Series"; d.category = "Maths"; d.subcategory = "Sequence";
    d.inputs = {In("Start", Kind::Number, Access::Item, Value::Number(0)), In("Step", Kind::Number, Access::Item, Value::Number(1)), In("Count", Kind::Integer, Access::Item, Value::Integer(10))};
    d.outputs = {Out("Series", Kind::Number)};
    d.eval = [](EvalContext& c) {
      const double a = c.Num(0), s = c.Num(1);
      const int n = std::max(0, c.Int(2));
      std::vector<Value> out;
      for (int i = 0; i < n; ++i) out.push_back(Value::Number(a + s * i));
      c.OutList(0, out);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Random"; d.nick = "Rand"; d.category = "Maths"; d.subcategory = "Sequence";
    d.inputs = {In("Domain Start", Kind::Number, Access::Item, Value::Number(0)), In("Domain End", Kind::Number, Access::Item, Value::Number(1)),
                In("Count", Kind::Integer, Access::Item, Value::Integer(10)), In("Seed", Kind::Integer, Access::Item, Value::Integer(1))};
    d.outputs = {Out("Random", Kind::Number)};
    d.eval = [](EvalContext& c) {
      std::mt19937 rng(static_cast<unsigned>(c.Int(3)));
      std::uniform_real_distribution<double> dist(c.Num(0), c.Num(1));
      const int n = std::max(0, c.Int(2));
      std::vector<Value> out;
      for (int i = 0; i < n; ++i) out.push_back(Value::Number(dist(rng)));
      c.OutList(0, out);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Remap Numbers"; d.nick = "ReMap"; d.category = "Maths"; d.subcategory = "Domain";
    d.inputs = {In("Value", Kind::Number, Access::Item, Value::Number(0)), In("Source Min", Kind::Number, Access::Item, Value::Number(0)),
                In("Source Max", Kind::Number, Access::Item, Value::Number(1)), In("Target Min", Kind::Number, Access::Item, Value::Number(0)),
                In("Target Max", Kind::Number, Access::Item, Value::Number(1))};
    d.outputs = {Out("Mapped", Kind::Number)};
    d.eval = [](EvalContext& c) {
      const double v = c.Num(0), s0 = c.Num(1), s1 = c.Num(2), t0 = c.Num(3), t1 = c.Num(4);
      const double t = (s1 != s0) ? (v - s0) / (s1 - s0) : 0.0;
      c.Out(0, Value::Number(t0 + t * (t1 - t0)));
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Expression"; d.nick = "Expr"; d.category = "Maths"; d.subcategory = "Script";
    d.description = "Evaluates an arithmetic expression; x, y, z refer to A, B, C.";
    d.inputs = {In("x", Kind::Number, Access::Item, Value::Number(0)), In("y", Kind::Number, Access::Item, Value::Number(0)), In("z", Kind::Number, Access::Item, Value::Number(0))};
    d.outputs = {Out("Result", Kind::Number)};
    d.special = NodeDef::Special::Expression;
    r.Add(d);
  }
}

void AddSets() {
  Registry& r = Registry::Get();
  {
    NodeDef d; d.name = "List Item"; d.nick = "Item"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("List", Kind::Any, Access::List), In("Index", Kind::Integer, Access::Item, Value::Integer(0)), In("Wrap", Kind::Boolean, Access::Item, Value::Boolean(true))};
    d.outputs = {Out("Item", Kind::Any)};
    d.eval = [](EvalContext& c) {
      const std::vector<Value>& l = c.List(0);
      if (l.empty()) return;
      int i = c.Int(1);
      const bool wrap = c.Bool(2, true);
      if (wrap) { i %= static_cast<int>(l.size()); if (i < 0) i += static_cast<int>(l.size()); } else { i = std::clamp(i, 0, static_cast<int>(l.size()) - 1); }
      c.Out(0, l[static_cast<size_t>(i)]);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "List Length"; d.nick = "Len"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("List", Kind::Any, Access::List)};
    d.outputs = {Out("Length", Kind::Integer)};
    d.eval = [](EvalContext& c) { c.Out(0, Value::Integer(static_cast<long long>(c.List(0).size()))); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Reverse List"; d.nick = "Rev"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("List", Kind::Any, Access::List)};
    d.outputs = {Out("Reversed", Kind::Any)};
    d.eval = [](EvalContext& c) { std::vector<Value> l = c.List(0); std::reverse(l.begin(), l.end()); c.OutList(0, l); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Shift List"; d.nick = "Shift"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("List", Kind::Any, Access::List), In("Shift", Kind::Integer, Access::Item, Value::Integer(1)), In("Wrap", Kind::Boolean, Access::Item, Value::Boolean(true))};
    d.outputs = {Out("List", Kind::Any)};
    d.eval = [](EvalContext& c) {
      std::vector<Value> l = c.List(0);
      if (l.empty()) { c.OutList(0, l); return; }
      int s = c.Int(1) % static_cast<int>(l.size());
      if (s < 0) s += static_cast<int>(l.size());
      if (!c.Bool(2, true) && s == 0) { c.OutList(0, l); return; }
      std::rotate(l.begin(), l.begin() + (static_cast<long>(l.size()) - s), l.end());
      c.OutList(0, l);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Sort List"; d.nick = "Sort"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("Keys", Kind::Number, Access::List), In("Values", Kind::Any, Access::List, Value::Null(), true)};
    d.outputs = {Out("Sorted Keys", Kind::Number), Out("Sorted Values", Kind::Any)};
    d.eval = [](EvalContext& c) {
      std::vector<Value> keys = c.List(0);
      std::vector<Value> vals = c.List(1);
      std::vector<size_t> idx(keys.size());
      for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
      std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { double ka, kb; keys[a].AsNumber(ka); keys[b].AsNumber(kb); return ka < kb; });
      std::vector<Value> sk, sv;
      for (size_t i : idx) { sk.push_back(keys[i]); if (i < vals.size()) sv.push_back(vals[i]); }
      c.OutList(0, sk);
      c.OutList(1, sv);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Cull Pattern"; d.nick = "Cull"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("List", Kind::Any, Access::List), In("Pattern", Kind::Boolean, Access::List)};
    d.outputs = {Out("List", Kind::Any)};
    d.eval = [](EvalContext& c) {
      const std::vector<Value>& l = c.List(0);
      const std::vector<Value>& pat = c.List(1);
      std::vector<Value> out;
      if (pat.empty()) { c.OutList(0, l); return; }
      for (size_t i = 0; i < l.size(); ++i) { bool keep; pat[i % pat.size()].AsBool(keep); if (keep) out.push_back(l[i]); }
      c.OutList(0, out);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Partition List"; d.nick = "Partition"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("List", Kind::Any, Access::List), In("Size", Kind::Integer, Access::Item, Value::Integer(3))};
    d.outputs = {Out("Chunk", Kind::Any)};
    d.eval = [](EvalContext& c) {
      const std::vector<Value>& l = c.List(0);
      const int size = std::max(1, c.Int(1));
      int branch = 0;
      c.outputs.resize(1);
      for (size_t i = 0; i < l.size(); i += static_cast<size_t>(size)) {
        for (size_t k = i; k < l.size() && k < i + static_cast<size_t>(size); ++k) {
          std::vector<int> p = c.path; p.push_back(branch);
          c.outputs.resize(1);
          c.outputs[0].push_back({p, l[k]});
        }
        ++branch;
      }
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Flatten Tree"; d.nick = "Flatten"; d.category = "Sets"; d.subcategory = "Tree";
    d.inputs = {In("Tree", Kind::Any, Access::Tree)};
    d.outputs = {Out("List", Kind::Any)};
    d.eval = [](EvalContext& c) { if (const Tree* t = c.TreeIn(0)) c.OutList(0, t->AllItems()); };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Graft Tree"; d.nick = "Graft"; d.category = "Sets"; d.subcategory = "Tree";
    d.inputs = {In("Tree", Kind::Any, Access::Tree)};
    d.outputs = {Out("Tree", Kind::Any)};
    d.eval = [](EvalContext& c) {
      if (!c.TreeIn(0)) return;
      Tree g = c.TreeIn(0)->Grafted();
      c.outputs.resize(1);
      for (const Branch& b : g.branches) for (const Value& v : b.items) c.outputs[0].push_back({b.path, v});
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Simplify Tree"; d.nick = "Simplify"; d.category = "Sets"; d.subcategory = "Tree";
    d.inputs = {In("Tree", Kind::Any, Access::Tree)};
    d.outputs = {Out("Tree", Kind::Any)};
    d.eval = [](EvalContext& c) {
      if (!c.TreeIn(0)) return;
      Tree g = c.TreeIn(0)->Simplified();
      c.outputs.resize(1);
      for (const Branch& b : g.branches) for (const Value& v : b.items) c.outputs[0].push_back({b.path, v});
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Merge"; d.nick = "Merge"; d.category = "Sets"; d.subcategory = "List";
    d.inputs = {In("A", Kind::Any, Access::List), In("B", Kind::Any, Access::List)};
    d.outputs = {Out("Result", Kind::Any)};
    d.eval = [](EvalContext& c) {
      std::vector<Value> out = c.List(0);
      const std::vector<Value>& b = c.List(1);
      out.insert(out.end(), b.begin(), b.end());
      c.OutList(0, out);
    };
    r.Add(d);
  }
  {
    NodeDef d; d.name = "Entwine"; d.nick = "Entwine"; d.category = "Sets"; d.subcategory = "Tree";
    d.inputs = {In("A", Kind::Any, Access::List), In("B", Kind::Any, Access::List, Value::Null(), true)};
    d.outputs = {Out("Result", Kind::Any)};
    d.eval = [](EvalContext& c) {
      c.outputs.resize(1);
      const std::vector<Value>& a = c.List(0);
      const std::vector<Value>& b = c.List(1);
      std::vector<int> pa = c.path; pa.push_back(0);
      std::vector<int> pb = c.path; pb.push_back(1);
      for (const Value& v : a) c.outputs[0].push_back({pa, v});
      for (const Value& v : b) c.outputs[0].push_back({pb, v});
    };
    r.Add(d);
  }
}

}  // namespace

void RegisterCoreNodes() {
  AddParams();
  AddMaths();
  AddSets();
}

}  // namespace dino8::flow
