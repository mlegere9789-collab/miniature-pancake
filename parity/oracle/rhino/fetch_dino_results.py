#!/usr/bin/env python3
"""Turn a CI job log (or a saved copy of one) into dino_results.jsonl.

The Dino 8 case runner (dino8-kernel/tests/extra/oracle_rhino.cpp, test name extra::OracleRhino) prints one `D8RES {record}` line per case.
Artifact download is blocked by the egress proxy, so the lines are read from the raw job log:

  python3 -I parity/oracle/rhino/fetch_dino_results.py --job <job id> --out parity/oracle/rhino/dino_results.jsonl
  python3 -I parity/oracle/rhino/fetch_dino_results.py --log saved.log   --out parity/oracle/rhino/dino_results.jsonl

Records come out in cases.jsonl order, one per case; a case with no D8RES line is reported and left out (compare.py then lists it missing).
"""
import argparse, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = "mlegere9789-collab/miniature-pancake"


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--job", help="GitHub Actions job id of the Kernel job")
    g.add_argument("--log", help="a saved job log")
    ap.add_argument("--cases", default=os.path.join(HERE, "cases.jsonl"))
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    if a.job:
        text = subprocess.run(["gh", "api", f"repos/{REPO}/actions/jobs/{a.job}/logs"], check=True, capture_output=True, text=True).stdout
    else:
        text = open(a.log, encoding="utf-8", errors="replace").read()

    recs = {}
    for line in text.splitlines():
        line = re.sub(r"^\d{4}-\d\d-\d\dT[\d:.]+Z ", "", line)
        if not line.startswith("D8RES {"):
            continue
        r = json.loads(line[len("D8RES "):])
        recs[r["id"]] = r  # a later line for the same id wins

    order = [json.loads(l)["id"] for l in open(a.cases, encoding="utf-8") if l.strip()]
    missing = [i for i in order if i not in recs]
    extra = [i for i in recs if i not in set(order)]
    with open(a.out, "w", encoding="utf-8") as f:
        for i in order:
            if i in recs:
                f.write(json.dumps(recs[i], sort_keys=True) + "\n")
    print(f"{len(recs) - len(extra)} of {len(order)} case records written to {a.out}; missing {len(missing)}; unknown ids {len(extra)}")
    if missing:
        print("missing:", ", ".join(missing[:20]))
    return 1 if (missing or extra) else 0


if __name__ == "__main__":
    sys.exit(main())
