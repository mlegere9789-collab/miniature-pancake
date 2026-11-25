#!/usr/bin/env python3
"""Compare Dino 8 results against Rhino golden records and rank divergences.

Inputs (JSON lines, one record per case id):
  --golden  golden/*.jsonl   written by run_rhino.py on the owner's Rhino
  --dino    dino_results.jsonl written by the Dino 8 case runner (same cases.jsonl, same record shape)
Record shape:
  {"id": ..., "ok": true|false, "error": "<type>", "tolerance": 0.001, "build": "...", "out": {"area":.., "volume":.., ...}}

Tolerance classes are FIXED here. They are changed only by the reviewer with a written reason (CLEANROOM.md rule 3).
Never widen them to make a case pass.
"""
import argparse, glob, json, math, sys

REL = {"area": 1e-4, "volume": 1e-4, "length": 1e-4}
ABS_BBOX_FACTOR = 1.0  # bbox corners compared to document tolerance * this
ALLOWED_OUT_KEYS = {"area", "volume", "bbox", "valid", "solid", "faces", "edges", "vertices", "euler",
                    "count", "points", "length", "degree", "ctrlpts"}


def load(paths):
    recs = {}
    for p in paths:
        for line in open(p, encoding="utf-8"):
            line = line.strip()
            if line:
                r = json.loads(line)
                recs[r["id"]] = r
    return recs


def check_golden(golden, cases):
    bad = []
    for cid, r in golden.items():
        if cid not in cases:
            bad.append((cid, "unknown case id"))
        extra = set(r.get("out", {})) - ALLOWED_OUT_KEYS
        if extra:
            bad.append((cid, f"disallowed output keys {sorted(extra)}"))
        for k, v in r.items():
            if k not in ("id", "ok", "error", "tolerance", "build", "out", "ms") and not isinstance(v, (int, float, bool)):
                bad.append((cid, f"unexpected field {k}"))
        if isinstance(r.get("error"), str) and len(r["error"]) > 200:
            bad.append((cid, "error string longer than 200 chars"))
        if not r.get("build"):
            bad.append((cid, "missing build string"))
    return bad


def close(a, b, rel, absol):
    return abs(a - b) <= max(absol, rel * max(abs(a), abs(b)))


def cmp_out(key, g, d, tol):
    if key in REL:
        return close(g, d, REL[key], tol)
    if key == "bbox":
        return all(close(x, y, 0.0, tol * ABS_BBOX_FACTOR) for x, y in zip(g, d)) and len(g) == len(d)
    if key == "points":
        if len(g) != len(d):
            return False
        gs = sorted(map(tuple, g)); ds = sorted(map(tuple, d))
        return all(all(close(x, y, 0.0, tol) for x, y in zip(a, b)) for a, b in zip(gs, ds))
    return g == d  # counts, flags, euler, degree, ctrlpts: exact


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", default="cases.jsonl")
    ap.add_argument("--golden", nargs="+", required=True)
    ap.add_argument("--dino")
    ap.add_argument("--check-golden", action="store_true")
    ap.add_argument("--report")
    a = ap.parse_args()
    cases = load([a.cases])
    golden = load(sorted(sum((glob.glob(g) for g in a.golden), [])))
    bad = check_golden(golden, cases)
    for cid, why in bad:
        print(f"GOLDEN REJECT {cid}: {why}")
    print(f"golden records: {len(golden)} of {len(cases)} cases, {len(bad)} rejected")
    if a.check_golden or not a.dino:
        return 1 if bad else 0
    dino = load([a.dino])
    rows, n_ok, n_div, n_missing = [], 0, 0, 0
    for cid, g in sorted(golden.items()):
        d = dino.get(cid)
        if d is None:
            n_missing += 1
            rows.append((cid, "dino result missing", cases[cid]["family"]))
            continue
        if g.get("ok") != d.get("ok"):
            n_div += 1
            rows.append((cid, f"outcome differs: rhino ok={g.get('ok')} ({g.get('error')}), dino ok={d.get('ok')} ({d.get('error')})", cases[cid]["family"]))
            continue
        if not g.get("ok"):
            n_ok += 1
            continue
        tol = float(g.get("tolerance", 0.001))
        diffs = []
        for k, gv in g["out"].items():
            dv = d.get("out", {}).get(k)
            if dv is None or not cmp_out(k, gv, dv, tol):
                diffs.append(f"{k}: rhino={gv} dino={dv}")
        if diffs:
            n_div += 1
            rows.append((cid, "; ".join(diffs), cases[cid]["family"]))
        else:
            n_ok += 1
    print(f"match {n_ok}, diverge {n_div}, missing {n_missing}")
    fam = {}
    for _, _, f in rows:
        fam[f] = fam.get(f, 0) + 1
    print("divergences by family:", fam)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            for cid, why, fm in rows:
                f.write(json.dumps({"id": cid, "family": fm, "why": why}) + "\n")
    return 1 if (n_div or n_missing or bad) else 0


if __name__ == "__main__":
    sys.exit(main())
