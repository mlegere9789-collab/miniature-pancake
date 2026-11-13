#!/usr/bin/env python3
"""Compare a dino8_bench_budgets JSON result with a stored baseline and check the Appendix B gates.

  python3 -I parity/tools/perf_compare.py --current cur.json --baseline parity/perf_baseline.json
          [--threshold 3.0] [--min-abs-ms 0.25] [--gates parity/tools/perf_gates.json]
          [--strict-gates] [--summary out.md] [--write-baseline new_baseline.json]

Exit status
  0  no metric regressed more than --threshold percent (and, with --strict-gates, every gate is met)
  1  a regression, a metric missing from the current run, or a measurement that errored (value < 0)
  2  usage or input error

Rules (Master Plan section 9.1 'No benchmark regresses more than 3%', Appendix B budgets):
  * a metric is identified by (metric, dataset); the value is lower-is-better (milliseconds).
  * REGRESSION: current > baseline * (1 + threshold/100) and current - baseline > --min-abs-ms. The absolute floor only
    keeps a 3% move of a 0.1 ms measurement (timer jitter) from blocking a merge; it is never applied to anything slower.
  * MISSING: in the baseline but absent or errored in the current run. Removing a benchmark must be a deliberate baseline edit.
  * NEW: in the current run only. Reported, never failing; add it to the baseline with --write-baseline.
  * GATE: every perf_gates.json entry whose dataset exists in the current run is checked against its Appendix B maximum.
    A miss is REPORTED always; it fails the run only with --strict-gates (the kernel does not meet every budget yet, and a
    missed budget is a tracked gap, not a regression). 'indicative' entries are scaled and are printed but never counted.
"""
import argparse
import json
import re
import sys


def load(path):
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        print(f"perf_compare: cannot read {path}: {e}", file=sys.stderr)
        sys.exit(2)
    metrics = {}
    for m in doc.get("metrics", []):
        try:
            metrics[(m["metric"], m["dataset"])] = m
        except (KeyError, TypeError):
            print(f"perf_compare: {path}: malformed metric entry {m!r}", file=sys.stderr)
            sys.exit(2)
    return doc, metrics


