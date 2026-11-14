#!/usr/bin/env python3
"""Summarize a corpus_runner results file against corpus.json (Python standard library only).

usage: python3 -I summarize.py corpus.json results.jsonl [--failures]

Prints Markdown: success rates (all boolean cases, golden, evil, per category), failure
classes, the precision ladder, CPU time, and with --failures the list of failing cases with
the first failing invariant.  Controls (measure-only cases) are reported separately and are
not part of the boolean success rate.
"""
import json
import math
import re
import sys
from collections import Counter, defaultdict


def load(path):
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


def pct(a, b):
    return "%.1f%%" % (100.0 * a / b) if b else "n/a"


def family(c):
    """Pair configurations carry their stem as the first tag; multi-operand cases are named by id."""
    t = c["tags"][0] if c.get("tags") else ""
    if t and t not in ("curved", "planar", "many-holes", "nested", "control"):
        return t
    parts = c["id"].split("-")[1:]
    if parts and parts[-1] in ("U", "I", "AB", "BA"):
        parts = parts[:-1]
    return "-".join(parts)


def classify(r):
    """Failure class of a non-ok result, by what stopped it."""
    res = r["result"]
    reason = r.get("reason", "")
    if res == "timeout":
        return "timeout"
    if res == "throw":
        if reason.startswith("crash"):
            return "crash (signal)"
        if "claimed by 3 or more fragment loops" in reason:
            return "throw: non-manifold fragment loops (engine refuses)"
        if "not a valid ON_Brep" in reason:
            return "throw: sealed result is not a valid ON_Brep"
        if "gaps between faces" in reason or "unpaired edge" in reason or "after sewing" in reason:
            return "throw: result not closed after sewing"
        if "not consistently oriented" in reason or "IsSolid() is false" in reason:
            return "throw: faces not consistently oriented"
        if "operand construction" in reason:
            return "throw: operand construction (corpus bug)"
        return "throw: other"
    failed = r.get("failed", [])
    return "fail: " + (failed[0] if failed else "?")


def opname(c):
    """Operation of a boolean case: the step operations joined by '+' (multi-step cases are their own row)."""
    steps = c.get("operation", {}).get("steps", [])
    return "+".join(st["op"] for st in steps) or c.get("operation", {}).get("type", "?")


