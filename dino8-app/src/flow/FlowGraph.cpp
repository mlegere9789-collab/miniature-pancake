#include "flow/FlowGraph.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "util/json_mini.h"

namespace dino8::flow {

// ---------------------------------------------------------------------
// EvalContext
// ---------------------------------------------------------------------
void EvalContext::Out(int i, const Value& v) {
  if (i >= static_cast<int>(outputs.size())) outputs.resize(static_cast<size_t>(i) + 1);
  outputs[static_cast<size_t>(i)].push_back({path, v});
}
void EvalContext::OutList(int i, const std::vector<Value>& vs) {
  if (i >= static_cast<int>(outputs.size())) outputs.resize(static_cast<size_t>(i) + 1);
  if (i >= static_cast<int>(output_is_list.size())) output_is_list.resize(static_cast<size_t>(i) + 1, false);
  output_is_list[static_cast<size_t>(i)] = true;
  for (const Value& v : vs) outputs[static_cast<size_t>(i)].push_back({path, v});
}

// ---------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------
Registry& Registry::Get() {
  static Registry r;
  return r;
}
void Registry::Add(NodeDef def) {
  for (NodeDef& d : defs_) if (d.name == def.name) { d = std::move(def); return; }
  defs_.push_back(std::move(def));
}
const NodeDef* Registry::Find(const std::string& name) const {
  for (const NodeDef& d : defs_) if (d.name == name) return &d;
  return nullptr;
}
std::vector<std::string> Registry::Categories() const {
  std::vector<std::string> cats;
  for (const NodeDef& d : defs_) if (std::find(cats.begin(), cats.end(), d.category) == cats.end()) cats.push_back(d.category);
  return cats;
}
void Registry::RemoveCategory(const std::string& category) {
  defs_.erase(std::remove_if(defs_.begin(), defs_.end(), [&](const NodeDef& d) { return d.category == category; }), defs_.end());
}

// ---------------------------------------------------------------------
// Graph: nodes / wires
// ---------------------------------------------------------------------
Graph::Graph() { RegisterBuiltinNodes(); }

Node* Graph::Add(const std::string& type, float x, float y) {
  const NodeDef* def = Registry::Get().Find(type);
  if (!def) return nullptr;
  auto n = std::make_unique<Node>();
  n->id = next_id_++;
  n->type = type;
  n->def = def;
  n->x = x; n->y = y;
  for (const PortDef& p : def->inputs) { Port port; port.name = p.name; port.kind = p.kind; port.user_value = p.def; n->inputs.push_back(port); }
  for (const PortDef& p : def->outputs) { Port port; port.name = p.name; port.kind = p.kind; n->outputs.push_back(port); }
  Node* raw = n.get();
  nodes_.push_back(std::move(n));
  SetModified();
  return raw;
}

Node* Graph::Find(NodeId id) { for (auto& n : nodes_) if (n->id == id) return n.get(); return nullptr; }
const Node* Graph::Find(NodeId id) const { for (const auto& n : nodes_) if (n->id == id) return n.get(); return nullptr; }

bool Graph::Remove(NodeId id) {
  DisconnectAll(id);
  const size_t before = nodes_.size();
  nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const std::unique_ptr<Node>& n) { return n->id == id; }), nodes_.end());
  if (nodes_.size() != before) { SetModified(); return true; }
  return false;
}

const Wire* Graph::WireInto(NodeId to, int to_port) const {
  for (const Wire& w : wires_) if (w.to == to && w.to_port == to_port) return &w;
  return nullptr;
}
bool Graph::HasOutgoing(NodeId from, int port) const {
  for (const Wire& w : wires_) if (w.from == from && w.from_port == port) return true;
  return false;
}

bool Graph::WouldCycle(NodeId from, NodeId to) const {
  if (from == to) return true;
  std::vector<NodeId> stack = {to};
  std::unordered_set<NodeId> seen;
  while (!stack.empty()) {
    NodeId cur = stack.back();
    stack.pop_back();
    if (!seen.insert(cur).second) continue;
    for (const Wire& w : wires_) {
      if (w.from == cur) {
        if (w.to == from) return true;
        stack.push_back(w.to);
      }
    }
  }
  return false;
}

