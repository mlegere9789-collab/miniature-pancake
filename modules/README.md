# Modules

Each subfolder here is one **income-generating program**. They are intentionally
empty for now (just a README describing intent) — we built the shared foundation
in [`/orchestrator`](../orchestrator) first, and each program will be built on
top of it, one at a time.

| Folder | Program |
|--------|---------|
| [`stock_licensing/`](stock_licensing) | Stock asset licensing |
| [`ecommerce_dropshipping/`](ecommerce_dropshipping) | E-commerce / dropshipping ops |
| [`deal_alert_bot/`](deal_alert_bot) | Deal-alert bot |
| [`digital_products/`](digital_products) | Digital product creation |
| [`micro_saas/`](micro_saas) | Micro-SaaS tool |

## The contract every module follows

A module is just Python that uses the shared foundation. The pattern is the
same for all five:

```python
from orchestrator import get_logger, config

log = get_logger("deal_alert_bot")          # must match this folder's name

def run():
    log.status("running", "Scanning for deals")
    api_key = config.require("TELEGRAM_BOT_TOKEN")   # never hardcode secrets
    # ... do the work ...
    log.activity("Scanned 200 listings, found 3 deals")
    log.earning(4.20, source="amazon", description="Affiliate commission")
    log.flag_for_review("Post this deal?", payload={"url": "..."})
    log.status("ok", "Done")

if __name__ == "__main__":
    run()
```

Convention: give each module a `run.py` with a `run()` function so the
scheduler can call `python -m modules.<name>.run`. The scheduler jobs in
[`orchestrator/jobs.example.json`](../orchestrator/jobs.example.json) already
point at that path — flip `"enabled": true` when a module is ready.

What each module gets for free from the foundation:

- **Config/secrets** — `config.require("KEY")` reads from `.env` (never hardcoded).
- **Logging** — `log.activity(...)`, `log.status(...)`, `log.earning(...)`.
- **Review queue** — `log.flag_for_review(...)` surfaces items on the dashboard
  for your manual approval.
- **Scheduling** — add a job entry; cron or the portable daemon runs it.
- **Dashboard** — everything above shows up automatically. No extra wiring.
