#!/usr/bin/env python3
"""Deterministic Grasshopper case corpus (Phase B). Cases are authored by us from closed forms and sweeps (CLEANROOM G1).

Case shape:
  {"id": "GH-SERI-0001", "family": "sets", "op": "graph",
   "args": {"nodes": {"n": {"c": "<component name>", "sub": "<sub-category, optional>", "in": [<input0>, <input1>, ...]}},
            "wires": [["src", out_index, "dst", in_index], ...],
            "outs": [["node", out_index], ...]}}
  input spec: null (leave default) | [numbers or strings] (one branch {0}) | {"0;1": [..], "2": [..]} (explicit branches)
Component names are public identifiers only. Inputs and outputs are addressed by position.
"""
import json, sys

cases = []


def add(prefix, family, nodes, wires, outs):
    n = sum(1 for c in cases if c["id"].startswith(prefix + "-")) + 1
    cases.append({"id": "%s-%04d" % (prefix, n), "family": family, "op": "graph",
                  "args": {"nodes": nodes, "wires": wires, "outs": outs}})


def one(prefix, family, comp, sub, ins, nout=1):
    add(prefix, family, {"n": {"c": comp, "sub": sub, "in": ins}}, [], [["n", i] for i in range(nout)])


# --- sets: Series, Range
for start, step, count in [(0, 1, 5), (1, 2, 3), (5, -1, 4), (0, 0.1, 4), (0, 0, 3), (0, 1, 0), (0, 1, 1), (3, 1, -2), (1e9, 1, 3), (0.5, 0.25, 6)]:
    one("GH-SERI", "sets", "Series", "Sequence", [[start], [step], [count]])
for lo, hi, n in [(0, 1, 4), (0, 10, 5), (-1, 1, 2), (5, 5, 3), (1, 0, 4), (0, 1, 0), (0, 1, 1), (0, 1, -3)]:
    # Range input 0 is a domain; supply it through Construct Domain
    add("GH-RANG", "sets", {"d": {"c": "Construct Domain", "sub": "Domain", "in": [[lo], [hi]]},
                            "r": {"c": "Range", "sub": "Sequence", "in": [None, [n]]}},
        [["d", 0, "r", 0]], [["r", 0]])

# --- sets: list operations on a fixed list 10..60
base = {"d": {"c": "Series", "sub": "Sequence", "in": [[10], [10], [6]]}}
for shift in (0, 1, 2, 6, 7, -1, -7):
    for wrap in (True, False):
        nodes = dict(base); nodes["n"] = {"c": "Shift List", "sub": "List", "in": [None, [shift], [wrap]]}
        add("GH-SHFT", "sets", nodes, [["d", 0, "n", 0]], [["n", 0]])
for idx in (0, 2, 5, 6, -1, 100):
    for wrap in (True, False):
        nodes = dict(base); nodes["n"] = {"c": "List Item", "sub": "List", "in": [None, [idx], [wrap]]}
        add("GH-LITM", "sets", nodes, [["d", 0, "n", 0]], [["n", 0]])
for size in (1, 2, 4, 6, 7, 0):
    nodes = dict(base); nodes["n"] = {"c": "Partition List", "sub": "List", "in": [None, [size]]}
    add("GH-PART", "sets", nodes, [["d", 0, "n", 0]], [["n", 0]])
for idxs in ([0], [0, 5], [6], [-1], [1, 1], [0, 1, 2, 3, 4, 5]):
    nodes = dict(base); nodes["n"] = {"c": "Cull Index", "sub": "Sequence", "in": [None, idxs, [False]]}
    add("GH-CULL", "sets", nodes, [["d", 0, "n", 0]], [["n", 0]])
nodes = dict(base); nodes["n"] = {"c": "Reverse List", "sub": "List", "in": [None]}
add("GH-REVL", "sets", nodes, [["d", 0, "n", 0]], [["n", 0]])
nodes = dict(base); nodes["n"] = {"c": "List Length", "sub": "List", "in": [None]}
add("GH-LLEN", "sets", nodes, [["d", 0, "n", 0]], [["n", 0]])

