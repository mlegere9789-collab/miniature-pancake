// Solver node family: a Gene Pool source and a Galapagos-style evolutionary
// solver that drives it. See RunSolverNode() in FlowGraph.cpp for the
// actual genetic algorithm; the node definitions here only describe ports
// (both are Special-cased in Graph::Evaluate, so `eval` is unused).
#include "flow/FlowGraph.h"

namespace dino8::flow {

namespace {

PortDef In(std::string name, Kind k, Access acc = Access::Item, Value def = Value::Null(), bool optional = false) {
  PortDef p; p.name = name; p.nick = name; p.kind = k; p.access = acc; p.def = std::move(def); p.optional = optional; return p;
}
PortDef Out(std::string name, Kind k) { PortDef p; p.name = name; p.nick = name; p.kind = k; return p; }

}  // namespace

void RegisterSolverNodes() {
  Registry& r = Registry::Get();
  {
    NodeDef d;
    d.name = "Gene Pool";
    d.nick = "Genes";
    d.category = "Solver";
    d.subcategory = "Input";
    d.description = "A vector of Count numbers in [Min, Max], driven by hand or by an Evolutionary Solver wired to its output.";
    d.outputs = {Out("Genes", Kind::Number)};
    d.special = NodeDef::Special::GenePool;
    d.body_width = 160;
    r.Add(d);
  }
  {
    NodeDef d;
    d.name = "Evolutionary Solver";
    d.nick = "Solver";
    d.category = "Solver";
    d.subcategory = "Optimize";
    d.description =
        "Galapagos-style solver: searches the domain of a Gene Pool wired into Genes with a real-valued genetic "
        "algorithm (tournament selection, arithmetic crossover, random-offset mutation, elitism) to minimise or "
        "maximise whatever is wired into Fitness. Each candidate genome re-solves the whole graph, so Fitness must "
        "be wired from a node downstream of the same Gene Pool. Not a global-optimum guarantee - a best-effort "
        "search, same family as Grasshopper's Galapagos.";
    d.inputs = {
        In("Genes", Kind::Number, Access::Tree, Value::Null(), false),
        In("Fitness", Kind::Number, Access::Item, Value::Null(), false),
        In("Minimize", Kind::Boolean, Access::Item, Value::Boolean(true)),
        In("Population", Kind::Integer, Access::Item, Value::Integer(40)),
        In("Generations", Kind::Integer, Access::Item, Value::Integer(60)),
        In("Mutation Rate", Kind::Number, Access::Item, Value::Number(0.15)),
        In("Seed", Kind::Integer, Access::Item, Value::Integer(1)),
    };
    d.outputs = {
        Out("Best Genes", Kind::Number),
        Out("Best Fitness", Kind::Number),
        Out("Generations Run", Kind::Integer),
    };
    d.special = NodeDef::Special::Solver;
    r.Add(d);
  }
}

}  // namespace dino8::flow
