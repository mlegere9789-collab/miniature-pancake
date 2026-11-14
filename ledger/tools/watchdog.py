#!/usr/bin/env python3
"""Watchdog: apply the stall and pace rules of plan 7.6 and print incidents as JSON.

    python3 ledger/tools/watchdog.py [--ledger DIR] [--remote R | --claims-file FILE]
        [--now T] [--write-incidents DIR] [--fail-on-incident] [thresholds...]

It is independent of the workers and stateless: every run reads the ledger and the claim refs
and recomputes everything, so it can run from a scheduled workflow every 5 to 10 minutes and
a missed run loses nothing.

Inputs
  ledger/heartbeats/<date>/<agent>.jsonl  one JSON object per line, append-only:
      {"time","what","commit","evidence"} plus optional "task", "metric" (a number),
      "state" (working | waiting | idle | done | released | handoff), "waiting_on"
      (a CI run id or other external wait).
  claim refs (refs/heads/claim/<task>)    from --remote, or a JSON list from --claims-file
  ledger/credits.jsonl, ledger/percent.json, ledger/pace.json     for the pace rule
  ledger/tasks/<task>.json                "retries" for the relaunch count

Rules (plan 7.6; thresholds are options)
  late_first_output  10 min  no commit or evidence in any heartbeat since the claim or launch
  no_heartbeat       15 min  an active agent has written no heartbeat
  no_change          3       the last three heartbeats show no measurable change: same task,
                             commit, evidence, metric and state (the "what" text is ignored).
                             Waiting on a CI run (waiting_on) is excused for 40 minutes.
  claim_expired      45 min  no claim, renew or heartbeat of the owner for the task
  commit_overdue     30 min  the commit in the heartbeats has not changed (warning)
  pace_miss          for each rolling six-hour window after wave 0, rows verified must be at
                     least 50% of one quarter of that day's required rows (plan 6.4)
  replan_trigger     two consecutive six-hour pace misses (plan 11.2)
Stalled agents carry action relaunch, or split_task once two relaunches are used up.
Every incident has a stable id (type, subject, start time), so repeated runs report the same
incident, and --write-incidents writes ledger/incidents/<id>.md only if it does not exist.

Exit status: 0, or 1 with --fail-on-incident when an incident of severity high or above
exists, or 2 for an input problem.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import claims as claimlib  # noqa: E402
import ledgerlib  # noqa: E402

TERMINAL_STATES = ("done", "released", "handoff", "idle", "closed", "finished")
SEVERITY_RANK = {"critical": 0, "high": 1, "warn": 2, "info": 3}
MAX_RELAUNCHES = 2

DEFAULTS = {
    "heartbeat_timeout": 15.0,
    "no_change_beats": 3,
    "first_output": 10.0,
    "claim_expiry": 45.0,
    "commit_interval": 30.0,
    "ci_wait_grace": 40.0,
    "lookback_days": 1,
    "root_cause_hours": 2.0,
}


def _stamp(when):
    return when.strftime("%Y%m%dT%H%M%SZ")


def _incident(kind, severity, subject, since, now, detail, rule, action, **extra):
    incident = {
        "id": "%s-%s-%s" % (kind, subject, _stamp(since)),
        "type": kind,
        "severity": severity,
        "since": ledgerlib.format_time(since),
        "detected_at": ledgerlib.format_time(now),
        "detail": detail,
        "rule": rule,
        "action": action,
    }
    incident.update(extra)
    return incident


def fingerprint(beat):
    """What counts as a measurable change between two heartbeats."""
    return (beat["task"], beat["commit"], beat["evidence"], beat["metric"], beat["state"], beat["waiting_on"])


def is_active(beats):
    return bool(beats) and (beats[-1]["state"] or "working").lower() not in TERMINAL_STATES


def unchanged_tail(beats, count):
    """The trailing run of heartbeats that share the last fingerprint (a list, newest last)."""
    tail = []
    for beat in reversed(beats):
        if tail and fingerprint(beat) != fingerprint(tail[-1]):
            break
        tail.append(beat)
    tail.reverse()
    return tail


def relaunch_action(relaunches):
    return "relaunch" if relaunches < MAX_RELAUNCHES else "split_task"


def relaunches_for(task, claim_by_task, task_files):
    count = 0
    claim = claim_by_task.get(task)
    if claim:
        count = max(count, int(claim.get("attempt") or 1) - 1)
    info = task_files.get(task) or {}
    retries = info.get("retries")
    if isinstance(retries, int) and not isinstance(retries, bool):
        count = max(count, retries)
    return count


def read_task_files(directory):
    found = {}
    if directory and os.path.isdir(directory):
        for name in sorted(os.listdir(directory)):
            if name.endswith(".json"):
                try:
                    with open(os.path.join(directory, name), "r", encoding="utf-8") as handle:
                        data = json.load(handle)
                except (OSError, ValueError):
                    continue
                if isinstance(data, dict):
                    found[data.get("id") or name[:-5]] = data
    return found


def read_credit_times(path):
    """Times (aware datetimes) at which rows were credited, from credits.jsonl."""
    times = []
    if path and os.path.isfile(path):
        items, _ = ledgerlib.read_jsonl(path)
        for _, obj in items:
            when = ledgerlib.try_parse_time(obj.get("at"))
            if when:
                times.append(when)
    return times


def count_between(times, start, end):
    return sum(1 for t in times if start < t <= end)


def check_pace(pace, total, credit_times, now, config):
    """The pace rule: returns (summary dict, incidents list)."""
    hours = float(pace.get("window_hours", 6))
    window = datetime.timedelta(hours=hours)
    rules_start = ledgerlib.try_parse_time(pace.get("pace_rules_start"))
    summary = {"window_hours": hours, "evaluated": False}
    incidents = []
    current_start, previous_start = now - window, now - 2 * window
    if rules_start is not None and current_start < rules_start:
        summary["status"] = "wave0_or_warmup"
        return summary, incidents
    minimum = ledgerlib.window_minimum(pace, total, now)
    if minimum <= 0:
        summary["status"] = "no_requirement"
        return summary, incidents
    verified = count_between(credit_times, current_start, now)
    summary.update(
        {
            "evaluated": True,
            "window_start": ledgerlib.format_time(current_start),
            "window_end": ledgerlib.format_time(now),
            "rows_verified": verified,
            "rows_required": round(minimum, 2),
            "required_per_day": round(ledgerlib.required_per_day(pace, total, now), 1),
            "met": verified >= minimum,
        }
    )
    summary["status"] = "met" if summary["met"] else "missed"
    if summary["met"]:
        return summary, incidents
    due = now + datetime.timedelta(hours=config["root_cause_hours"])
    detail = "%d rows verified in the six hours to %s; the minimum is %.1f (50%% of a quarter of %.0f rows per day)" % (
        verified, summary["window_end"], minimum, summary["required_per_day"])
    incidents.append(
        _incident(
            "pace_miss", "high", "register", current_start, now, detail,
            "7.6 pace: rows verified per six-hour window >= 50% of one quarter of the day's required rows",
            "reallocate",
            root_cause_due=ledgerlib.format_time(due),
            rows_verified=verified,
            rows_required=round(minimum, 2),
        )
    )
    previous_end = now - window
    previous_min = ledgerlib.window_minimum(pace, total, previous_end)
    already = rules_start is None or previous_start >= rules_start
    if already and previous_min > 0:
        previous_verified = count_between(credit_times, previous_start, previous_end)
        summary["previous_window"] = {
            "rows_verified": previous_verified,
            "rows_required": round(previous_min, 2),
            "met": previous_verified >= previous_min,
        }
        if previous_verified < previous_min:
            incidents.append(
                _incident(
                    "replan_trigger", "critical", "register", previous_start, now,
                    "two consecutive six-hour pace misses (%d of %.1f, then %d of %.1f): the re-plan of plan 11.2 is due"
                    % (previous_verified, previous_min, verified, minimum),
                    "11.2 re-plan trigger 2: two consecutive six-hour pace misses",
                    "replan",
                    root_cause_due=ledgerlib.format_time(due),
                )
            )
    return summary, incidents


def evaluate(beats_by_agent, claim_records, now, config, task_files=None, credit_times=None, pace=None, total=None, problems=None):
    """Apply every rule. Returns the report dict (see the module docstring)."""
    task_files = task_files or {}
    problems = problems or []
    incidents = []
    claim_by_task = {c["task"]: c for c in claim_records if c.get("task")}
    claim_by_agent = {}
    for claim in claim_records:
        claim_by_agent.setdefault(claim.get("agent"), []).append(claim)

    agents = {}
    names = sorted(set(beats_by_agent) | {a for a in claim_by_agent if a})
    for name in names:
        beats = beats_by_agent.get(name, [])
        owned = claim_by_agent.get(name, [])
        last = beats[-1] if beats else None
        # a claim newer than the last heartbeat is the agent's latest launch and sign of life
        fresh = []
        for claim in owned:
            times = [ledgerlib.try_parse_time(claim.get(k)) for k in ("claimed_at", "renewed_at")]
            times = [t for t in times if t]
            if times and (last is None or max(times) > last["time"]):
                fresh.append(max(times))
        info = {
            "agent": name,
            "task": (last["task"] if last and last["task"] else (owned[0]["task"] if owned else None)),
            "beats": len(beats),
            "last_heartbeat": ledgerlib.format_time(last["time"]) if last else None,
            "minutes_since_heartbeat": round(ledgerlib.minutes_between(last["time"], now), 1) if last else None,
            "state": (last["state"] if last else None) or ("claimed" if owned else "unknown"),
            "status": "ok",
        }
        active = bool(fresh) or (bool(beats) and is_active(beats))
        if not active:
            info["status"] = "finished"
            agents[name] = info
            continue

        task = info["task"]
        relaunches = relaunches_for(task, claim_by_task, task_files)
        action = relaunch_action(relaunches)
        stalled = False

        # last sign of life: the newest heartbeat, or a newer claim
        candidates = list(fresh)
        if last:
            candidates.append(last["time"])
        sign = max(candidates) if candidates else None
        if sign is not None:
            idle = ledgerlib.minutes_between(sign, now)
            info["minutes_since_sign_of_life"] = round(idle, 1)
            if idle > config["heartbeat_timeout"]:
                stalled = True
                incidents.append(
                    _incident(
                        "no_heartbeat", "high", name, sign, now,
                        "%s has written no heartbeat for %.0f minutes (limit %.0f); task %s" % (name, idle, config["heartbeat_timeout"], task),
                        "7.6 heartbeat: none for 15 minutes marks the agent stalled", action,
                        agent=name, task=task, relaunches=relaunches,
                    )
                )

        # three heartbeats with no measurable change
        if beats and len(beats) >= config["no_change_beats"]:
            tail = unchanged_tail(beats, config["no_change_beats"])
            if len(tail) >= config["no_change_beats"]:
                waiting = tail[-1]["waiting_on"]
                span = ledgerlib.minutes_between(tail[0]["time"], now)
                if waiting and span <= config["ci_wait_grace"]:
                    info["status"] = "waiting"
                    info["waiting_on"] = waiting
                else:
                    stalled = True
                    reason = "waiting on %s for %.0f minutes (grace %.0f)" % (waiting, span, config["ci_wait_grace"]) if waiting else "no change"
                    incidents.append(
                        _incident(
                            "no_change", "high", name, tail[0]["time"], now,
                            "the last %d heartbeats of %s show no measurable change (%s); task %s" % (len(tail), name, reason, task),
                            "7.6 heartbeat: three heartbeats with no measurable change marks the agent stalled", action,
                            agent=name, task=task, relaunches=relaunches, heartbeats_unchanged=len(tail),
                        )
                    )

        # commit at least every 30 minutes
        if beats and beats[-1]["commit"]:
            same = []
            for beat in reversed(beats):
                if beat["commit"] != beats[-1]["commit"]:
                    break
                same.append(beat)
            age = ledgerlib.minutes_between(same[-1]["time"], now)
            if age > config["commit_interval"] and not (info["status"] == "waiting"):
                incidents.append(
                    _incident(
                        "commit_overdue", "warn", name, same[-1]["time"], now,
                        "%s has not committed since %s (%.0f minutes; limit %.0f)" % (name, beats[-1]["commit"], age, config["commit_interval"]),
                        "7.3 work: commit and push at least every 30 minutes", "none",
                        agent=name, task=task,
                    )
                )

        # first verifiable output within 10 minutes of launch
        launch = None
        if owned:
            launches = [ledgerlib.try_parse_time(c.get("claimed_at")) for c in owned]
            launches = [t for t in launches if t]
            launch = max(launches) if launches else None
        if launch is None and beats:
            launch = beats[0]["time"]
        if launch is not None and ledgerlib.minutes_between(launch, now) > config["first_output"]:
            proofs = [b for b in beats if b["time"] >= launch and (b["commit"] or b["evidence"])]
            if not proofs:
                stalled = True
                incidents.append(
                    _incident(
                        "late_first_output", "high", name, launch, now,
                        "%s has produced no commit or evidence %.0f minutes after launch (limit %.0f); task %s"
                        % (name, ledgerlib.minutes_between(launch, now), config["first_output"], task),
                        "7.6 first verifiable output after launch: 10 minutes", action,
                        agent=name, task=task, relaunches=relaunches,
                    )
                )
        if stalled:
            info["status"] = "stalled"
        agents[name] = info

    claim_report = []
    for claim in claim_records:
        row = claimlib.describe(claim, beats_by_agent, now, config["claim_expiry"])
        claim_report.append(row)
        if row["expired"]:
            owner_beats = beats_by_agent.get(row["agent"], [])
            finished = bool(owner_beats) and not is_active(owner_beats)
            alive = ledgerlib.try_parse_time(row["last_alive"]) or now
            relaunches = relaunches_for(row["task"], claim_by_task, task_files)
            incidents.append(
                _incident(
                    "claim_expired", "high", row["task"], alive, now,
                    "claim on %s by %s has had no sign of life for %s minutes (limit %.0f)%s"
                    % (row["task"], row["agent"], row["idle_minutes"], config["claim_expiry"],
                       "; the owner finished but never released it" if finished else ""),
                    "7.3 claims expire after 45 minutes without a heartbeat",
                    "reap_claim" if finished else ("takeover" if relaunches < MAX_RELAUNCHES else "split_task"),
                    agent=row["agent"], task=row["task"], relaunches=relaunches, attempt=row["attempt"],
                )
            )

    pace_summary = {"evaluated": False, "status": "not_configured"}
    if pace is not None and credit_times is not None and total:
        pace_summary, pace_incidents = check_pace(pace, total, credit_times, now, config)
        incidents.extend(pace_incidents)
    elif pace is not None:
        pace_summary = {"evaluated": False, "status": "no_credits_or_total"}

    by_file = {}
    for problem in problems:
        by_file.setdefault(problem["file"], []).append(problem)
    for path, items in sorted(by_file.items()):
        incidents.append(
            _incident(
                "malformed_heartbeat", "info", os.path.splitext(os.path.basename(path))[0], now, now,
                "%d unusable heartbeat lines in %s (first: line %s: %s)" % (len(items), path, items[0]["line"], items[0]["message"]),
                "7.2 heartbeats are append-only JSON lines", "none", file=path,
            )
        )

    incidents.sort(key=lambda i: (SEVERITY_RANK.get(i["severity"], 9), i["since"], i["id"]))
    return {
        "now": ledgerlib.format_time(now),
        "thresholds": config,
        "agents": [agents[name] for name in sorted(agents)],
        "claims": claim_report,
        "pace": pace_summary,
        "incidents": incidents,
    }


def write_incident_files(incidents, directory):
    """Write ledger/incidents/<id>.md for incidents that have no file yet. Never overwrites."""
    written = []
    os.makedirs(directory, exist_ok=True)
    for incident in incidents:
        if incident["severity"] == "info":
            continue
        path = os.path.join(directory, incident["id"] + ".md")
        if os.path.exists(path):
            continue
        lines = [
            "# Incident %s" % incident["id"],
            "",
            "- type: %s" % incident["type"],
            "- severity: %s" % incident["severity"],
            "- since: %s (first detected %s)" % (incident["since"], incident["detected_at"]),
            "- rule: %s" % incident["rule"],
            "- action: %s" % incident["action"],
        ]
        for key in ("agent", "task", "relaunches", "root_cause_due"):
            if incident.get(key) is not None:
                lines.append("- %s: %s" % (key, incident[key]))
        lines += ["", incident["detail"], "", "## Root cause", "", "(to be written by the owner of the task; the plan asks for it within two hours for a pace miss)", ""]
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines))
        written.append(path)
    return written


def run(argv=None, out=None):
    parser = argparse.ArgumentParser(description="Apply the stall and pace rules of plan 7.6.")
    parser.add_argument("--ledger", default=os.path.join(REPO_ROOT, "ledger"), help="ledger directory")
    parser.add_argument("--remote", help="git remote (URL, path or name) holding the claim refs")
    parser.add_argument("--claims-file", help="JSON list of claims instead of --remote")
    parser.add_argument("--now", help="time to use instead of the clock (ISO 8601 UTC)")
    parser.add_argument("--credits", help="credits JSONL (default <ledger>/credits.jsonl)")
    parser.add_argument("--percent", help="percent.json for the Register size (default <ledger>/percent.json)")
    parser.add_argument("--total", type=int, help="Register size, instead of percent.json")
    parser.add_argument("--pace", help="pace line (default <ledger>/pace.json, else the plan 6.4 defaults)")
    parser.add_argument("--write-incidents", metavar="DIR", help="write incident stubs there (default none)")
    parser.add_argument("--fail-on-incident", action="store_true")
    parser.add_argument("--heartbeat-timeout", type=float, default=DEFAULTS["heartbeat_timeout"])
    parser.add_argument("--no-change-beats", type=int, default=DEFAULTS["no_change_beats"])
    parser.add_argument("--first-output", type=float, default=DEFAULTS["first_output"])
    parser.add_argument("--claim-expiry", type=float, default=DEFAULTS["claim_expiry"])
    parser.add_argument("--commit-interval", type=float, default=DEFAULTS["commit_interval"])
    parser.add_argument("--ci-wait-grace", type=float, default=DEFAULTS["ci_wait_grace"])
    parser.add_argument("--lookback-days", type=int, default=DEFAULTS["lookback_days"])
    args = parser.parse_args(argv)
    out = out or sys.stdout

    try:
        now = ledgerlib.parse_time(args.now) if args.now else ledgerlib.utcnow()
    except ValueError as exc:
        print("watchdog: %s" % exc, file=sys.stderr)
        return 2
    config = dict(DEFAULTS)
    config.update(
        heartbeat_timeout=args.heartbeat_timeout,
        no_change_beats=args.no_change_beats,
        first_output=args.first_output,
        claim_expiry=args.claim_expiry,
        commit_interval=args.commit_interval,
        ci_wait_grace=args.ci_wait_grace,
        lookback_days=args.lookback_days,
    )
    ledger = args.ledger
    beats, problems = ledgerlib.read_heartbeats(os.path.join(ledger, "heartbeats"), config["lookback_days"], now)

    claim_records = []
    try:
        if args.claims_file:
            with open(args.claims_file, "r", encoding="utf-8") as handle:
                claim_records = json.load(handle)
            if not isinstance(claim_records, list):
                raise ValueError("the claims file must hold a JSON list")
        elif args.remote:
            with claimlib.Workspace(args.remote) as ws:
                claim_records = list(ws.fetch_claims().values())
    except (OSError, ValueError, claimlib.ClaimError) as exc:
        print("watchdog: cannot read the claims: %s" % exc, file=sys.stderr)
        return 2

    pace_path = args.pace or os.path.join(ledger, "pace.json")
    try:
        pace = ledgerlib.load_pace(pace_path if os.path.isfile(pace_path) else None)
    except (OSError, ValueError, KeyError) as exc:
        print("watchdog: bad pace file: %s" % exc, file=sys.stderr)
        return 2
    credits_path = args.credits or os.path.join(ledger, "credits.jsonl")
    credit_times = read_credit_times(credits_path) if os.path.isfile(credits_path) else None
    total = args.total
    percent_path = args.percent or os.path.join(ledger, "percent.json")
    if total is None and os.path.isfile(percent_path):
        try:
            with open(percent_path, "r", encoding="utf-8") as handle:
                total = int(json.load(handle).get("total") or 0)
        except (OSError, ValueError, TypeError):
            total = None

    report = evaluate(
        beats, claim_records, now, config, read_task_files(os.path.join(ledger, "tasks")),
        credit_times, pace, total, problems,
    )
    if args.write_incidents:
        report["written"] = write_incident_files(report["incidents"], args.write_incidents)
    json.dump(report, out, indent=2, sort_keys=True)
    out.write("\n")
    if args.fail_on_incident and any(i["severity"] in ("critical", "high") for i in report["incidents"]):
        return 1
    return 0


def main():
    sys.exit(run())


if __name__ == "__main__":
    main()