bool Graph::Connect(NodeId from, int from_port, NodeId to, int to_port) {
  const Node* fn = Find(from);
  Node* tn = Find(to);
  if (!fn || !tn) return false;
  if (from_port < 0 || from_port >= static_cast<int>(fn->outputs.size())) return false;
  if (to_port < 0 || to_port >= static_cast<int>(tn->inputs.size())) return false;
  if (!KindCompatible(fn->outputs[static_cast<size_t>(from_port)].kind, tn->inputs[static_cast<size_t>(to_port)].kind)) return false;
  if (WouldCycle(from, to)) return false;
  Disconnect(to, to_port);
  wires_.push_back({from, from_port, to, to_port});
  MarkDirty(to);
  SetModified();
  return true;
}

void Graph::Disconnect(NodeId to, int to_port) {
  const size_t before = wires_.size();
  wires_.erase(std::remove_if(wires_.begin(), wires_.end(), [&](const Wire& w) { return w.to == to && w.to_port == to_port; }), wires_.end());
  if (wires_.size() != before) { MarkDirty(to); SetModified(); }
}

void Graph::DisconnectAll(NodeId node) {
  const size_t before = wires_.size();
  std::vector<NodeId> downstream;
  for (const Wire& w : wires_) if (w.from == node) downstream.push_back(w.to);
  wires_.erase(std::remove_if(wires_.begin(), wires_.end(), [&](const Wire& w) { return w.from == node || w.to == node; }), wires_.end());
  if (wires_.size() != before) { for (NodeId d : downstream) MarkDirty(d); SetModified(); }
}

void Graph::Clear() { nodes_.clear(); wires_.clear(); next_id_ = 1; path.clear(); SetModified(); }

// ---------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------
void Graph::MarkDirty(NodeId id) {
  std::vector<NodeId> stack = {id};
  std::unordered_set<NodeId> seen;
  while (!stack.empty()) {
    NodeId cur = stack.back();
    stack.pop_back();
    if (!seen.insert(cur).second) continue;
    if (Node* n = Find(cur)) n->dirty = true;
    for (const Wire& w : wires_) if (w.from == cur) stack.push_back(w.to);
  }
}
void Graph::MarkAllDirty() { for (auto& n : nodes_) n->dirty = true; }
bool Graph::AnyDirty() const { for (const auto& n : nodes_) if (n->dirty) return true; return false; }

std::vector<Node*> Graph::TopologicalOrder() {
  std::unordered_map<NodeId, int> indeg;
  for (const auto& n : nodes_) indeg[n->id] = 0;
  for (const Wire& w : wires_) indeg[w.to]++;
  std::vector<NodeId> queue;
  for (const auto& n : nodes_) if (indeg[n->id] == 0) queue.push_back(n->id);
  std::vector<Node*> order;
  size_t qi = 0;
  std::unordered_set<NodeId> done;
  while (qi < queue.size()) {
    NodeId cur = queue[qi++];
    if (!done.insert(cur).second) continue;
    if (Node* n = Find(cur)) order.push_back(n);
    for (const Wire& w : wires_) {
      if (w.from == cur) {
        if (--indeg[w.to] <= 0) queue.push_back(w.to);
      }
    }
  }
  // Any remaining nodes (cycle, shouldn't happen since Connect blocks
  // cycles, but be defensive) are appended in id order.
  for (const auto& n : nodes_) if (!done.count(n->id)) order.push_back(n.get());
  return order;
}

void Graph::GatherInput(Node& n, int port, Tree& out) {
  out.Clear();
  const Wire* w = WireInto(n.id, port);
  if (w) {
    if (Node* src = Find(w->from)) {
      if (w->from_port < static_cast<int>(src->outputs.size())) out = src->outputs[static_cast<size_t>(w->from_port)].data;
    }
  } else if (n.inputs[static_cast<size_t>(port)].has_user_value) {
    out = Tree::Single(n.inputs[static_cast<size_t>(port)].user_value);
  } else if (!n.inputs[static_cast<size_t>(port)].user_value.IsNull()) {
    out = Tree::Single(n.inputs[static_cast<size_t>(port)].user_value);
  }
}

