#!/usr/bin/env python3
"""Append one heartbeat line to ledger/heartbeats/<UTC date>/<agent>.jsonl (plan 7.2, 7.3).

    python3 ledger/tools/heartbeat.py --agent NAME --what "what changed" [--task T-1]
        [--commit SHA] [--evidence "4 checks ok"] [--metric 12] [--state working]
        [--waiting-on CI-RUN-ID] [--ledger DIR] [--now TIME]

The file is append-only and belongs to one agent, so heartbeats never conflict in git. The
commit defaults to the short hash of HEAD in the current directory. A heartbeat is "a commit
or ledger entry with evidence" at least every 10 minutes; the watchdog marks an agent stalled
after 15 minutes of silence or three heartbeats with no measurable change (task, commit,
evidence, metric and state), so put real evidence in --evidence, not a repeat of the message.
States that end the watch on an agent: done, released, handoff, idle. Use --waiting-on with a
CI run id while you wait for it: that is excused for 40 minutes.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import ledgerlib  # noqa: E402

AGENT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


def head_commit():
    try:
        done = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout.strip() or None if done.returncode == 0 else None


def build(args, now):
    beat = {"time": ledgerlib.format_time(now), "what": args.what.strip()}
    if args.task:
        beat["task"] = args.task
    commit = args.commit if args.commit is not None else head_commit()
    if commit:
        beat["commit"] = commit
    if args.evidence:
        beat["evidence"] = args.evidence
    if args.metric is not None:
        beat["metric"] = args.metric
    if args.state:
        beat["state"] = args.state
    if args.waiting_on:
        beat["waiting_on"] = args.waiting_on
    return beat


def run(argv=None, out=None):
    out = out or sys.stdout
    parser = argparse.ArgumentParser(description="Append a heartbeat to the ledger.")
    parser.add_argument("--agent", required=True)
    parser.add_argument(
        "--what", required=True, help="what changed since the last heartbeat"
    )
    parser.add_argument("--task")
    parser.add_argument(
        "--commit", help="commit hash (default: HEAD of the current directory)"
    )
    parser.add_argument(
        "--evidence", help="a measurement or test result that shows the change"
    )
    parser.add_argument(
        "--metric", type=float, help="any number that moves when work moves"
    )
    parser.add_argument(
        "--state", help="working, waiting, idle, done, released or handoff"
    )
    parser.add_argument(
        "--waiting-on", help="CI run id (or other external wait) you are waiting for"
    )
    parser.add_argument("--ledger", default=os.path.join(REPO_ROOT, "ledger"))
    parser.add_argument("--now", help="time to use instead of the clock")
    args = parser.parse_args(argv)
    if not AGENT_RE.match(args.agent):
        print(
            "heartbeat: agent names use letters, digits, '.', '_' and '-' only",
            file=sys.stderr,
        )
        return 2
    if not args.what.strip():
        print("heartbeat: --what must say what changed", file=sys.stderr)
        return 2
    try:
        now = ledgerlib.parse_time(args.now) if args.now else ledgerlib.utcnow()
    except ValueError as exc:
        print("heartbeat: %s" % exc, file=sys.stderr)
        return 2
    beat = build(args, now)
    folder = os.path.join(args.ledger, "heartbeats", now.strftime("%Y-%m-%d"))
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, args.agent + ".jsonl")
    line = json.dumps(beat, sort_keys=True)
    with open(path, "a", encoding="utf-8", newline="\n") as handle:
        handle.write(line + "\n")
    out.write("%s\n" % line)
    return 0


def main():
    sys.exit(run())


if __name__ == "__main__":
    main()
