#!/usr/bin/env python3
"""Grasshopper golden intake check (G2) and Dino 8 comparison. Tolerance classes are fixed here; the reviewer changes them with a written reason.
  --check-golden : schema check only.   --dino file : compare a Dino 8 result file (same record shape) against golden.
Numbers compare at relative 1e-9 (pure math) and 1e-4 for geometry lengths; text, bools, paths and counts compare exactly.
"""
import argparse, glob, json, math, sys

ALLOWED_OUT = {"trees", "warnings", "errors", "remarks"}
REL_MATH, REL_GEOM = 1e-9, 1e-4


def load(paths):
    recs = {}
    for p in paths:
        for line in open(p, encoding="utf-8"):
            if line.strip():
                r = json.loads(line, parse_constant=lambda s: (_ for _ in ()).throw(ValueError("non-finite constant " + s)))
                recs[r["id"]] = r
    return recs


def check_golden(golden, cases):
    bad = []
    for cid, r in golden.items():
        if cid not in cases:
            bad.append((cid, "unknown case id"))
        if set(r.get("out", {})) - ALLOWED_OUT:
            bad.append((cid, "disallowed output keys %s" % sorted(set(r["out"]) - ALLOWED_OUT)))
        for k, v in r.items():
            if k not in ("id", "ok", "error", "build", "out", "ms") and not isinstance(v, (int, float, bool)):
                bad.append((cid, "unexpected field " + k))
        if not r.get("build"):
            bad.append((cid, "missing build string"))
        if r.get("ok") is False and not isinstance(r.get("error"), str):
            bad.append((cid, "failed record without error type"))
        if r.get("ok") and not r.get("out"):
            bad.append((cid, "ok record without output"))
        for name, rows in r.get("out", {}).get("trees", {}).items():
            for row in rows:
                for x in row[1]:
                    if isinstance(x, str) and len(x) > 200:
                        bad.append((cid, "string longer than 200 chars"))
    return bad


def same(g, d, rel):
    if isinstance(g, bool) or isinstance(d, bool) or isinstance(g, str) or isinstance(d, str):
        return g == d
    if isinstance(g, (int, float)) and isinstance(d, (int, float)):
        return abs(g - d) <= max(1e-12, rel * max(abs(g), abs(d)))
    if isinstance(g, list) and isinstance(d, list):
        return len(g) == len(d) and all(same(x, y, rel) for x, y in zip(g, d))
    return g == d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", required=True)
    ap.add_argument("--golden", nargs="+", required=True)
    ap.add_argument("--dino")
    ap.add_argument("--check-golden", action="store_true")
    a = ap.parse_args()
    cases = load([a.cases])
    golden = load(sorted(sum((glob.glob(g) for g in a.golden), [])))
    bad = check_golden(golden, cases)
    if not golden:
        bad.append(("*", "no golden records found"))
    for cid, why in bad:
        print("GOLDEN REJECT %s: %s" % (cid, why))
    print("golden records: %d of %d cases, %d rejected" % (len(golden), len(cases), len(bad)))
    if a.check_golden or not a.dino:
        return 1 if bad else 0
    dino = load([a.dino])
    div = 0
    for cid, g in sorted(golden.items()):
        d = dino.get(cid)
        rel = REL_GEOM if cid.startswith("GH-CIRC") else REL_MATH
        if d is None or d.get("ok") != g.get("ok") or (not g["ok"] and d.get("error") != g.get("error")) or (g["ok"] and not same(g["out"], d.get("out"), rel)):
            div += 1
            print("DIVERGE", cid)
    print("%d of %d golden cases diverge" % (div, len(golden)))
    return 1 if div else 0


if __name__ == "__main__":
    sys.exit(main())