void RunMatched(Graph& g, Node& n, app::Document* doc, const std::vector<Tree>& inputs) {
  if (!n.def || !n.def->eval) return;
  n.error.clear();
  n.warning.clear();
  for (Port& o : n.outputs) o.data.Clear();

  std::vector<std::vector<int>> paths;
  auto add_path = [&](const std::vector<int>& p) { if (std::find(paths.begin(), paths.end(), p) == paths.end()) paths.push_back(p); };
  for (size_t i = 0; i < inputs.size() && i < n.def->inputs.size(); ++i) {
    if (n.def->inputs[i].access != Access::Item) continue;
    for (const Branch& b : inputs[i].branches) if (!b.items.empty()) add_path(b.path);
  }
  if (paths.empty()) paths.push_back({0});

  const size_t out_count = n.def->outputs.size();
  std::vector<std::vector<std::pair<std::vector<int>, Value>>> gathered(out_count);

  for (const std::vector<int>& branch_path : paths) {
    size_t longest = 1;
    for (size_t i = 0; i < inputs.size() && i < n.def->inputs.size(); ++i) {
      if (n.def->inputs[i].access != Access::Item) continue;
      for (const Branch& b : inputs[i].branches) if (b.path == branch_path) longest = std::max(longest, b.items.size());
    }
    for (size_t item = 0; item < longest; ++item) {
      EvalContext ectx(g, n, doc);
      ectx.path = branch_path;
      ectx.index = static_cast<int>(item);
      ectx.count = static_cast<int>(longest);
      ectx.items.resize(inputs.size());
      ectx.lists.resize(inputs.size());
      ectx.trees.resize(inputs.size());
      for (size_t i = 0; i < inputs.size() && i < n.def->inputs.size(); ++i) {
        ectx.trees[i] = &inputs[i];
        const Access acc = n.def->inputs[i].access;
        if (acc == Access::Tree) continue;
        std::vector<Value> flat = inputs[i].AllItems();
        const Branch* match = nullptr;
        for (const Branch& b : inputs[i].branches) if (b.path == branch_path) { match = &b; break; }
        const std::vector<Value>& src = match ? match->items : flat;
        if (acc == Access::List) {
          ectx.lists[i] = src;
        } else if (!src.empty()) {
          ectx.items[i] = src[item % src.size()];
        } else if (!flat.empty()) {
          ectx.items[i] = flat[item % flat.size()];
        }
      }
      n.def->eval(ectx);
      if (!ectx.error.empty()) n.error = ectx.error;
      if (!ectx.warning.empty()) n.warning = ectx.warning;
      for (size_t o = 0; o < out_count && o < ectx.outputs.size(); ++o)
        for (auto& pv : ectx.outputs[o]) gathered[o].push_back(std::move(pv));
    }
  }
  for (size_t o = 0; o < out_count; ++o)
    for (auto& [path_, v] : gathered[o]) n.outputs[o].data.Add(v, path_);
}

