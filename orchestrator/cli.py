"""Unified command-line entry point.

python -m orchestrator init          # create the database + tables
python -m orchestrator dashboard     # launch the local dashboard
python -m orchestrator scheduler ... # manage scheduled jobs
python -m orchestrator demo          # seed sample data to explore the UI
python -m orchestrator doctor        # check your setup / which keys are set
python -m orchestrator export-earnings  # write the earnings ledger as CSV
python -m orchestrator export-reviews   # write the review decision log as CSV
"""

from __future__ import annotations

import argparse
import csv
import sys
from typing import Any

from . import database as db
from .config import config
from .database import EARNINGS_CSV_FIELDS, REVIEWS_CSV_FIELDS
from .paths import DB_PATH, ENV_PATH, JOBS_PATH, MODULES


def _write_csv(rows: list[dict[str, Any]], fields: list[str], handle) -> None:
    writer = csv.DictWriter(handle, fieldnames=fields)
    writer.writeheader()
    for row in rows:
        writer.writerow({field: row[field] for field in fields})


def _cmd_init() -> int:
    db.init_db()
    print(f"Database ready at {DB_PATH}")
    print(f"Registered {len(MODULES)} modules: {', '.join(MODULES)}")
    return 0


def _cmd_doctor() -> int:
    print("Income Orchestrator — setup check\n")
    print(
        f"  .env file:   {'found' if ENV_PATH.exists() else 'MISSING (copy .env.example)'}"
    )
    print(
        f"  database:    {'exists' if DB_PATH.exists() else 'not created yet (run: init)'}"
    )
    print(
        f"  jobs.json:   {'found' if JOBS_PATH.exists() else 'using jobs.example.json defaults'}"
    )
    print("\n  Credentials detected in .env:")
    tracked = [
        "ANTHROPIC_API_KEY",
        "SHOPIFY_ADMIN_API_TOKEN",
        "STRIPE_SECRET_KEY",
        "ETSY_API_KEY",
        "TELEGRAM_BOT_TOKEN",
        "SHUTTERSTOCK_API_TOKEN",
    ]
    for key in tracked:
        print(f"    {'set ' if config.has(key) else 'unset'}  {key}")
    print("\n  (Unset keys are fine — you only need them for modules you build.)")
    return 0


def _cmd_demo() -> int:
    from .demo import seed

    seed()
    return 0


def _cmd_export_earnings(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="orchestrator export-earnings",
        description="Write the earnings ledger as CSV, for bookkeeping/taxes.",
    )
    parser.add_argument("--module", default=None, help="only this module's earnings")
    parser.add_argument(
        "--since", default=None, help="only earnings on/after this date (YYYY-MM-DD)"
    )
    parser.add_argument("--out", default=None, help="file to write (default: stdout)")
    args = parser.parse_args(argv)

    rows = db.list_earnings(module=args.module, since=args.since)

    if args.out:
        with open(args.out, "w", newline="", encoding="utf-8") as handle:
            _write_csv(rows, EARNINGS_CSV_FIELDS, handle)
        print(f"Wrote {len(rows)} earning(s) to {args.out}")
    else:
        _write_csv(rows, EARNINGS_CSV_FIELDS, sys.stdout)
    return 0


def _cmd_export_reviews(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="orchestrator export-reviews",
        description="Write the review decision log (approved/rejected) as CSV.",
    )
    parser.add_argument("--module", default=None, help="only this module's decisions")
    parser.add_argument(
        "--since", default=None, help="only decisions on/after this date (YYYY-MM-DD)"
    )
    parser.add_argument("--out", default=None, help="file to write (default: stdout)")
    args = parser.parse_args(argv)

    rows = db.list_resolved_reviews(module=args.module, since=args.since)

    if args.out:
        with open(args.out, "w", newline="", encoding="utf-8") as handle:
            _write_csv(rows, REVIEWS_CSV_FIELDS, handle)
        print(f"Wrote {len(rows)} decision(s) to {args.out}")
    else:
        _write_csv(rows, REVIEWS_CSV_FIELDS, sys.stdout)
    return 0


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print(__doc__)
        return 0
    cmd, rest = argv[0], argv[1:]

    if cmd == "init":
        return _cmd_init()
    if cmd == "doctor":
        return _cmd_doctor()
    if cmd == "demo":
        return _cmd_demo()
    if cmd == "export-earnings":
        return _cmd_export_earnings(rest)
    if cmd == "export-reviews":
        return _cmd_export_reviews(rest)
    if cmd == "dashboard":
        from .dashboard import main as dash_main

        return dash_main(rest)
    if cmd == "scheduler":
        from .scheduler import main as sched_main

        return sched_main(rest)
    print(f"Unknown command: {cmd}\n")
    print(__doc__)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
