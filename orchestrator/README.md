# Orchestrator (shared foundation)

This package is the shared backbone for all five income programs in
[`/modules`](../modules). Build this once; every program plugs into it.

It is **pure Python standard library** — no `pip install` required to run the
core (config, database, logging, scheduler, dashboard).

## What's in here

| File | Responsibility |
|------|----------------|
| `config.py` | Secure config/credentials. Reads a git-ignored `.env`; nothing is hardcoded. `config.require("KEY")` fails loudly if a key is missing. |
| `database.py` | The shared SQLite store: modules, activity log, status, earnings, review queue. Read/write helpers. |
| `logger.py` | The API modules use: `get_logger("<module>")` → `.activity()`, `.status()`, `.earning()`, `.flag_for_review()` (optionally pushes a webhook notification — see `notifier.py`). |
| `notifier.py` | Optional push notification (Slack/Discord/generic webhook) whenever any module flags something for review or errors out. Unconfigured = silent no-op. |
| `scheduler.py` | Trigger scripts on a cadence. Generates cron entries (Linux/macOS) **or** runs a portable Python daemon (any OS, incl. Windows). |
| `dashboard.py` | A local web dashboard: per-module status with a **Run now** button, earnings (with a CSV export for bookkeeping), an interactive review queue (with a recently-resolved audit trail of past approve/reject decisions), and an activity feed. |
| `cli.py` / `__main__.py` | `python -m orchestrator <command>`. |
| `demo.py` | Seeds sample data so you can explore the dashboard immediately. |
| `paths.py` | Single source of truth for file locations and the module list. |
| `jobs.example.json` | Scheduler job templates (copy to `jobs.json`). |

## Commands

```bash
python -m orchestrator init          # create the SQLite database + tables
python -m orchestrator demo          # seed sample data to explore the UI
python -m orchestrator dashboard     # launch the dashboard (http://127.0.0.1:8787)
python -m orchestrator doctor        # check setup / which credentials are set
python -m orchestrator export-earnings [--module NAME] [--since YYYY-MM-DD] [--out FILE]
                                      # earnings ledger as CSV (stdout by default)
python -m orchestrator export-reviews [--module NAME] [--since YYYY-MM-DD] [--out FILE]
                                      # full approve/reject decision log as CSV
python -m orchestrator scheduler list       # preview jobs + their cron lines
python -m orchestrator scheduler install     # install enabled jobs into cron
python -m orchestrator scheduler uninstall   # remove them from cron
python -m orchestrator scheduler run         # portable scheduler daemon (any OS)
```

## How a module uses the foundation

```python
from orchestrator import get_logger, config

log = get_logger("deal_alert_bot")   # name must match a folder in /modules

def run():
    log.status("running", "Scanning for deals")
    token = config.require("TELEGRAM_BOT_TOKEN")   # from .env, never hardcoded
    # ... work ...
    log.activity("Scanned 200 listings, found 3 deals")
    log.earning(4.20, source="amazon", description="Affiliate commission")
    log.flag_for_review("Post this deal?", payload={"url": "..."})
    log.status("ok", "Done")
```

`flag_for_review(...)` and `status("error", ...)` above also push a
notification if you've set `NOTIFY_WEBHOOK_URL` in `.env` — no code changes
needed in any module to get one, it's handled once in the shared logger.

See the top-level [`SETUP.md`](../SETUP.md) for the full first-time walkthrough.