// ---------------------------------------------------------------------
// Evolutionary solver ("Galapagos"-style)
// ---------------------------------------------------------------------
// Algorithm: a generational real-valued genetic algorithm - tournament
// selection (size 3), arithmetic (blend) crossover, uniform-random-offset
// mutation, single-individual elitism. This is a genuine, if simple,
// gradient-free evolutionary search: it does not know the objective's
// gradient or shape and explores by resampling and recombining candidate
// genomes, same family of algorithm as Grasshopper's Galapagos (which uses
// a similar generational GA). It is NOT CMA-ES or simulated annealing, and
// it does not guarantee a global optimum - like Galapagos, it is a
// best-effort search that tends to converge for smooth, low-dimensional
// objectives within its generation budget and can stagnate on hard
// (highly multimodal / discontinuous) ones. Convergence: elitism guarantees
// the best fitness seen is monotonically non-worsening across generations;
// there is no separate convergence test - it always runs the requested
// number of generations, since (as in Galapagos) "good enough" is left to
// the user to judge from the reported best fitness.
//
// Mechanics: each candidate genome is evaluated by writing it into the
// Gene Pool node this solver's Genes input is wired from, marking that
// node (and everything downstream of it) dirty, and re-running the whole
// graph solver - the ordinary dependency-driven Solve() - so whatever is
// wired into this node's own Fitness input reflects that genome. The
// solver node keeps itself un-dirtied for the duration (MarkDirty also
// marks the solver itself, since the Gene Pool feeds it) so the nested
// Solve() calls never recurse back into RunSolverNode. This makes the
// solver's cost O(population * generations) full-graph solves, which is
// fine for the small arithmetic graphs it is meant for but would be slow
// on a graph with expensive geometry nodes in the fitness path.
void RunSolverNode(Graph& g, Node& n, app::Document* doc) {
  n.error.clear();
  n.warning.clear();
  for (Port& o : n.outputs) o.data.Clear();

  const Wire* gw = g.WireInto(n.id, 0);
  Node* gp = gw ? g.Find(gw->from) : nullptr;
  if (!gp || !gp->def || gp->def->special != NodeDef::Special::GenePool) {
    n.error = "Genes must be wired directly from a Gene Pool node";
    return;
  }
  gp->gene_count = std::max(1, gp->gene_count);
  const int len = gp->gene_count;
  const double lo = std::min(gp->slider_min, gp->slider_max);
  const double hi = std::max(gp->slider_min, gp->slider_max);

  auto scalar_num = [&](int port, double fallback) {
    Tree t;
    g.GatherPort(n.id, port, t);
    const Value* v = t.First();
    double out;
    return (v && v->AsNumber(out)) ? out : fallback;
  };
  bool minimize = true;
  { Tree t; g.GatherPort(n.id, 2, t); const Value* v = t.First(); bool b; if (v && v->AsBool(b)) minimize = b; }
  const int population = std::max(4, static_cast<int>(std::llround(scalar_num(3, 40))));
  const int generations = std::max(1, static_cast<int>(std::llround(scalar_num(4, 60))));
  const double mutation_rate = std::clamp(scalar_num(5, 0.15), 0.0, 1.0);
  const unsigned seed = static_cast<unsigned>(std::llround(scalar_num(6, 1)));

  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> uni(0.0, 1.0);
  auto random_genome = [&] {
    std::vector<double> gm(static_cast<size_t>(len));
    for (double& x : gm) x = lo + uni(rng) * (hi - lo);
    return gm;
  };

  // Writes `genome` into the Gene Pool, forces a full re-solve, and reads
  // back whatever is wired into this node's Fitness input.
  auto evaluate = [&](const std::vector<double>& genome) -> double {
    gp->gene_values = genome;
    g.MarkDirty(gp->id);
    n.dirty = false;  // MarkDirty just re-dirtied us too (Genes wires into us); undo it so the nested Solve() below cannot recurse back into RunSolverNode.
    g.Solve(doc);
    Tree t;
    g.GatherPort(n.id, 1, t);
    const Value* v = t.First();
    double f;
    if (v && v->AsNumber(f)) return f;
    return minimize ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
  };

  struct Individual { std::vector<double> genome; double fitness = 0; };
  auto better = [minimize](double a, double b) { return minimize ? a < b : a > b; };

  std::vector<Individual> pop(static_cast<size_t>(population));
  for (Individual& ind : pop) { ind.genome = random_genome(); ind.fitness = evaluate(ind.genome); }
  Individual best = pop[0];
  for (const Individual& ind : pop) if (better(ind.fitness, best.fitness)) best = ind;

  std::uniform_int_distribution<int> pick(0, population - 1);
  auto tournament = [&]() -> const Individual& {
    const Individual* w = &pop[static_cast<size_t>(pick(rng))];
    for (int k = 0; k < 2; ++k) {
      const Individual& c = pop[static_cast<size_t>(pick(rng))];
      if (better(c.fitness, w->fitness)) w = &c;
    }
    return *w;
  };

  int generations_run = 0;
  for (int gen = 0; gen < generations; ++gen) {
    std::vector<Individual> next;
    next.reserve(pop.size());
    next.push_back(best);  // elitism: never lose the best genome found so far
    while (next.size() < pop.size()) {
      const Individual& pa = tournament();
      const Individual& pb = tournament();
      std::vector<double> child(static_cast<size_t>(len));
      for (int i = 0; i < len; ++i) {
        const double t = uni(rng);
        double gene = pa.genome[static_cast<size_t>(i)] * t + pb.genome[static_cast<size_t>(i)] * (1.0 - t);  // arithmetic (blend) crossover
        if (uni(rng) < mutation_rate) gene += (uni(rng) * 2.0 - 1.0) * (hi - lo) * 0.1;  // random-offset mutation
        child[static_cast<size_t>(i)] = std::clamp(gene, lo, hi);
      }
      Individual ind; ind.genome = std::move(child); ind.fitness = evaluate(ind.genome);
      if (better(ind.fitness, best.fitness)) best = ind;
      next.push_back(std::move(ind));
    }
    pop = std::move(next);
    ++generations_run;
  }

  // Leave the graph at the best genome found: set the solver's own outputs
  // first, then re-solve so anything wired downstream of *this* node also
  // sees the final answer within the same Solve() pass.
  gp->gene_values = best.genome;
  {
    std::vector<Value> items;
    for (double v : best.genome) items.push_back(Value::Number(v));
    n.outputs[0].data = Tree::FromList(items);
  }
  n.outputs[1].data = Tree::Single(Value::Number(best.fitness));
  n.outputs[2].data = Tree::Single(Value::Integer(generations_run));
  n.solver_best_fitness = best.fitness;
  n.solver_generations_run = generations_run;
  g.MarkDirty(gp->id);
  n.dirty = false;
  g.Solve(doc);
  n.dirty = false;
}

