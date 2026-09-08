// Dino Flow graph: node definitions (the component library), node
// instances with ports and per-port default values, wires, the solver
// (topological order, dirty propagation, Grasshopper-style list matching),
// graph undo, and JSON (.dflow) persistence.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flow/FlowData.h"

namespace dino8::app { class Document; class Application; }

namespace dino8::flow {

using NodeId = std::uint32_t;
constexpr NodeId kNoNode = 0;

// How an input receives its data: one item per evaluation, the whole
// branch as a list, or the entire tree.
enum class Access { Item, List, Tree };

struct PortDef {
  std::string name;
  std::string nick;
  std::string description;
  Kind kind = Kind::Any;
  Access access = Access::Item;
  Value def;               // default when nothing is wired and the user typed nothing
  bool optional = false;   // may be empty without flagging the node
};

class Node;
class Graph;

// Everything an evaluator needs for one invocation.
class EvalContext {
 public:
  EvalContext(Graph& g, Node& n, app::Document* doc) : graph(g), node(n), doc(doc) {}
  Graph& graph;
  Node& node;
  app::Document* doc;
  // Current item values (Access::Item inputs) / lists (List inputs).
  std::vector<Value> items;
  std::vector<std::vector<Value>> lists;
  std::vector<const Tree*> trees;
  std::vector<int> path;  // branch path the outputs go to
  int index = 0;          // item index within the branch
  int count = 1;          // items in this branch

  const Value& In(int i) const { return i < static_cast<int>(items.size()) ? items[i] : null_; }
  const std::vector<Value>& List(int i) const { return i < static_cast<int>(lists.size()) ? lists[i] : empty_; }
  const Tree* TreeIn(int i) const { return i < static_cast<int>(trees.size()) ? trees[i] : nullptr; }
  double Num(int i, double fallback = 0) const { double v; return In(i).AsNumber(v) ? v : fallback; }
  int Int(int i, int fallback = 0) const { double v; return In(i).AsNumber(v) ? static_cast<int>(std::llround(v)) : fallback; }
  bool Bool(int i, bool fallback = false) const { bool v; return In(i).AsBool(v) ? v : fallback; }
  std::string Text(int i) const { return In(i).AsText(); }
  Point3d Pt(int i, Point3d fallback = Point3d(0, 0, 0)) const { Point3d p; return In(i).AsPoint(p) ? p : fallback; }
  Vector3d Vec(int i, Vector3d fallback = Vector3d(0, 0, 1)) const { Vector3d v; return In(i).AsVector(v) ? v : fallback; }
  Plane PlaneIn(int i) const { Plane p; In(i).AsPlane(p); return p; }
  bool Has(int i) const { return !In(i).IsNull(); }

  // Outputs: one value per output port per invocation, or a list (which
  // gets its own sub-branch when several invocations happen).
  void Out(int i, const Value& v);
  void OutList(int i, const std::vector<Value>& v);
  void Fail(const std::string& msg) { if (error.empty()) error = msg; }
  void Warn(const std::string& msg) { if (warning.empty()) warning = msg; }
  std::string error, warning;

  std::vector<std::vector<std::pair<std::vector<int>, Value>>> outputs;  // per port: (path, value)
  std::vector<bool> output_is_list;

 private:
  Value null_;
  std::vector<Value> empty_;
};

using Evaluator = std::function<void(EvalContext&)>;

// A node type. `custom_ui` draws the node body in the editor (sliders,
// panels, pickers); `on_change` lets it mark itself dirty.
struct NodeDef {
  std::string name;         // "Circle"
  std::string nick;         // "Cir"
  std::string category;     // "Curve"
  std::string subcategory;  // "Primitive"
  std::string description;
  std::vector<PortDef> inputs;
  std::vector<PortDef> outputs;
  Evaluator eval;
  // Special node kinds the editor/solver treat specially.
  // GenePool: a Slider-like source that outputs `gene_count` numbers in
  // [Node::slider_min, Node::slider_max]; a solver drives it by writing
  // Node::gene_values directly. Solver: a Galapagos-style evolutionary
  // optimizer, see RunSolverNode() in FlowGraph.cpp.
  enum class Special { None, Slider, Panel, Toggle, Reference, Bake, Preview, TextTag, Expression, Colour, Plugin, GenePool, Solver } special = Special::None;
  float body_width = 0;  // custom body width hint (0 = auto)
};

class Registry {
 public:
  static Registry& Get();
  void Add(NodeDef def);
  const NodeDef* Find(const std::string& name) const;
  const std::vector<NodeDef>& All() const { return defs_; }
  std::vector<std::string> Categories() const;
  // Removes every node contributed by plug-ins (before reloading them).
  void RemoveCategory(const std::string& category);

