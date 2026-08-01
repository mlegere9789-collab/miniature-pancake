"""Unified command-line entry point.

python -m orchestrator init          # create the database + tables
python -m orchestrator dashboard     # launch the local dashboard
python -m orchestrator scheduler ... # manage scheduled jobs
python -m orchestrator demo          # seed sample data to explore the UI
python -m orchestrator doctor        # check your setup / which keys are set
"""

from __future__ import annotations

import sys

from . import database as db
from .config import config
from .paths import DB_PATH, ENV_PATH, JOBS_PATH, MODULES


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