void Graph::Evaluate(Node& n, app::Document* doc) {
  if (!n.def) { n.dirty = false; return; }
  const auto t0 = std::chrono::steady_clock::now();
  n.error.clear();
  n.warning.clear();

  // Special node kinds bypass the generic evaluator plumbing.
  if (n.def->special == NodeDef::Special::Slider) {
    n.outputs[0].data = Tree::Single(n.slider_integer ? Value::Integer(static_cast<long long>(std::llround(n.slider_value))) : Value::Number(n.slider_value));
    n.dirty = false;
    return;
  }
  if (n.def->special == NodeDef::Special::Toggle) {
    n.outputs[0].data = Tree::Single(Value::Boolean(n.toggle));
    n.dirty = false;
    return;
  }
  if (n.def->special == NodeDef::Special::Panel) {
    n.outputs[0].data = Tree::Single(Value::Text(n.text));
    n.dirty = false;
    return;
  }
  if (n.def->special == NodeDef::Special::Colour) {
    n.outputs[0].data = Tree::Single(Value::ColourV(n.colour));
    n.dirty = false;
    return;
  }
  if (n.def->special == NodeDef::Special::GenePool) {
    n.gene_count = std::max(1, n.gene_count);
    if (n.gene_values.size() != static_cast<size_t>(n.gene_count)) n.gene_values.resize(static_cast<size_t>(n.gene_count), (n.slider_min + n.slider_max) * 0.5);
    std::vector<Value> items;
    for (double v : n.gene_values) items.push_back(Value::Number(v));
    n.outputs[0].data = Tree::FromList(items);
    n.dirty = false;
    return;
  }
  if (n.def->special == NodeDef::Special::Solver) {
    RunSolverNode(*this, n, doc);
    n.dirty = false;
    return;
  }

  std::vector<Tree> inputs(n.inputs.size());
  for (size_t i = 0; i < n.inputs.size(); ++i) GatherInput(n, static_cast<int>(i), inputs[i]);

  if (!n.def->eval) { n.dirty = false; return; }
  n.error.clear();
  n.warning.clear();
  for (Port& o : n.outputs) o.data.Clear();

  std::vector<std::vector<int>> paths;
  auto add_path = [&](const std::vector<int>& p) { if (std::find(paths.begin(), paths.end(), p) == paths.end()) paths.push_back(p); };
  bool any_item_input = false;
  for (size_t i = 0; i < inputs.size() && i < n.def->inputs.size(); ++i) {
    if (n.def->inputs[i].access != Access::Item) continue;
    any_item_input = true;
    for (const Branch& b : inputs[i].branches) if (!b.items.empty()) add_path(b.path);
  }
  if (paths.empty()) paths.push_back({0});
  (void)any_item_input;

  const size_t out_count = n.def->outputs.size();
  std::vector<std::vector<std::pair<std::vector<int>, Value>>> gathered(out_count);

  for (const std::vector<int>& branch_path : paths) {
    size_t longest = 1;
    for (size_t i = 0; i < inputs.size() && i < n.def->inputs.size(); ++i) {
      if (n.def->inputs[i].access != Access::Item) continue;
      for (const Branch& b : inputs[i].branches) if (b.path == branch_path) longest = std::max(longest, b.items.size());
    }
    for (size_t item = 0; item < longest; ++item) {
      EvalContext ectx(*this, n, doc);
      ectx.path = branch_path;
      ectx.index = static_cast<int>(item);
      ectx.count = static_cast<int>(longest);
      ectx.items.resize(inputs.size());
      ectx.lists.resize(inputs.size());
      ectx.trees.resize(inputs.size());
      for (size_t i = 0; i < inputs.size() && i < n.def->inputs.size(); ++i) {
        ectx.trees[i] = &inputs[i];
        const Access acc = n.def->inputs[i].access;
        if (acc == Access::Tree) continue;
        std::vector<Value> flat = inputs[i].AllItems();
        const Branch* match = nullptr;
        for (const Branch& b : inputs[i].branches) if (b.path == branch_path) { match = &b; break; }
        const std::vector<Value>& src = match ? match->items : flat;
        if (acc == Access::List) {
          ectx.lists[i] = src;
        } else if (!src.empty()) {
          ectx.items[i] = src[item % src.size()];
        } else if (!flat.empty()) {
          ectx.items[i] = flat[item % flat.size()];
        }
      }
      n.def->eval(ectx);
      if (!ectx.error.empty()) n.error = ectx.error;
      if (!ectx.warning.empty()) n.warning = ectx.warning;
      for (size_t o = 0; o < out_count && o < ectx.outputs.size(); ++o)
        for (auto& pv : ectx.outputs[o]) gathered[o].push_back(std::move(pv));
    }
  }
  for (size_t o = 0; o < out_count; ++o) {
    n.outputs[o].data.Clear();
    for (auto& [path_, v] : gathered[o]) n.outputs[o].data.Add(v, path_);
  }
  const auto t1 = std::chrono::steady_clock::now();
  n.solve_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  n.dirty = false;
}

