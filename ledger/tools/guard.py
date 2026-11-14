#!/usr/bin/env python3
"""Anti-gaming guards that run in CI on every push (plan 7.4 and Appendix C).

    python3 ledger/tools/guard.py paths --actor NAME [--allowed a,b] [--changed FILE|-]
    python3 ledger/tools/guard.py rows  --actor NAME [--allowed a,b] --base DIR --head DIR

paths  No agent writes to ledger/evidence/, ledger/percent.json or ledger/credits.jsonl: only
       CI does. The changed paths (one per line, from `git diff --name-only`) are read from
       --changed or stdin. A path is a violation unless the pushing actor is one of the CI
       actors (--allowed, or LEDGER_CI_ACTORS, default github-actions[bot]).
rows   tier_now, status and evidence of a Register row are set by CI only. Comparing the
       Register in --base with the one in --head, a push by anyone else must not raise a
       tier above the old one, set status verified, or set or change evidence, and a new row
       must start at tier None, status open (or in_progress) and evidence null. Lowering a
       credit is always allowed: it can only reduce the count.

Exit status 0 when clean, 1 with violations (printed one per line), 2 for a usage problem.
Repository settings (branch protection, required reviewers on these paths) are the hard
barrier; this guard is the check that runs even where they are not configured.
"""

from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO_ROOT, "parity", "tools"))

import regio  # noqa: E402

PROTECTED_PREFIXES = ("ledger/evidence/",)
PROTECTED_FILES = ("ledger/percent.json", "ledger/credits.jsonl")
DEFAULT_CI_ACTORS = ("github-actions[bot]",)
NEW_ROW_STATUS = ("open", "in_progress")


def ci_actors(allowed):
    names = [n.strip() for n in (allowed or os.environ.get("LEDGER_CI_ACTORS") or "").split(",") if n.strip()]
    return tuple(names) or DEFAULT_CI_ACTORS


def check_paths(changed, actor, allowed=None):
    """Violations (strings) for the changed paths pushed by actor."""
    if actor in ci_actors(allowed):
        return []
    found = []
    for path in changed:
        path = path.strip().replace("\\", "/")
        if path in PROTECTED_FILES or path.startswith(PROTECTED_PREFIXES):
            found.append("%s: written by CI only, not by %s" % (path, actor))
    return found


def _rows_by_id(root):
    rows = {}
    if root and os.path.isdir(root):
        records, _ = regio.read_register(root)
        for record in records:
            row_id = record["row"].get("id")
            if isinstance(row_id, str) and row_id not in rows:
                rows[row_id] = record
    return rows


def _rank(row):
    return regio.TIER_RANK.get(regio.tier_of(row), 0)


def check_rows(base_rows, head_rows, actor, allowed=None):
    """Violations for credit fields changed by an actor that is not CI."""
    if actor in ci_actors(allowed):
        return []
    found = []
    for row_id in sorted(head_rows):
        record = head_rows[row_id]
        row = record["row"]
        where = "%s:%s %s" % (record["file"], record["line"], row_id)
        old = base_rows.get(row_id)
        if old is None:
            if _rank(row) > 0:
                found.append("%s: a new row must start at tier None, not %s" % (where, regio.tier_of(row)))
            if row.get("status") not in NEW_ROW_STATUS and row.get("status") is not None:
                found.append("%s: a new row must start with status open, not %s" % (where, row.get("status")))
            if row.get("evidence") is not None:
                found.append("%s: a new row must start with evidence null" % where)
            continue
        before = old["row"]
        if _rank(row) > _rank(before):
            found.append("%s: tier_now raised from %s to %s by %s; only CI may credit a row" % (where, regio.tier_of(before), regio.tier_of(row), actor))
        if row.get("status") == "verified" and before.get("status") != "verified":
            found.append("%s: status set to verified by %s; only CI may verify a row" % (where, actor))
        if row.get("evidence") is not None and row.get("evidence") != before.get("evidence"):
            found.append("%s: evidence set or changed by %s; evidence is written by CI only" % (where, actor))
    return found


def run(argv=None, stdin=None, out=None):
    out = out or sys.stdout
    parser = argparse.ArgumentParser(description="Anti-gaming guards (plan 7.4).")
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("paths", "rows"):
        p = sub.add_parser(name)
        p.add_argument("--actor", required=True)
        p.add_argument("--allowed", help="comma separated CI actors (default LEDGER_CI_ACTORS or github-actions[bot])")
        if name == "paths":
            p.add_argument("--changed", default="-", help="file with changed paths, or - for stdin")
        else:
            p.add_argument("--base", required=True, help="Register directory before the push")
            p.add_argument("--head", required=True, help="Register directory after the push")
    args = parser.parse_args(argv)

    if args.command == "paths":
        if args.changed == "-":
            text = (stdin or sys.stdin).read()
        else:
            with open(args.changed, "r", encoding="utf-8") as handle:
                text = handle.read()
        violations = check_paths([line for line in text.splitlines() if line.strip()], args.actor, args.allowed)
    else:
        violations = check_rows(_rows_by_id(args.base), _rows_by_id(args.head), args.actor, args.allowed)
    for line in violations:
        out.write("guard: %s\n" % line)
    return 1 if violations else 0


def main():
    sys.exit(run())


if __name__ == "__main__":
    main()
