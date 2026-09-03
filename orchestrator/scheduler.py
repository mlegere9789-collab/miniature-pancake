"""Task scheduler — trigger module scripts on a cadence.

Two ways to run scheduled jobs, both driven by the same ``jobs.json``:

1. **cron** (recommended on Linux / macOS)  — the native OS scheduler.
   Generates crontab lines and installs them behind a marker block so they
   are easy to remove again::

       python -m orchestrator.scheduler list        # preview cron lines
       python -m orchestrator.scheduler install      # write them to your crontab
       python -m orchestrator.scheduler uninstall    # remove them

2. **portable daemon** (works everywhere, incl. Windows) — a long-running
   Python loop that runs jobs when they are due::

       python -m orchestrator.scheduler run

Cadence grammar (see ``jobs.example.json``):
    hourly            -> top of every hour
    daily   at HH:MM  -> once a day
    weekly  at DOW HH:MM  (DOW = mon|tue|...|sun)  -> once a week

All jobs ship ``"enabled": false`` so nothing runs until you opt in.

The portable daemon writes a heartbeat (``data/scheduler_heartbeat.json``)
on every poll, which the dashboard reads to show whether it's actually
alive -- unlike cron, a long-lived process can die silently with nothing
else to notice.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone
from typing import Any

from .paths import JOBS_EXAMPLE_PATH, JOBS_PATH, PROJECT_ROOT, ensure_data_dir

MARKER_BEGIN = "# >>> income-orchestrator jobs >>>"
MARKER_END = "# <<< income-orchestrator jobs <<<"
STATE_FILE = PROJECT_ROOT / "data" / "scheduler_state.json"

_DOW = {d: i for i, d in enumerate(["mon", "tue", "wed", "thu", "fri", "sat", "sun"])}
_CRON_DOW = {"mon": 1, "tue": 2, "wed": 3, "thu": 4, "fri": 5, "sat": 6, "sun": 0}


# --------------------------------------------------------------------------- #
#  Job loading
# --------------------------------------------------------------------------- #
def load_jobs(*, enabled_only: bool = False) -> list[dict[str, Any]]:
    path = JOBS_PATH if JOBS_PATH.exists() else JOBS_EXAMPLE_PATH
    data = json.loads(path.read_text(encoding="utf-8"))
    jobs = [j for j in data.get("jobs", []) if isinstance(j, dict) and j.get("name")]
    if enabled_only:
        jobs = [j for j in jobs if j.get("enabled")]
    return jobs


def _parse_at(cadence: str, at: str) -> tuple[int, int, int | None]:
    """Return (hour, minute, dow) for a job. dow is None unless weekly."""
    cadence = cadence.lower().strip()
    at = (at or "").strip().lower()
    if cadence == "hourly":
        return (0, 0, None)  # minute 0, hour ignored
    if cadence == "daily":
        hh, mm = (at or "00:00").split(":")
        return (int(hh), int(mm), None)
    if cadence == "weekly":
        parts = at.split()
        if len(parts) != 2 or parts[0] not in _DOW:
            raise ValueError(f"weekly 'at' must be 'dow HH:MM', got {at!r}")
        hh, mm = parts[1].split(":")
        return (int(hh), int(mm), _DOW[parts[0]])
    raise ValueError(f"unknown cadence {cadence!r} (use hourly|daily|weekly|every)")


def _interval_hours(job: dict[str, Any]) -> int:
    """Validate and return the N for an 'every' cadence (1..23 hours)."""
    n = int(job.get("interval_hours", 1))
    if not 1 <= n <= 23:
        raise ValueError("'every' cadence needs interval_hours between 1 and 23")
    return n


# --------------------------------------------------------------------------- #
#  cron mode
# --------------------------------------------------------------------------- #
def to_cron_line(job: dict[str, Any]) -> str:
    cadence = job["cadence"].lower().strip()
    if cadence == "every":
        expr = f"0 */{_interval_hours(job)} * * *"
    else:
        hour, minute, dow = _parse_at(cadence, job.get("at", ""))
        if cadence == "hourly":
            expr = "0 * * * *"
        elif cadence == "daily":
            expr = f"{minute} {hour} * * *"
        else:  # weekly
            cron_dow = _CRON_DOW[[k for k, v in _DOW.items() if v == dow][0]]
            expr = f"{minute} {hour} * * {cron_dow}"
    # cd into project so `python -m modules...` resolves; log to a per-job file.
    logfile = f"data/logs/{job['name']}.log"
    cmd = job["command"]
    return (
        f"{expr} cd {PROJECT_ROOT} && mkdir -p data/logs && "
        f"{cmd} >> {logfile} 2>&1  # {job['name']}"
    )


def _read_crontab() -> str:
    res = subprocess.run(["crontab", "-l"], capture_output=True, text=True)
    return res.stdout if res.returncode == 0 else ""


def _write_crontab(content: str) -> None:
    subprocess.run(["crontab", "-"], input=content, text=True, check=True)


def cmd_list() -> int:
    jobs = load_jobs()
    src = "jobs.json" if JOBS_PATH.exists() else "jobs.example.json (defaults)"
    print(f"Loaded {len(jobs)} job(s) from {src}:\n")
    for j in jobs:
        flag = "ENABLED " if j.get("enabled") else "disabled"
        when = (
            f"every {j.get('interval_hours', 1)}h"
            if j.get("cadence", "").lower().strip() == "every"
            else f"{j['cadence']} {j.get('at', '')}".strip()
        )
        print(f"  [{flag}] {j['name']}  ({when})")
        if j.get("enabled"):
            print(f"            {to_cron_line(j)}")
    enabled = [j for j in jobs if j.get("enabled")]
    if not enabled:
        print('\nNo jobs are enabled yet. Set "enabled": true in jobs.json,')
        print("then run:  python -m orchestrator.scheduler install")
    return 0


def cmd_install() -> int:
    jobs = load_jobs(enabled_only=True)
    if not jobs:
        print("No enabled jobs to install. Enable some in jobs.json first.")
        return 1
    try:
        current = _read_crontab()
    except FileNotFoundError:
        print(
            "ERROR: 'crontab' not found. Use the portable daemon instead:\n"
            "  python -m orchestrator.scheduler run"
        )
        return 2
    # Strip any previous block, then append a fresh one.
    lines, skip = [], False
    for line in current.splitlines():
        if line.strip() == MARKER_BEGIN:
            skip = True
            continue
        if line.strip() == MARKER_END:
            skip = False
            continue
        if not skip:
            lines.append(line)
    block = [MARKER_BEGIN] + [to_cron_line(j) for j in jobs] + [MARKER_END]
    new_content = "\n".join([*lines, *block]).strip() + "\n"
    _write_crontab(new_content)
    print(f"Installed {len(jobs)} job(s) into your crontab. Review with: crontab -l")
    return 0


def cmd_uninstall() -> int:
    try:
        current = _read_crontab()
    except FileNotFoundError:
        print("'crontab' not found; nothing to uninstall.")
        return 0
    lines, skip, removed = [], False, 0
    for line in current.splitlines():
        if line.strip() == MARKER_BEGIN:
            skip = True
            continue
        if line.strip() == MARKER_END:
            skip = False
            continue
        if skip:
            removed += 1
            continue
        lines.append(line)
    _write_crontab("\n".join(lines).strip() + "\n")
    print(f"Removed the orchestrator crontab block ({removed} line(s)).")
    return 0


# --------------------------------------------------------------------------- #
#  portable daemon mode
# --------------------------------------------------------------------------- #
def _load_state() -> dict[str, str]:
    if STATE_FILE.exists():
        try:
            return json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            return {}
    return {}


def _save_state(state: dict[str, str]) -> None:
    ensure_data_dir()
    STATE_FILE.write_text(json.dumps(state, indent=2), encoding="utf-8")


HEARTBEAT_FILE = PROJECT_ROOT / "data" / "scheduler_heartbeat.json"
HEARTBEAT_STALE_SECONDS = 90  # 3 missed polls at the default 30s interval


def _write_heartbeat(poll_seconds: int) -> None:
    ensure_data_dir()
    HEARTBEAT_FILE.write_text(
        json.dumps(
            {
                "pid": os.getpid(),
                "poll_seconds": poll_seconds,
                "beat_at": datetime.now(timezone.utc).isoformat(),
            }
        ),
        encoding="utf-8",
    )


def read_heartbeat() -> dict[str, Any] | None:
    """The portable daemon's last heartbeat, or None if it has never run.

    cron reports its own reliability (a missed run just doesn't happen), but
    the portable daemon (`scheduler run` — the only option on Windows) is a
    long-lived process that can die silently with nothing else to notice.
    The dashboard uses this to show whether it's actually alive.
    """
    if not HEARTBEAT_FILE.exists():
        return None
    try:
        return json.loads(HEARTBEAT_FILE.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def heartbeat_is_stale(
    heartbeat: dict[str, Any], *, now: datetime | None = None
) -> bool:
    now = now or datetime.now(timezone.utc)
    beat_at = datetime.fromisoformat(heartbeat["beat_at"])
    return (now - beat_at).total_seconds() > HEARTBEAT_STALE_SECONDS


def _is_due(job: dict[str, Any], now: datetime, last_run: datetime | None) -> bool:
    cadence = job["cadence"].lower().strip()
    if cadence == "every":
        interval = timedelta(hours=_interval_hours(job))
        # 1-minute slack so a poll that lands just under the interval still fires.
        return last_run is None or (now - last_run) >= interval - timedelta(minutes=1)
    hour, minute, dow = _parse_at(cadence, job.get("at", ""))
    if cadence == "hourly":
        return last_run is None or (now - last_run) >= timedelta(minutes=59)
    # For daily/weekly: due once the scheduled minute has passed today and we
    # have not already run in this period.
    scheduled_today = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
    if cadence == "daily":
        if now < scheduled_today:
            return False
        return last_run is None or last_run < scheduled_today
    if cadence == "weekly":
        if now.weekday() != dow or now < scheduled_today:
            return False
        return last_run is None or last_run < scheduled_today
    return False


def _run_job(job: dict[str, Any]) -> None:
    print(f"[scheduler] running {job['name']}: {job['command']}")
    log_dir = PROJECT_ROOT / "data" / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    logfile = log_dir / f"{job['name']}.log"
    with open(logfile, "a", encoding="utf-8") as fh:
        fh.write(f"\n===== run {datetime.now(timezone.utc).isoformat()} =====\n")
        fh.flush()
        subprocess.Popen(
            job["command"],
            shell=True,
            cwd=str(PROJECT_ROOT),
            stdout=fh,
            stderr=subprocess.STDOUT,
        )


def cmd_run(poll_seconds: int = 30) -> int:
    print("Portable scheduler running. Ctrl-C to stop.")
    print(
        f"Checking {len(load_jobs(enabled_only=True))} enabled job(s) "
        f"every {poll_seconds}s.\n"
    )
    _write_heartbeat(poll_seconds)
    try:
        while True:
            now = datetime.now(timezone.utc)
            state = _load_state()
            for job in load_jobs(enabled_only=True):
                last_raw = state.get(job["name"])
                last_run = datetime.fromisoformat(last_raw) if last_raw else None
                if _is_due(job, now, last_run):
                    _run_job(job)
                    state[job["name"]] = now.isoformat()
            _save_state(state)
            time.sleep(poll_seconds)
            _write_heartbeat(poll_seconds)
    except KeyboardInterrupt:
        print("\n[scheduler] stopped.")
        return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Income orchestrator scheduler")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list", help="preview jobs and their cron lines")
    sub.add_parser("install", help="install enabled jobs into your crontab")
    sub.add_parser("uninstall", help="remove orchestrator jobs from your crontab")
    run_p = sub.add_parser("run", help="run the portable scheduler daemon")
    run_p.add_argument("--poll", type=int, default=30, help="poll interval seconds")
    args = parser.parse_args(argv)

    if args.command == "list":
        return cmd_list()
    if args.command == "install":
        return cmd_install()
    if args.command == "uninstall":
        return cmd_uninstall()
    if args.command == "run":
        return cmd_run(args.poll)
    return 1


if __name__ == "__main__":
    sys.exit(main())