SolveStats Graph::Solve(app::Document* doc) {
  SolveStats stats;
  if (!solver_enabled) return last_stats;
  const auto t0 = std::chrono::steady_clock::now();
  for (Node* n : TopologicalOrder()) {
    if (!n->enabled) { n->dirty = false; continue; }
    if (!n->dirty) continue;
    Evaluate(*n, doc);
    ++stats.solved_nodes;
    if (!n->error.empty()) ++stats.errors;
  }
  const auto t1 = std::chrono::steady_clock::now();
  stats.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  last_stats = stats;
  return stats;
}

// ---------------------------------------------------------------------
// JSON persistence
// ---------------------------------------------------------------------
namespace {

std::string JEsc(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out += c;
    }
  }
  return out;
}

void WriteValue(std::ostream& os, const Value& v) {
  os << "{\"kind\":\"" << KindName(v.kind) << "\"";
  switch (v.kind) {
    case Kind::Number: case Kind::Integer: case Kind::Boolean: os << ",\"num\":" << v.num; break;
    case Kind::Text: os << ",\"text\":\"" << JEsc(v.text) << "\""; break;
    case Kind::Point: case Kind::Vector: os << ",\"x\":" << v.point.x << ",\"y\":" << v.point.y << ",\"z\":" << v.point.z; break;
    case Kind::Plane:
      os << ",\"ox\":" << v.plane.origin.x << ",\"oy\":" << v.plane.origin.y << ",\"oz\":" << v.plane.origin.z
         << ",\"xx\":" << v.plane.x.x << ",\"xy\":" << v.plane.x.y << ",\"xz\":" << v.plane.x.z
         << ",\"yx\":" << v.plane.y.x << ",\"yy\":" << v.plane.y.y << ",\"yz\":" << v.plane.y.z;
      break;
    case Kind::Colour: os << ",\"r\":" << v.colour.r << ",\"g\":" << v.colour.g << ",\"b\":" << v.colour.b; break;
    default: break;  // geometry values are not persisted; they are re-solved on load
  }
  os << "}";
}

