#!/usr/bin/env python3
"""Compute ledger/percent.json from the Register and the CI evidence (plan section 7).

    python3 parity/tools/percent.py [--register DIR] [--evidence DIR] [--checks FILE ...]
        [--legacy FILE] [--pace FILE] [--previous FILE] [--out FILE] [--credits-out FILE]
        [--now TIME] [--run-id ID] [--commit SHA] [--check] [--strict]

Nothing in the output is typed by hand: the numbers come from the Register rows and from
machine-written evidence. An agent's own statement counts as nothing.

A row PASSES when it names at least one acceptance test and EVERY acceptance test id has
result "ok" in the evidence, judged by the latest run that contains the test. A test does not
count when it:
  - is missing from the evidence, failed, or was skipped;
  - has mixed results for the same commit or run (flaky: it blocks crediting, plan 7.4);
  - ran in an earlier run but is absent from the latest FULL run (deleted or disabled);
  - belongs to a row whose evidence seal (evidence.spec_sha) no longer matches the row's
    acceptance list, tolerances, superiority tests or target (weakened after credit).
A passing row is "Best" when its superiority tests (superiority_tests) all pass as well.
Alias rows (alias_of) are never counted: one user outcome is one row.

Evidence layout (everything below --evidence, default ledger/evidence):
  <run>/run.json        optional manifest {"run_id","sha","kind":"full"|"lite","finished_at"}
  <run>/<task>.json     {"run_id","task","sha","finished_at","kind","passed":[ids],
                         "failed":[ids],"skipped":[ids],"artifacts":[urls]}
  <run>/**/*.jsonl      one line per check {"suite","test","name","result","ms"[,"commit"]}
Runs are ordered by their directory name, numbers numerically (GitHub run ids increase).
--checks adds loose JSONL files as one extra run called "adhoc" (the newest run).

Outputs: ledger/percent.json (summary) and ledger/credits.jsonl (one line per passing row:
id, tier, run, time of credit, tests). The previous credits file is used to list rows that
lost their credit (reverted) since the last computation.

Exit status: 0; 1 with --check when percent.json differs from the recomputation, or with
--strict when evidence is malformed or the Register claims a credit the evidence does not
support; 2 for an input problem.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO_ROOT, "ledger", "tools"))

import ledgerlib  # noqa: E402
import regio  # noqa: E402

DEFAULT_REGISTER = os.path.join(REPO_ROOT, "parity", "register")
DEFAULT_EVIDENCE = os.path.join(REPO_ROOT, "ledger", "evidence")
DEFAULT_OUT = os.path.join(REPO_ROOT, "ledger", "percent.json")
DEFAULT_CREDITS = os.path.join(REPO_ROOT, "ledger", "credits.jsonl")
DEFAULT_LEGACY = os.path.join(REPO_ROOT, "ledger", "legacy.json")
DEFAULT_PACE = os.path.join(REPO_ROOT, "ledger", "pace.json")

SCHEMA_VERSION = 1
LIST_LIMIT = 200  # longest list written into percent.json; the count is always exact

Obs = collections.namedtuple("Obs", "run test result sha time source")


def norm_time(value):
    """An ISO time as YYYY-MM-DDTHH:MM:SSZ, or None when it is absent or unusable."""
    if not isinstance(value, str):
        return None
    when = ledgerlib.try_parse_time(value)
    return ledgerlib.format_time(when) if when else None


def run_order(run):
    """Run ordering key: loose files first, numbers numerically, ad-hoc checks last."""
    if run == "_":
        return ((-1,),)
    if run == "adhoc":
        return ((2,),)
    return ((0,) + ledgerlib.natural_key(run),)


def norm_result(text):
    """ok, fail, skip or other. Only "ok" ever counts for a row."""
    value = str(text).strip().lower()
    if value == "ok":
        return "ok"
    if value in ("fail", "failed", "error", "failure"):
        return "fail"
    if value in ("skip", "skipped", "xfail", "ignored", "disabled"):
        return "skip"
    return "other"


# --------------------------------------------------------------------------------------
# Evidence
# --------------------------------------------------------------------------------------


class Evidence:
    def __init__(self):
        self.runs = {}  # run key -> {"kind", "sha", "finished_at", "files", "checks"}
        self.obs = collections.defaultdict(list)  # test id -> [Obs]
        self.malformed = []  # {"file", "line", "message"}
        self.files = 0
        self.checks = 0
        self.digest = hashlib.sha256()

    def run_info(self, run):
        return self.runs.setdefault(
            run, {"kind": None, "sha": None, "finished_at": None, "files": 0, "checks": 0}
        )

    def bad(self, path, line, message):
        self.malformed.append({"file": path, "line": line, "message": message})


def _read_text(path):
    with open(path, "rb") as handle:
        return handle.read()


def _string_list(value):
    return isinstance(value, list) and all(isinstance(v, str) and v.strip() for v in value)


def _read_json_file(ev, run, path):
    """A run manifest or a task evidence file."""
    raw = _read_text(path)
    ev.digest.update(path.encode("utf-8") + b"\0" + raw + b"\0")
    ev.files += 1
    ev.run_info(run)["files"] += 1
    try:
        data = json.loads(raw.decode("utf-8-sig"))
    except ValueError as exc:
        ev.bad(path, None, "not valid JSON: %s" % exc)
        return
    if not isinstance(data, dict):
        ev.bad(path, None, "not a JSON object")
        return
    info = ev.run_info(run)
    if os.path.basename(path) == "run.json":
        if data.get("kind") in ("full", "lite"):
            info["kind"] = data["kind"]
        elif data.get("kind") is not None:
            ev.bad(path, None, "kind must be 'full' or 'lite'")
        if isinstance(data.get("sha"), str):
            info["sha"] = data["sha"]
        if data.get("finished_at") is not None:
            if norm_time(data["finished_at"]) is None:
                ev.bad(path, None, "finished_at is not an ISO time")
            else:
                info["finished_at"] = norm_time(data["finished_at"])
        return
    # task evidence
    declared = data.get("run_id")
    if declared is not None and str(declared) != run:
        ev.bad(path, None, "run_id %s does not match the directory %s" % (declared, run))
        return
    task = data.get("task")
    stem = os.path.splitext(os.path.basename(path))[0]
    if task is not None and task != stem:
        ev.bad(path, None, "task %r does not match the file name %r" % (task, stem))
        return
    lists = {}
    for key in ("passed", "failed", "skipped"):
        if key in data:
            if not _string_list(data[key]):
                ev.bad(path, None, "%r must be a list of test ids" % key)
                return
            lists[key] = data[key]
    if not lists:
        ev.bad(path, None, "none of passed, failed or skipped is present")
        return
    kind = data.get("kind")
    if kind in ("full", "lite") and info["kind"] is None:
        info["kind"] = kind
    sha = data.get("sha") if isinstance(data.get("sha"), str) else None
    finished = norm_time(data.get("finished_at"))
    if data.get("finished_at") is not None and finished is None:
        ev.bad(path, None, "finished_at is not an ISO time")
        return
    if finished and (info["finished_at"] is None or finished > info["finished_at"]):
        info["finished_at"] = finished
    if sha and info["sha"] is None:
        info["sha"] = sha
    for key, result in (("passed", "ok"), ("failed", "fail"), ("skipped", "skip")):
        for test in lists.get(key, []):
            ev.obs[test].append(Obs(run, test, result, sha or info["sha"], finished or info["finished_at"], path))
            ev.checks += 1
            info["checks"] += 1


def _read_checks_file(ev, run, path):
    raw = _read_text(path)
    ev.digest.update(path.encode("utf-8") + b"\0" + raw + b"\0")
    ev.files += 1
    ev.run_info(run)["files"] += 1
    items, problems = ledgerlib.read_jsonl(path)
    for line, message in problems:
        ev.bad(path, line, message)
    info = ev.run_info(run)
    for line, obj in items:
        test, result = obj.get("test"), obj.get("result")
        if not (isinstance(test, str) and test.strip()) or not isinstance(result, str):
            ev.bad(path, line, "a check needs a string 'test' and a string 'result'")
            continue
        sha = obj.get("commit") if isinstance(obj.get("commit"), str) else None
        when = norm_time(obj.get("time"))
        ev.obs[test].append(Obs(run, test, norm_result(result), sha or info["sha"], when or info["finished_at"], "%s:%d" % (path, line)))
        ev.checks += 1
        info["checks"] += 1


def _read_run_dir(ev, run, directory):
    manifest = os.path.join(directory, "run.json")
    if os.path.isfile(manifest):
        _read_json_file(ev, run, manifest)
    for dirpath, dirnames, filenames in os.walk(directory):
        dirnames.sort()
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            if path == manifest:
                continue
            if name.endswith(".jsonl"):
                _read_checks_file(ev, run, path)
            elif name.endswith(".json"):
                _read_json_file(ev, run, path)


def load_evidence(root, extra_checks=()):
    ev = Evidence()
    if root and os.path.isdir(root):
        for entry in sorted(os.listdir(root), key=ledgerlib.natural_key):
            full = os.path.join(root, entry)
            if os.path.isdir(full):
                _read_run_dir(ev, entry, full)
            elif entry.endswith(".jsonl"):
                _read_checks_file(ev, "_", full)
            elif entry.endswith(".json"):
                _read_json_file(ev, "_", full)
    for path in extra_checks:
        _read_checks_file(ev, "adhoc", path)
    return ev


def judge_tests(ev):
    """test id -> {"state", "run", "sha", "since"}.

    state: ok | failing | skipped | conflicting | vanished. A test with no observation has
    no entry (the caller reads that as "missing").
    """
    order = sorted(ev.runs, key=run_order)
    rank = {run: index for index, run in enumerate(order)}
    full_runs = [run for run in order if ev.runs[run]["kind"] == "full"]
    latest_full = full_runs[-1] if full_runs else None
    in_full = set()
    if latest_full is not None:
        for test, observations in ev.obs.items():
            if any(o.run == latest_full for o in observations):
                in_full.add(test)

    def run_time(run, observations):
        when = ev.runs[run]["finished_at"]
        if when:
            return when
        times = [o.time for o in observations if o.run == run and o.time]
        return max(times) if times else None

    states = {}
    for test, observations in ev.obs.items():
        runs = sorted({o.run for o in observations}, key=rank.get)
        latest = runs[-1]
        latest_obs = [o for o in observations if o.run == latest]
        sha = None
        for o in latest_obs:
            if o.sha:
                sha = o.sha
        scope = [o for o in observations if o.run == latest or (sha and o.sha == sha)]
        results = {o.result for o in scope}
        non_ok = results - {"ok"}
        if not non_ok:
            state = "ok"
        elif "ok" in results:
            state = "conflicting"
        elif non_ok & {"fail", "other"}:
            state = "failing"
        else:
            state = "skipped"
        if latest_full is not None and test not in in_full and rank[latest] < rank[latest_full]:
            state = "vanished"
        since = None
        if state == "ok":
            streak = []
            for run in reversed(runs):
                run_results = {o.result for o in observations if o.run == run}
                if run_results == {"ok"}:
                    streak.append(run)
                else:
                    break
            since = run_time(streak[-1], observations)
        states[test] = {"state": state, "run": latest, "sha": sha, "since": since}
    return states


# --------------------------------------------------------------------------------------
# Rows
# --------------------------------------------------------------------------------------


def evaluate_row(row, states):
    """Judge one row. Returns a dict with passing, tier, target_met, reasons, tests, run, at."""
    tests = regio.acceptance_ids(row)
    reasons = []
    if not tests:
        reasons.append("no-tests")
    for test in tests:
        entry = states.get(test)
        state = entry["state"] if entry else "missing"
        if state != "ok":
            reasons.append("%s:%s" % (state, test))
    evidence = row.get("evidence")
    if isinstance(evidence, dict) and isinstance(evidence.get("spec_sha"), str):
        if evidence["spec_sha"] != regio.spec_sha(row):
            reasons.append("spec-changed")
    passing = not reasons
    tier = "None"
    used = list(tests)
    if passing:
        tier = "Strong"
        superiority = regio.superiority_ids(row)
        if superiority and all(states.get(t, {}).get("state") == "ok" for t in superiority):
            tier = "Best"
            used += [t for t in superiority if t not in used]
    target_met = passing and (row.get("target") != "Best" or tier == "Best")
    run = None
    at = None
    if passing:
        ranked = [states[t] for t in used]
        run = max((s["run"] for s in ranked), key=run_order)
        times = [s["since"] for s in ranked]
        at = None if any(t is None for t in times) else max(times, key=ledgerlib.try_parse_time)
    return {
        "passing": passing,
        "tier": tier,
        "target_met": target_met,
        "reasons": reasons,
        "tests": used,
        "run": run,
        "at": at,
    }


def primary_reason(reasons):
    """One word for why a row does not pass, most serious first."""
    kinds = [r.split(":", 1)[0] for r in reasons]
    for kind in ("spec-changed", "conflicting", "vanished", "failing", "skipped", "missing", "no-tests"):
        if kind in kinds:
            return kind
    return "unknown"


def _bucket():
    return {"total": 0, "passing": 0, "best": 0, "target_met": 0}


def _finish(bucket):
    total = bucket["total"]
    bucket["percent"] = round(100.0 * bucket["passing"] / total, 3) if total else 0.0
    bucket["target_met_percent"] = round(100.0 * bucket["target_met"] / total, 3) if total else 0.0
    return bucket


def _add(bucket, verdict):
    bucket["total"] += 1
    if verdict["passing"]:
        bucket["passing"] += 1
    if verdict["tier"] == "Best":
        bucket["best"] += 1
    if verdict["target_met"]:
        bucket["target_met"] += 1


def read_credits(path):
    """Previous credits: id -> credit line. Missing or unreadable file gives {}."""
    if not path or not os.path.isfile(path):
        return {}
    items, _ = ledgerlib.read_jsonl(path)
    return {obj["id"]: obj for _, obj in items if isinstance(obj.get("id"), str)}


def load_legacy(path):
    placeholder = {
        "status": "placeholder",
        "kernel_pct": None,
        "app_pct": None,
        "as_of_commit": None,
        "frozen": True,
        "source": None,
    }
    if not path or not os.path.isfile(path):
        return placeholder
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    result = dict(placeholder)
    result.update({k: data[k] for k in placeholder if k in data})
    result["status"] = "frozen" if data.get("kernel_pct") is not None or data.get("app_pct") is not None else "placeholder"
    return result


def compute(records, evidence, now, previous=None, legacy=None, pace=None, run_id=None, commit=None):
    """Return (percent dict, credits list). Pure: no file access."""
    previous = previous or {}
    states = judge_tests(evidence)
    first = {}
    duplicate_ids = 0
    without_id = 0
    for record in records:
        row_id = record["row"].get("id")
        if not isinstance(row_id, str):
            without_id += 1
        else:
            if row_id in first:
                duplicate_ids += 1
                continue
            first[row_id] = record
    rows = [record["row"] for record in first.values()]
    alias_rows = [r for r in rows if isinstance(r.get("alias_of"), str)]
    counted = [r for r in rows if not isinstance(r.get("alias_of"), str)]

    overall = _bucket()
    by_program = collections.defaultdict(_bucket)
    by_factory = collections.defaultdict(_bucket)
    by_wave = collections.defaultdict(_bucket)
    typed = collections.Counter()
    computed = collections.Counter()
    breakdown = collections.Counter()
    credits = []
    unproven = []
    pending = 0
    verdicts = {}

    for row in counted:
        verdict = evaluate_row(row, states)
        verdicts[row["id"]] = verdict
        _add(overall, verdict)
        programs = row.get("program") if isinstance(row.get("program"), list) else []
        for program in programs or ["unknown"]:
            _add(by_program[str(program)], verdict)
        _add(by_factory[str(row.get("factory") or "unknown")], verdict)
        wave = row.get("wave")
        _add(by_wave[str(wave) if isinstance(wave, int) and not isinstance(wave, bool) else "unassigned"], verdict)
        claimed = regio.tier_of(row)
        if not isinstance(claimed, str) or claimed not in regio.TIER_RANK:
            claimed = "invalid"
        typed[claimed] += 1
        computed[verdict["tier"] if verdict["passing"] else "None"] += 1
        if not verdict["passing"]:
            breakdown[primary_reason(verdict["reasons"])] += 1
        if claimed in regio.CREDITED_TIERS:
            if regio.TIER_RANK[claimed] > regio.TIER_RANK[verdict["tier"] if verdict["passing"] else "None"]:
                unproven.append(
                    {
                        "id": row["id"],
                        "claimed": claimed,
                        "computed": verdict["tier"] if verdict["passing"] else "None",
                        "reasons": verdict["reasons"],
                    }
                )
        elif verdict["passing"]:
            pending += 1
        if verdict["passing"]:
            before = previous.get(row["id"])
            at = verdict["at"] or (before.get("at") if before else None) or ledgerlib.format_time(now)
            credits.append(
                {
                    "id": row["id"],
                    "tier": verdict["tier"],
                    "run": verdict["run"],
                    "at": at,
                    "tests": verdict["tests"],
                }
            )
    credits.sort(key=lambda c: c["id"])

    reverted = []
    credited_now = {c["id"] for c in credits}
    for row_id in sorted(previous):
        if row_id in credited_now:
            continue
        if row_id not in first:
            reverted.append({"id": row_id, "reasons": ["row-removed (needs a written rationale)"]})
        elif row_id in verdicts:
            reverted.append({"id": row_id, "reasons": verdicts[row_id]["reasons"]})
        else:
            reverted.append({"id": row_id, "reasons": ["now an alias row"]})

    flaky = sorted(t for t, s in states.items() if s["state"] == "conflicting")
    vanished = sorted(t for t, s in states.items() if s["state"] == "vanished")
    failing = sorted(t for t, s in states.items() if s["state"] == "failing")

    total = overall["total"]
    passing = overall["passing"]
    overall = _finish(overall)
    legacy = dict(legacy or load_legacy(None))
    legacy["register_percent"] = overall["percent"]
    kernel = legacy.get("kernel_pct")
    app = legacy.get("app_pct")
    legacy["dual"] = "Register %d/%d rows passing (%.2f%%); legacy parity map Kernel %s App %s (frozen at %s, not Register credit)" % (
        passing,
        total,
        overall["percent"],
        "%.1f%%" % kernel if isinstance(kernel, (int, float)) else "n/a",
        "%.1f%%" % app if isinstance(app, (int, float)) else "n/a",
        legacy.get("as_of_commit") or "n/a",
    )

    out = {
        "schema": SCHEMA_VERSION,
        "generated_at": ledgerlib.format_time(now),
        "produced_by": {"run_id": run_id, "commit": commit},
        "headline": "%d of %d Register rows passing (%.2f%%)" % (passing, total, overall["percent"]),
        "total": total,
        "passing": passing,
        "percent": overall["percent"],
        "best": overall["best"],
        "target_met": overall["target_met"],
        "target_met_percent": overall["target_met_percent"],
        "by_program": {k: _finish(v) for k, v in sorted(by_program.items())},
        "by_factory": {k: _finish(v) for k, v in sorted(by_factory.items())},
        "by_wave": {k: _finish(v) for k, v in sorted(by_wave.items())},
        "register": {
            "files": len({r["file"] for r in records}),
            "rows": len(rows),
            "aliases": len(alias_rows),
            "duplicate_ids_ignored": duplicate_ids,
            "rows_without_id_ignored": without_id,
            "tiers_written_in_register": dict(sorted(typed.items())),
            "tiers_computed": dict(sorted(computed.items())),
            "rows_passing_but_not_yet_written": pending,
        },
        "not_passing_by_reason": dict(sorted(breakdown.items())),
        "evidence": {
            "runs": len(evidence.runs),
            "latest_run": max(evidence.runs, key=run_order) if evidence.runs else None,
            "files": evidence.files,
            "checks": evidence.checks,
            "tests": len(evidence.obs),
            "sha256": evidence.digest.hexdigest(),
            "malformed_count": len(evidence.malformed),
            "malformed": evidence.malformed[:LIST_LIMIT],
        },
        "flaky_tests": {"count": len(flaky), "ids": flaky[:LIST_LIMIT]},
        "vanished_tests": {"count": len(vanished), "ids": vanished[:LIST_LIMIT]},
        "failing_tests": {"count": len(failing), "ids": failing[:LIST_LIMIT]},
        "unproven_claims": {"count": len(unproven), "rows": unproven[:LIST_LIMIT]},
        "reverted": {"count": len(reverted), "rows": reverted[:LIST_LIMIT]},
        "legacy": legacy,
    }
    if pace is not None:
        out["trajectory"] = ledgerlib.trajectory(pace, total, passing, now)
    return out, credits


# --------------------------------------------------------------------------------------
# Command line
# --------------------------------------------------------------------------------------

_VOLATILE = ("generated_at", "produced_by")


def _stable(data):
    return {k: v for k, v in data.items() if k not in _VOLATILE}


def run(argv=None, out=None):
    parser = argparse.ArgumentParser(description="Compute ledger/percent.json from the Register and the evidence.")
    parser.add_argument("--register", help="register directory (default parity/register)")
    parser.add_argument("--evidence", help="evidence directory (default ledger/evidence)")
    parser.add_argument("--checks", nargs="*", default=[], help="extra JSONL check files, one run called 'adhoc'")
    parser.add_argument("--legacy", help="frozen legacy numbers (default ledger/legacy.json)")
    parser.add_argument("--pace", help="pace line (default ledger/pace.json, else the plan 6.4 defaults)")
    parser.add_argument("--previous", help="previous credits file (default: the --credits-out file)")
    parser.add_argument("--out", help="output file (default ledger/percent.json, '-' for stdout)")
    parser.add_argument("--credits-out", help="credits JSONL (default ledger/credits.jsonl)")
    parser.add_argument("--now", help="time to use instead of the clock (ISO 8601 UTC)")
    parser.add_argument("--run-id", default=os.environ.get("GITHUB_RUN_ID"))
    parser.add_argument("--commit", default=os.environ.get("GITHUB_SHA"))
    parser.add_argument("--check", action="store_true", help="do not write; fail when percent.json differs")
    parser.add_argument("--strict", action="store_true", help="fail on malformed evidence or unproven claims")
    args = parser.parse_args(argv)
    out = out or sys.stdout

    register = args.register or DEFAULT_REGISTER
    if args.register and not os.path.isdir(register):
        print("percent: no such register directory: %s" % register, file=sys.stderr)
        return 2
    evidence_dir = args.evidence or DEFAULT_EVIDENCE
    if args.evidence and not os.path.isdir(evidence_dir):
        print("percent: no such evidence directory: %s" % evidence_dir, file=sys.stderr)
        return 2
    for path in args.checks:
        if not os.path.isfile(path):
            print("percent: no such checks file: %s" % path, file=sys.stderr)
            return 2
    try:
        now = ledgerlib.parse_time(args.now) if args.now else ledgerlib.utcnow()
    except ValueError as exc:
        print("percent: %s" % exc, file=sys.stderr)
        return 2

    out_path = args.out or DEFAULT_OUT
    credits_path = args.credits_out or DEFAULT_CREDITS
    try:
        pace = ledgerlib.load_pace(args.pace or (DEFAULT_PACE if os.path.isfile(DEFAULT_PACE) else None))
        legacy = load_legacy(args.legacy or DEFAULT_LEGACY)
    except (OSError, ValueError, KeyError) as exc:
        print("percent: %s" % exc, file=sys.stderr)
        return 2

    records, problems = regio.read_register(register) if os.path.isdir(register) else ([], [])
    for problem in problems:
        if problem["severity"] == "error":
            print("percent: %s:%s: %s" % (problem["file"], problem["line"], problem["message"]), file=sys.stderr)
    previous = read_credits(args.previous or credits_path)
    evidence = load_evidence(evidence_dir, args.checks)
    for item in evidence.malformed:
        print("percent: malformed evidence %s%s: %s" % (item["file"], (":%s" % item["line"]) if item["line"] else "", item["message"]), file=sys.stderr)

    result, credits = compute(records, evidence, now, previous, legacy, pace, args.run_id, args.commit)

    status = 0
    if args.check:
        try:
            with open(out_path, "r", encoding="utf-8") as handle:
                committed = json.load(handle)
        except (OSError, ValueError) as exc:
            print("percent: cannot read %s: %s" % (out_path, exc), file=sys.stderr)
            return 1
        # trajectory depends on the clock; the rest depends only on the inputs.
        left, right = _stable(committed), _stable(result)
        for volatile in ("trajectory", "reverted"):  # clock and history dependent
            left.pop(volatile, None)
            right.pop(volatile, None)
        if left != right:
            keys = sorted(k for k in set(left) | set(right) if left.get(k) != right.get(k))
            print("percent: %s is not what the Register and the evidence compute (differs in: %s). "
                  "It is written by CI only." % (out_path, ", ".join(keys)), file=sys.stderr)
            return 1
        out.write("percent: %s matches the recomputation (%s)\n" % (out_path, result["headline"]))
        return 0

    text = ledgerlib.dump_json(result)
    if out_path == "-":
        out.write(text)
    else:
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        with open(credits_path, "w", encoding="utf-8", newline="\n") as handle:
            for credit in credits:
                handle.write(json.dumps(credit, sort_keys=True) + "\n")
        out.write("percent: %s\n" % result["headline"])
    if args.strict and (evidence.malformed or result["unproven_claims"]["count"]):
        print("percent: strict: %d malformed evidence entries, %d unproven claims" % (len(evidence.malformed), result["unproven_claims"]["count"]), file=sys.stderr)
        status = 1
    return status


def main():
    sys.exit(run())


if __name__ == "__main__":
    main()