def bucket(r):
    """Coarse outcome bucket used by the baseline report. Every non-ok result lands in exactly one bucket
    (first match wins); nothing is relabelled as ok.
      crash          process died on a signal
      timeout        CPU-time limit hit
      throw          the operation raised an exception (kernel refused or its sealed result failed its own validity gate)
      invalid result a result came back but the B-rep is not valid or not a solid
      wrong volume   a valid solid came back with a volume outside the oracle tolerance
      wrong emptiness / wrong topology (Euler, shell count) / mesh-only (closed tessellation, area, centroid)
    """
    res = r["result"]
    if res == "ok":
        return "ok"
    if res == "timeout":
        return "timeout"
    if res == "throw":
        return "crash" if (r.get("reason", "") or "").startswith("crash") else "throw"
    f = set(r.get("failed", []))
    if f & {"valid", "solid"}:
        return "invalid result"
    if "volume" in f:
        return "wrong volume"
    if "empty" in f:
        return "wrong emptiness"
    if f & {"euler", "shells"}:
        return "wrong topology"
    return "mesh/area/centroid only"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    manifest = json.load(open(args[0]))
    results = {r["id"]: r for r in load(args[1])}
    cases = {c["id"]: c for c in manifest["cases"]}
    missing = [i for i in cases if i not in results]
    rows = []
    for cid, c in cases.items():
        if cid in results:
            r = dict(results[cid])
            r["case"] = c
            rows.append(r)
    ctrl = [r for r in rows if r["case"]["category"] == "control"]
    bools = [r for r in rows if r["case"]["category"] != "control"]
    ok = lambda rs: sum(1 for r in rs if r["result"] == "ok")

    print("## Success rates (boolean cases; controls reported separately)\n")
    print("| set | cases | ok | fail | throw | timeout | success |")
    print("|---|---|---|---|---|---|---|")
    for name, rs in (("all boolean", bools), ("golden", [r for r in bools if r["case"]["set"] == "golden"]), ("evil", [r for r in bools if r["case"]["set"] == "evil"]), ("controls (no boolean)", ctrl)):
        c = Counter(r["result"] for r in rs)
        print("| %s | %d | %d | %d | %d | %d | **%s** |" % (name, len(rs), c["ok"], c["fail"], c["throw"], c["timeout"], pct(c["ok"], len(rs))))
    if missing:
        print("\nWARNING: %d manifest cases have no result: %s" % (len(missing), ", ".join(missing[:8])))


    print("\n## Per operation (boolean cases)\n")
    BK = ["ok", "crash", "throw", "invalid result", "wrong volume", "wrong emptiness", "wrong topology", "mesh/area/centroid only", "timeout"]
    print("| operation | cases | " + " | ".join(BK) + " | success |")
    print("|---|---|" + "---|" * len(BK) + "---|")
    byop = defaultdict(list)
    for r in bools:
        byop[opname(r["case"])].append(r)
    for name, rs in sorted(byop.items()) + [("ALL", bools)]:
        c = Counter(bucket(r) for r in rs)
        print("| %s | %d | " % (name, len(rs)) + " | ".join(str(c[k]) for k in BK) + " | %s |" % pct(c["ok"], len(rs)))
    print("\nOperation by set:\n")
    print("| operation | set | cases | ok | " + " | ".join(BK[1:]) + " |")
    print("|---|---|---|---|" + "---|" * (len(BK) - 1))
    byos = defaultdict(list)
    for r in bools:
        byos[(opname(r["case"]), r["case"]["set"])].append(r)
    for (name, st), rs in sorted(byos.items()):
        c = Counter(bucket(r) for r in rs)
        print("| %s | %s | %d | %d | " % (name, st, len(rs), c["ok"]) + " | ".join(str(c[k]) for k in BK[1:]) + " |")

    print("\n## Per category (boolean cases)\n")
    print("| category | set | cases | ok | success |")
    print("|---|---|---|---|---|")
    by = defaultdict(list)
    for r in bools:
        by[(r["case"]["set"], r["case"]["category"])].append(r)
    for (s, cat), rs in sorted(by.items()):
        print("| %s | %s | %d | %d | %s |" % (cat, s, len(rs), ok(rs), pct(ok(rs), len(rs))))

    print("\n## Per family (case id stem, boolean cases)\n")
    fam = defaultdict(list)
    for r in bools:
        fam[(r["case"]["set"], family(r["case"]))].append(r)
    print("| family | set | cases | ok |")
    print("|---|---|---|---|")
    for (s, f), rs in sorted(fam.items()):
        print("| %s | %s | %d | %d |" % (f, s, len(rs), ok(rs)))

    print("\n## Failure classes (boolean cases that are not ok)\n")
    cls = Counter(classify(r) for r in bools if r["result"] != "ok")
    print("| class | cases |")
    print("|---|---|")
    for k, v in cls.most_common():
        print("| %s | %d |" % (k, v))

    # sensitivity: which invariant families stop the non-ok cases
    mesh_only = {"tess_closed", "area", "centroid"}
    exact_ok = sum(1 for r in bools if r["result"] == "ok" or (r["result"] == "fail" and set(r.get("failed", [])) <= mesh_only))
    print("\n## Sensitivity\n")
    print("- strict (every invariant in the manifest): %d / %d = **%s**" % (ok(bools), len(bools), pct(ok(bools), len(bools))))
    print("- exact B-rep only (ignoring failures that only concern the closed-mesh checks: tess_closed, area, centroid): %d / %d = %s" % (exact_ok, len(bools), pct(exact_ok, len(bools))))
    print("- ran to a result without exception, crash or timeout: %d / %d = %s" % (sum(1 for r in bools if r["result"] in ("ok", "fail")), len(bools), pct(sum(1 for r in bools if r["result"] in ("ok", "fail")), len(bools))))

    print("\n## Precision ladder (volume only; cases that returned a measured volume, golden + evil, non-empty expectation)\n")
    meas = []
    for r in bools:
        m = r.get("measured") or {}
        e = r.get("expected") or {}
        if m.get("empty") is False and m.get("volume") is not None and e.get("volume"):
            meas.append(abs(m["volume"] - e["volume"]) / abs(e["volume"]))
    print("| relative volume error <= | cases | share of %d measured |" % len(meas))
    print("|---|---|---|")
    for t in (1e-2, 1e-3, 1e-4, 1e-6, 1e-9):
        n = sum(1 for x in meas if x <= t)
        print("| %g | %d | %s |" % (t, n, pct(n, len(meas))))

    cpu = sorted(r.get("cpu_ms", r.get("ms", 0)) for r in bools)
    if cpu:
        print("\n## CPU time per boolean case (ms, user+system of the case process)\n")
        print("median %.0f, 90th percentile %.0f, max %.0f, total %.1f s" % (cpu[len(cpu) // 2], cpu[int(len(cpu) * 0.9)], cpu[-1], sum(cpu) / 1000.0))

    if "--failures" in sys.argv:
        print("\n## Failing cases\n")
        print("| id | result | first failing invariant / reason |")
        print("|---|---|---|")
        for r in bools:
            if r["result"] != "ok":
                reason = (r.get("reason", "") or "").replace("|", "/").replace("\n", " ")
                reason = re.sub(r"\s+", " ", reason)[:170]
                print("| %s | %s | %s |" % (r["id"], r["result"], reason))
        for r in ctrl:
            if r["result"] != "ok":
                print("| %s | %s | %s |" % (r["id"], r["result"], (r.get("reason", "") or "")[:170]))


if __name__ == "__main__":
    main()