Value ReadValue(const json::Value& j) {
  Value v;
  v.kind = KindFromName(j["kind"].AsString("Null"));
  switch (v.kind) {
    case Kind::Number: case Kind::Integer: case Kind::Boolean: v.num = j["num"].type == json::Value::Type::Number ? j["num"].number : 0; break;
    case Kind::Text: v.text = j["text"].AsString(); break;
    case Kind::Point: case Kind::Vector: v.point = Point3d(j["x"].number, j["y"].number, j["z"].number); break;
    case Kind::Plane:
      v.plane.origin = Point3d(j["ox"].number, j["oy"].number, j["oz"].number);
      v.plane.x = Vector3d(j["xx"].number, j["xy"].number, j["xz"].number);
      v.plane.y = Vector3d(j["yx"].number, j["yy"].number, j["yz"].number);
      v.point = v.plane.origin;
      break;
    case Kind::Colour: v.colour = Colour{static_cast<float>(j["r"].number), static_cast<float>(j["g"].number), static_cast<float>(j["b"].number), 1.f}; break;
    default: break;
  }
  return v;
}

}  // namespace

std::string Graph::ToJson(const std::vector<NodeId>* only) const {
  std::ostringstream os;
  os << "{\"format\":\"dflow1\",\"nodes\":[";
  bool first = true;
  for (const auto& n : nodes_) {
    if (only && std::find(only->begin(), only->end(), n->id) == only->end()) continue;
    if (!first) os << ","; first = false;
    os << "{\"id\":" << n->id << ",\"type\":\"" << JEsc(n->type) << "\",\"x\":" << n->x << ",\"y\":" << n->y
       << ",\"enabled\":" << (n->enabled ? "true" : "false") << ",\"preview\":" << (n->preview ? "true" : "false")
       << ",\"slider_min\":" << n->slider_min << ",\"slider_max\":" << n->slider_max << ",\"slider_value\":" << n->slider_value
       << ",\"slider_integer\":" << (n->slider_integer ? "true" : "false")
       << ",\"toggle\":" << (n->toggle ? "true" : "false")
       << ",\"text\":\"" << JEsc(n->text) << "\""
       << ",\"colour\":[" << n->colour.r << "," << n->colour.g << "," << n->colour.b << "]"
       << ",\"gene_count\":" << n->gene_count << ",\"gene_values\":[";
    for (size_t i = 0; i < n->gene_values.size(); ++i) os << (i ? "," : "") << n->gene_values[i];
    os << "],\"refs\":[";
    for (size_t i = 0; i < n->refs.size(); ++i) os << (i ? "," : "") << n->refs[i];
    os << "],\"inputs\":[";
    for (size_t i = 0; i < n->inputs.size(); ++i) {
      if (i) os << ",";
      os << "{\"has\":" << (n->inputs[i].has_user_value ? "true" : "false") << ",\"value\":";
      WriteValue(os, n->inputs[i].user_value);
      os << "}";
    }
    os << "]}";
  }
  os << "],\"wires\":[";
  bool wfirst = true;
  for (const Wire& w : wires_) {
    if (only && (std::find(only->begin(), only->end(), w.from) == only->end() || std::find(only->begin(), only->end(), w.to) == only->end())) continue;
    if (!wfirst) os << ","; wfirst = false;
    os << "{\"from\":" << w.from << ",\"fp\":" << w.from_port << ",\"to\":" << w.to << ",\"tp\":" << w.to_port << "}";
  }
  os << "]}";
  return os.str();
}