 private:
  std::vector<NodeDef> defs_;
};

// Ensures the built-in component library is registered.
void RegisterBuiltinNodes();

struct Port {
  std::string name;
  Kind kind = Kind::Any;
  Value user_value;  // typed/edited default (input ports)
  bool has_user_value = false;
  Tree data;         // solved data (outputs; inputs cache the gathered tree)
};

class Node {
 public:
  NodeId id = kNoNode;
  std::string type;   // NodeDef::name
  const NodeDef* def = nullptr;
  float x = 0, y = 0;  // canvas position
  std::vector<Port> inputs;
  std::vector<Port> outputs;
  bool preview = true;
  bool enabled = true;
  bool selected = false;
  bool dirty = true;
  std::string error, warning;
  double solve_ms = 0;
  // Per-node state for special nodes.
  double slider_min = 0, slider_max = 10, slider_value = 5;
  bool slider_integer = false;
  std::string text;            // Panel text / expression / text tag / names
  bool toggle = false;
  std::vector<std::uint64_t> refs;  // referenced document object ids
  Colour colour{0.86f, 0.42f, 0.18f, 1.f};
  bool flatten_inputs = false;
  bool graft_inputs = false;
  bool baked_once = false;
  int width_hint = 0;
  // Gene Pool (Special::GenePool): how many numbers it outputs, and their
  // current values (driven by a solver, or left at the midpoint of
  // [slider_min, slider_max] until one runs). Solver (Special::Solver):
  // read-only progress left by the last run, for the editor to display.
  int gene_count = 5;
  std::vector<double> gene_values;
  double solver_best_fitness = 0;
  int solver_generations_run = 0;

  Tree& Output(int i) { return outputs[static_cast<size_t>(i)].data; }
};

struct Wire {
  NodeId from = kNoNode;
  int from_port = 0;
  NodeId to = kNoNode;
  int to_port = 0;
};

struct SolveStats {
  double total_ms = 0;
  int solved_nodes = 0;
  int errors = 0;
};

class Graph {
 public:
  Graph();
  Node* Add(const std::string& type, float x, float y);
  Node* Find(NodeId id);
  const Node* Find(NodeId id) const;
  bool Remove(NodeId id);
  std::vector<std::unique_ptr<Node>>& Nodes() { return nodes_; }
  const std::vector<std::unique_ptr<Node>>& Nodes() const { return nodes_; }
  std::vector<Wire>& Wires() { return wires_; }
  const std::vector<Wire>& Wires() const { return wires_; }
  // Connects (replacing an existing wire into the same input); false when
  // it would create a cycle or types are incompatible.
  bool Connect(NodeId from, int from_port, NodeId to, int to_port);
  void Disconnect(NodeId to, int to_port);
  void DisconnectAll(NodeId node);
  bool WouldCycle(NodeId from, NodeId to) const;
  const Wire* WireInto(NodeId to, int to_port) const;
  bool HasOutgoing(NodeId from, int port) const;
  void Clear();
  bool Empty() const { return nodes_.empty(); }

  // Solver.
  void MarkDirty(NodeId id);       // node and everything downstream
  void MarkAllDirty();
  std::vector<Node*> TopologicalOrder();
  SolveStats Solve(app::Document* doc);
  bool AnyDirty() const;
  // Reads what is currently gathered into `node`'s input `port` (the wired
  // source's solved output, or the port's literal/default value). Used by
  // the evolutionary solver to re-read its Fitness input after each
  // candidate genome is solved.
  void GatherPort(NodeId node, int port, Tree& out) { if (Node* n = Find(node)) GatherInput(*n, port, out); }
  void SetModified() { modified_ = true; ++revision_; }
  bool Modified() const { return modified_; }
  void ClearModified() { modified_ = false; }
  std::uint64_t Revision() const { return revision_; }

  // Persistence.
  std::string ToJson(const std::vector<NodeId>* only = nullptr) const;
  bool FromJson(const std::string& text, std::string& error, bool merge = false, float dx = 0, float dy = 0,
                std::vector<NodeId>* created = nullptr);
  bool SaveFile(const std::string& path, std::string& error) const;
  bool LoadFile(const std::string& path, std::string& error);

  // Undo of graph edits: call Snapshot() before a mutation.
  void Snapshot();
  bool Undo();
  bool Redo();
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }

  std::string path;  // file the graph was loaded from / saved to
  bool solver_enabled = true;
  SolveStats last_stats;

 private:
  void Evaluate(Node& n, app::Document* doc);
  void GatherInput(Node& n, int port, Tree& out);
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<Wire> wires_;
  NodeId next_id_ = 1;
  bool modified_ = false;
  std::uint64_t revision_ = 0;
  std::vector<std::string> undo_, redo_;
};

// Runs `def.eval` over the node's gathered inputs with Grasshopper list
// matching (longest list, branch-by-branch); fills the output trees.
void RunMatched(Graph& g, Node& n, app::Document* doc, const std::vector<Tree>& inputs);

// Runs the evolutionary solver node `n` in place: finds the Gene Pool node
// wired into its Genes input, searches its domain by genetic algorithm to
// minimise/maximise whatever is wired into the Fitness input (re-solving
// the graph once per candidate genome), and leaves the Gene Pool at the
// best genome found. See FlowNodesSolver.cpp for the node's port layout and
// the doc comment on the algorithm.
void RunSolverNode(Graph& g, Node& n, app::Document* doc);

}  // namespace dino8::flow
