"""Reproduces the review samples. Run from the repository root.

For each program: sort all ids, draw 150 with random.Random(8).sample, then top up every domain
file that has fewer than 10 sampled rows (or all its rows if fewer) with the same generator.
"""
import json, glob, random, collections
for program in ("rhino", "archicad"):
    rows = {}
    for fn in glob.glob(f"parity/register/{program}/*.jsonl"):
        for line in open(fn):
            r = json.loads(line)
            rows[r["id"]] = r
    ids = sorted(rows)
    rng = random.Random(8)
    chosen = set(rng.sample(ids, 150))
    by_domain = collections.defaultdict(list)
    for i in ids:
        by_domain[rows[i]["domain"]].append(i)
    for domain, members in sorted(by_domain.items()):
        have = [i for i in members if i in chosen]
        if len(have) < 10:
            rest = [i for i in members if i not in chosen]
            chosen.update(rng.sample(rest, min(10, len(members)) - len(have)))
    print(program, len(chosen))
