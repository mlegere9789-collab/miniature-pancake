# Income Orchestrator

A shared foundation plus five semi-autonomous income programs, each syncing
its own external service (Shopify, Stripe, a live health endpoint, Claude,
CheapShark/Discord) into one local dashboard: activity feed, status per
program, earnings, and a **review queue** for anything that needs your
manual approval before it acts.

The foundation (`orchestrator/`) is **pure Python standard library** — no
`pip install` required to run config, the database, logging, the scheduler,
or the dashboard. Each program (`modules/`) adds its own stdlib-only HTTP
client for whatever it talks to.

## Quick start

```bash
python3 -m orchestrator init          # create the SQLite database + tables
python3 -m orchestrator demo          # seed sample data to explore the UI
python3 -m orchestrator dashboard     # http://127.0.0.1:8787
```

No accounts or paid services are required just to run the foundation and
explore the dashboard with seeded data — see [`SETUP.md`](SETUP.md) for the
full first-time walkthrough, and [`.env.example`](.env.example) for every
credential a program can use (copy it to `.env` and fill in only what you
need).

## The five programs

| Program | Status | What it does |
|---|---|---|
| [`deal_alert_bot`](modules/deal_alert_bot) | **Built** | Polls CheapShark for game deals meeting a savings/price threshold, posts qualifying ones to a Discord webhook (dry-run by default). |
| [`ecommerce_dropshipping`](modules/ecommerce_dropshipping) | **Built** | Syncs open Shopify orders, computes net margin from a local supplier cost book, flags problem orders (incomplete address, refunds, unknown costs, thin margin) for review. |
| [`micro_saas`](modules/micro_saas) | **Built** | Health-checks a live service and reconciles Stripe billing — MRR, churn, collected revenue, failed charges, refunds. |
| [`digital_products`](modules/digital_products) | **Built** | Drafts marketplace listing copy with Claude from product briefs you queue; every draft is flagged for your approval, nothing auto-publishes. |
| [`stock_licensing`](modules/stock_licensing) | **Built** | Drafts contributor title/description/keyword metadata with Claude from assets you queue; every draft is flagged for your approval, nothing auto-uploads. |

Every program follows the same rules: `.env`-driven configuration with no
hardcoded secrets, idle (not erroring) when its credentials aren't set, a
top-level guard so a bad run never crashes the scheduler, and an offline
unit test suite. Each module's own README documents exactly what it does
and does not do yet — most notably, the parts of the original spec that
need a live paid account or a generic-API-that-doesn't-exist (supplier
fulfillment routing, marketplace publishing/uploading) are called out
explicitly rather than faked.

## How it fits together

1. **`orchestrator/`** — the shared backbone every program plugs into:
   secure `.env`-backed config, a shared SQLite store, the `get_logger(...)`
   API (`.activity()`, `.status()`, `.earning()`, `.flag_for_review()`), a
   scheduler (cron on Linux/macOS, or a portable daemon anywhere), and the
   local dashboard.
2. **`modules/<name>/run.py`** — each program's entry point
   (`python -m modules.<name>.run`), wired into that shared logger.
3. **The dashboard** — everything a module logs shows up automatically: no
   extra wiring per program.
4. **Optional push notifications** — set `NOTIFY_WEBHOOK_URL` in `.env` (a
   Slack or Discord webhook, or most other generic ones) and a flagged
   review item or a module erroring out both push a notification too, for
   every module, with nothing to change per program.

```
orchestrator/    shared foundation (config, database, logger, scheduler, dashboard)
modules/         the five income programs, one folder each
data/            local SQLite DB + per-module state (git-ignored)
.env.example     every credential a program can use (copy to .env)
SETUP.md         full first-time walkthrough
```

## Testing

```bash
python3 -m unittest discover -s modules -p "test_*.py"             # the five programs
python3 -m unittest discover -s orchestrator -t . -p "test_*.py"   # the shared foundation
```

Both suites run fully offline — no network, no credentials — via faked API
responses and a temporary SQLite database (never the real
`data/orchestrator.db`). See `.pre-commit-config.yaml` for the
formatting/lint tooling (`black`, `isort`, `flake8`) run across
`orchestrator/` and `modules/`.

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs that same
lint + both test suites, plus an `orchestrator init`/`doctor` smoke test,
on every push to `main` and every pull request, on Python 3.9 and 3.12
(the range `SETUP.md` claims support for).

## Further reading

- [`SETUP.md`](SETUP.md) — first-time setup walkthrough.
- [`orchestrator/README.md`](orchestrator/README.md) — the shared foundation.
- [`modules/README.md`](modules/README.md) — the module contract every
  program follows.
- Each module's own README — what it does, its configuration, and (where
  relevant) what's deliberately not implemented yet and why.