def gate_limit(g, dataset):
    """Return the numeric maximum for this gate entry on this dataset, or None when the entry does not apply."""
    if "dataset" in g:
        if g["dataset"] not in ("*", dataset):
            return None
        return g.get("max")
    if "dataset_regex" in g:
        mt = re.search(g["dataset_regex"], dataset)
        if not mt:
            return None
        if "max_per_million_tris" in g:
            return g["max_per_million_tris"] * int(mt.group(1)) / 1e6
        return g.get("max")
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--current", required=True)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--threshold", type=float, default=3.0, help="allowed regression in percent (default 3.0)")
    ap.add_argument("--min-abs-ms", type=float, default=0.25, help="ignore regressions smaller than this many ms (default 0.25)")
    ap.add_argument("--gates", default=None, help="Appendix B gate table (default: perf_gates.json next to this script)")
    ap.add_argument("--strict-gates", action="store_true", help="a missed Appendix B gate also fails the run")
    ap.add_argument("--summary", default=None, help="write a markdown report to this file (for $GITHUB_STEP_SUMMARY)")
    ap.add_argument("--write-baseline", default=None, help="write the current metrics as a new baseline file and exit 0")
    args = ap.parse_args()

    cur_doc, cur = load(args.current)
    if args.write_baseline:
        bad = [k for k, m in cur.items() if m.get("value", -1) < 0]
        if bad:
            print("perf_compare: refusing to write a baseline containing errored metrics: " + ", ".join(f"{a}/{b}" for a, b in bad), file=sys.stderr)
            return 2
        with open(args.write_baseline, "w", encoding="utf-8") as f:
            json.dump(cur_doc, f, indent=2)
            f.write("\n")
        print(f"wrote baseline {args.write_baseline} ({len(cur)} metrics)")
        return 0

    base_doc, base = load(args.baseline)
    gates_path = args.gates
    if gates_path is None:
        import os
        gates_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "perf_gates.json")
    with open(gates_path, encoding="utf-8") as f:
        gates = json.load(f).get("gates", [])

    lines, md = [], []
    regress, missing, errored, improved, steady, new = [], [], [], [], [], []

    for key in sorted(base):
        b = base[key]
        c = cur.get(key)
        name = f"{key[0]} [{key[1]}]"
        if c is None:
            missing.append(name)
            continue
        if c.get("value", -1) < 0:
            errored.append(f"{name}: {c.get('error', 'error')}")
            continue
        bv, cv = float(b["value"]), float(c["value"])
        if bv <= 0:
            continue
        pct = (cv - bv) / bv * 100.0
        row = (name, bv, cv, pct)
        if pct > args.threshold and (cv - bv) > args.min_abs_ms:
            regress.append(row)
        elif pct < -args.threshold and (bv - cv) > args.min_abs_ms:
            improved.append(row)
        else:
            steady.append(row)
    for key in sorted(cur):
        if key not in base:
            c = cur[key]
            if c.get("value", -1) < 0:
                errored.append(f"{key[0]} [{key[1]}]: {c.get('error', 'error')}")
            else:
                new.append((f"{key[0]} [{key[1]}]", float(c["value"])))

    misses, indicative_misses, met = [], [], []
    for key in sorted(cur):
        c = cur[key]
        if c.get("value", -1) < 0:
            continue
        for g in gates:
            if g["metric"] != key[0]:
                continue
            lim = gate_limit(g, key[1])
            if lim is None:
                continue
            row = (f"{key[0]} [{key[1]}]", float(c["value"]), lim, g.get("row", ""))
            if float(c["value"]) <= lim:
                met.append(row)
            elif g.get("indicative"):
                indicative_misses.append(row)
            else:
                misses.append(row)

    def out(s=""):
        lines.append(s)

    out(f"baseline commit {base_doc.get('commit', '?')}  vs  current commit {cur_doc.get('commit', '?')}  (threshold {args.threshold:g}%)")
    out(f"{len(steady)} within threshold, {len(improved)} improved, {len(regress)} REGRESSED, {len(missing)} missing, "
        f"{len(errored)} errored, {len(new)} new")
    for name, bv, cv, pct in regress:
        out(f"REGRESSION  {name}: {bv:.3f} -> {cv:.3f} ms ({pct:+.1f}%)")
    for name in missing:
        out(f"MISSING     {name}: in the baseline, absent from the current run")
    for e in errored:
        out(f"ERROR       {e}")
    for name, bv, cv, pct in improved:
        out(f"improved    {name}: {bv:.3f} -> {cv:.3f} ms ({pct:+.1f}%)")
    for name, v in new:
        out(f"new         {name}: {v:.3f} ms (not in the baseline)")
    out(f"Appendix B gates: {len(met)} met, {len(misses)} MISSED, {len(indicative_misses)} indicative rows over their scaled value")
    for name, v, lim, row in misses:
        out(f"GATE MISS   {name}: {v:.1f} ms > {lim:g} ms   [{row}]")
    for name, v, lim, row in indicative_misses:
        out(f"indicative  {name}: {v:.1f} ms > scaled {lim:.1f} ms   [{row}]")
    print("\n".join(lines))

    if args.summary:
        md.append("### Kernel performance vs baseline")
        md.append(lines[0])
        md.append("")
        md.append(lines[1])
        md.append("")
        md.append("| metric [dataset] | baseline ms | current ms | change |")
        md.append("|---|---:|---:|---:|")
        for name, bv, cv, pct in regress + improved + steady:
            flag = "**REGRESSION** " if (name, bv, cv, pct) in regress else ""
            md.append(f"| {name} | {bv:.3f} | {cv:.3f} | {flag}{pct:+.1f}% |")
        md.append("")
        md.append(f"### Appendix B gates ({len(met)} met, {len(misses)} missed)")
        md.append("| metric [dataset] | current ms | gate ms | status |")
        md.append("|---|---:|---:|---|")
        for name, v, lim, row in met:
            md.append(f"| {name} | {v:.1f} | {lim:g} | met |")
        for name, v, lim, row in misses:
            md.append(f"| {name} | {v:.1f} | {lim:g} | **MISSED** |")
        for name, v, lim, row in indicative_misses:
            md.append(f"| {name} | {v:.1f} | {lim:.1f} | indicative, over |")
        with open(args.summary, "a", encoding="utf-8") as f:
            f.write("\n".join(md) + "\n")

    failed = bool(regress or missing or errored)
    if args.strict_gates and misses:
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