bool Graph::FromJson(const std::string& text, std::string& error, bool merge, float dx, float dy, std::vector<NodeId>* created) {
  json::Value root;
  if (!json::Parse(text, root, error) || !root.IsObject()) { if (error.empty()) error = "invalid JSON"; return false; }
  if (!merge) Clear();
  std::unordered_map<std::uint64_t, NodeId> remap;
  const json::Value& jnodes = root["nodes"];
  for (size_t i = 0; i < jnodes.Size(); ++i) {
    const json::Value& jn = jnodes[i];
    const std::string type = jn["type"].AsString();
    Node* n = Add(type, static_cast<float>(jn["x"].number) + dx, static_cast<float>(jn["y"].number) + dy);
    if (!n) { error = "unknown node type: " + type; continue; }
    remap[static_cast<std::uint64_t>(jn["id"].number)] = n->id;
    n->enabled = jn["enabled"].type != json::Value::Type::Bool || jn["enabled"].boolean;
    n->preview = jn["preview"].type != json::Value::Type::Bool || jn["preview"].boolean;
    if (jn["slider_min"].type == json::Value::Type::Number) n->slider_min = jn["slider_min"].number;
    if (jn["slider_max"].type == json::Value::Type::Number) n->slider_max = jn["slider_max"].number;
    if (jn["slider_value"].type == json::Value::Type::Number) n->slider_value = jn["slider_value"].number;
    n->slider_integer = jn["slider_integer"].type == json::Value::Type::Bool && jn["slider_integer"].boolean;
    n->toggle = jn["toggle"].type == json::Value::Type::Bool && jn["toggle"].boolean;
    n->text = jn["text"].AsString();
    const json::Value& jc = jn["colour"];
    if (jc.IsArray() && jc.Size() >= 3) n->colour = Colour{static_cast<float>(jc[0].number), static_cast<float>(jc[1].number), static_cast<float>(jc[2].number), 1.f};
    if (jn["gene_count"].type == json::Value::Type::Number) n->gene_count = static_cast<int>(jn["gene_count"].number);
    const json::Value& jgenes = jn["gene_values"];
    for (size_t gv = 0; gv < jgenes.Size(); ++gv) n->gene_values.push_back(jgenes[gv].number);
    const json::Value& jrefs = jn["refs"];
    for (size_t r = 0; r < jrefs.Size(); ++r) n->refs.push_back(static_cast<std::uint64_t>(jrefs[r].number));
    const json::Value& jins = jn["inputs"];
    for (size_t p = 0; p < jins.Size() && p < n->inputs.size(); ++p) {
      const json::Value& jp = jins[p];
      if (jp["has"].type == json::Value::Type::Bool && jp["has"].boolean) {
        n->inputs[p].has_user_value = true;
        n->inputs[p].user_value = ReadValue(jp["value"]);
      }
    }
    if (created) created->push_back(n->id);
  }
  const json::Value& jwires = root["wires"];
  for (size_t i = 0; i < jwires.Size(); ++i) {
    const json::Value& jw = jwires[i];
    auto fi = remap.find(static_cast<std::uint64_t>(jw["from"].number));
    auto ti = remap.find(static_cast<std::uint64_t>(jw["to"].number));
    if (fi == remap.end() || ti == remap.end()) continue;
    Connect(fi->second, static_cast<int>(jw["fp"].number), ti->second, static_cast<int>(jw["tp"].number));
  }
  MarkAllDirty();
  SetModified();
  return true;
}

bool Graph::SaveFile(const std::string& file_path, std::string& error) const {
  std::ofstream out(file_path, std::ios::binary);
  if (!out) { error = "could not open " + file_path + " for writing"; return false; }
  out << ToJson();
  return true;
}

bool Graph::LoadFile(const std::string& file_path, std::string& error) {
  std::ifstream in(file_path, std::ios::binary);
  if (!in) { error = "could not open " + file_path; return false; }
  std::ostringstream ss;
  ss << in.rdbuf();
  const bool ok = FromJson(ss.str(), error, false);
  if (ok) path = file_path;
  return ok;
}

// ---------------------------------------------------------------------
// Graph undo (whole-graph JSON snapshots, matching the document's own
// simple-and-robust snapshot approach).
// ---------------------------------------------------------------------
void Graph::Snapshot() {
  undo_.push_back(ToJson());
  if (undo_.size() > 100) undo_.erase(undo_.begin());
  redo_.clear();
}
bool Graph::Undo() {
  if (undo_.empty()) return false;
  redo_.push_back(ToJson());
  const std::string snap = undo_.back();
  undo_.pop_back();
  std::string err;
  Clear();
  FromJson(snap, err, false);
  return true;
}
bool Graph::Redo() {
  if (redo_.empty()) return false;
  undo_.push_back(ToJson());
  const std::string snap = redo_.back();
  redo_.pop_back();
  std::string err;
  Clear();
  FromJson(snap, err, false);
  return true;
}

}  // namespace dino8::flow