# --- sets: sort
for keys in ([3, 1, 2], [1, 1, 1], [-1, 5, 0], [2.5, 2.4999999, 2.5], [0]):
    one("GH-SORT", "sets", "Sort List", "List", [keys], nout=1)

# --- trees: graft / flatten / simplify / trim on branch structures
trees = [
    {"0": [1, 2, 3]},
    {"0": [1, 2], "1": [3, 4]},
    {"0;0": [1], "0;1": [2, 3], "1;0": [4]},
    {"0": [], "1": [5]},
    {"0;0;0": [1, 2]},
]
for t in trees:
    for comp, sub in (("Graft Tree", "Tree"), ("Flatten Tree", "Tree"), ("Simplify Tree", "Tree")):
        one("GH-TREE", "tree", comp, sub, [t])
    for depth in (1, 2):
        one("GH-TRIM", "tree", "Trim Tree", "Tree", [t, [depth]])

# --- math operators: data matching between lists and trees
ops = [("Addition", "Operators"), ("Subtraction", "Operators"), ("Multiplication", "Operators"), ("Division", "Operators"),
       ("Modulus", "Operators"), ("Power", "Operators")]
pairs = [([1, 2, 3], [10, 20, 30]), ([1, 2, 3], [10, 20]), ([1, 2], [10, 20, 30, 40]), ([5], [1, 2, 3]), ([1, 2, 3], []),
         ([7], [0]), ([-7], [3]), ([7.5], [2]), ([0], [0]), ([2], [0.5]), ([-8], [1.0 / 3.0]), ([1e308], [10])]
for comp, sub in ops:
    for a, b in pairs:
        one("GH-MATH", "math", comp, sub, [a, b])
tree_pairs = [
    ({"0": [1, 2], "1": [3, 4]}, {"0": [10, 20, 30]}),
    ({"0": [1, 2]}, {"0": [10], "1": [20]}),
    ({"0": [1, 2], "1": [3]}, {"0": [10], "1": [20, 30, 40]}),
    ({"0;0": [1, 2], "0;1": [3]}, {"0": [100]}),
]
for comp, sub in ops[:3]:
    for a, b in tree_pairs:
        one("GH-TMTH", "math", comp, sub, [a, b])

# --- expression evaluation (inputs: expression, then variables x, y)
exprs = ["x+y", "x*y+2", "x-y*2", "(x+y)*2", "x^2", "x^y", "-x^2", "x/y", "x%y", "sqrt(x)", "abs(x)", "sin(x)", "cos(x)", "atan(x)",
         "log(x)", "ln(x)", "exp(x)", "floor(x)", "ceil(x)", "round(x)", "min(x,y)", "max(x,y)", "if(x>y,1,2)", "x>y", "x==y", "x<>y",
         "x and y", "x or y", "not(x)", "pi", "e", "1/0", "x+", "unknownfn(x)", "mod(x,y)", "x!", "x&y", "0.1+0.2"]
xy = [([3], [2]), ([-3], [2]), ([0], [0]), ([2.5], [0.5])]
for ex in exprs:
    for x, y in xy[:2] if ex not in ("x/y", "x%y", "x^y", "sqrt(x)", "log(x)") else xy:
        one("GH-EVAL", "expression", "Evaluate", "Script", [[ex], x, y])

# --- geometry spot checks that cross-check the Rhino corpus
for r in (0.5, 1, 2.5, 10, 0, -1):
    add("GH-CIRC", "geometry", {"c": {"c": "Circle", "sub": "Primitive", "in": [None, [r]]},
                                "l": {"c": "Length", "sub": "Analysis", "in": [None]}},
        [["c", 0, "l", 0]], [["c", 0], ["l", 0]])

json.dump  # keep the import used
for c in cases:
    sys.stdout.write(json.dumps(c, sort_keys=True, separators=(",", ":")) + "\n")
