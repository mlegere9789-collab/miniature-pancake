# Income Orchestrator — Setup Guide

This is the shared foundation for five semi-autonomous income programs, all
five of which are already built (see the table in the root
[`README.md`](README.md) — each has a documented scope, and some
deliberately stop short of live uploads/publishing where no reliable public
API exists to build against). This guide walks you through everything
**you** need to do to get it running locally.

Estimated time: ~10 minutes. No accounts or paid services are required just to
run the foundation — you only need API keys later, per program.

---

## 1. Prerequisites (install these yourself)

- **Python 3.9 or newer.** Check with:
  ```bash
  python3 --version
  ```
  If you don't have it: [python.org/downloads](https://www.python.org/downloads/)
  (on Windows, tick *"Add Python to PATH"* during install).

That's it. The foundation uses only Python's standard library — **nothing to
`pip install`** to run config, database, logging, scheduler, or dashboard.

---

## 2. Get the code and open a terminal in this folder

```bash
cd path/to/miniature-pancake
```

---

## 3. Create your secrets file

All API keys live in **one** git-ignored file, `.env`. Nothing is ever
hardcoded into scripts.

```bash
cp .env.example .env
chmod 600 .env        # macOS/Linux: make it readable only by you
```

On **Windows** (PowerShell):
```powershell
Copy-Item .env.example .env
```

Open `.env` in an editor. You can leave everything blank for now — you only
need a key when you build the program that uses it. `.env` will **never** be
committed (it's in `.gitignore`); only the blank `.env.example` template is
tracked.

> **How secrets are used in code:** modules call
> `config.require("SHOPIFY_ADMIN_API_TOKEN")`, which reads from `.env`. If a
> key is missing, you get a clear error telling you exactly what to add —
> instead of a confusing failure deep inside an API call.

---

## 4. Initialize the database

```bash
python3 -m orchestrator init
```

This creates `data/orchestrator.db` (SQLite) with all tables. The `data/`
folder is git-ignored, so your local database and logs never get committed.

---

## 5. See it working (sample data)

```bash
python3 -m orchestrator demo          # seed example activity/earnings/reviews
python3 -m orchestrator dashboard     # start the dashboard
```

Open **http://127.0.0.1:8787** in your browser. You'll see:
- a summary strip (total earnings, items awaiting review) with a **Download
  CSV** link for the full earnings ledger (bookkeeping/taxes — also
  available as `python3 -m orchestrator export-earnings`),
- a card per module (status, last activity, earnings, pending reviews) with
  a **Run now** button — trigger that module immediately instead of waiting
  for its schedule — and a **View log** link showing its recent output,
- the **review queue** with working **Approve** / **Reject** buttons,
- **Recently resolved**, an audit trail of your last few approve/reject
  decisions (so you can see what you decided, and when), with its own
  **Download CSV** link for the complete decision log (also available as
  `python3 -m orchestrator export-reviews`),
- a recent-activity feed.

Want a push notification the moment anything lands in the review queue, or
a module errors out, instead of checking the dashboard? Set
`NOTIFY_WEBHOOK_URL` in `.env` to a Slack or Discord incoming webhook URL
(`NOTIFY_FORMAT` picks the payload shape — `slack`, `discord`, or the
default `generic`, which works with most other webhook receivers too).
Blank = off, no notifications sent, same idle-by-default rule as everything
else here.

Stop the dashboard with **Ctrl-C**. To wipe the sample data and start clean,
delete `data/orchestrator.db` and run `init` again.

The dashboard binds to `127.0.0.1` (your machine only) by default. Change the
host/port via `DASHBOARD_HOST` / `DASHBOARD_PORT` in `.env` if needed.

---

## 6. Set up scheduling (when a program is ready)

Jobs are defined in `orchestrator/jobs.json`. Start from the template:

```bash
cp orchestrator/jobs.example.json orchestrator/jobs.json
```

Every job ships **disabled** so nothing runs until you say so. When a module is
ready, open `jobs.json`, set that job's `"enabled": true`, then pick one of two
ways to run it:

### Option A — cron (recommended on macOS/Linux)
```bash
python3 -m orchestrator scheduler list        # preview the cron lines
python3 -m orchestrator scheduler install      # add them to your crontab
python3 -m orchestrator scheduler uninstall    # remove them later
```
This installs a clearly-marked block in your personal crontab. Your machine
must be on at the scheduled time for a job to run.

### Option B — portable daemon (any OS, including Windows)
```bash
python3 -m orchestrator scheduler run
```
Leave this running in a terminal; it triggers jobs when they're due. On Windows
(no cron), this is the way. For always-on scheduling later, you'd run this under
a process manager (systemd, `pm2`, Windows Task Scheduler, etc.) — we can set
that up when you have a program to schedule.

**Cadence options** (in `jobs.json`): `hourly`, `daily` (with `"at": "HH:MM"`),
or `weekly` (with `"at": "mon 09:00"`).

---

## 7. Check your setup anytime

```bash
python3 -m orchestrator doctor
```
Shows whether `.env` exists, whether the database is created, and which
credentials are set (values are never printed).

---

## Accounts you'll create later (one per program, only when you build it)

You do **not** need these to run the foundation. Each program's README lists
what it needs; in summary:

| Program | Likely accounts / keys |
|---------|------------------------|
| Stock licensing | Adobe Stock / Shutterstock contributor accounts (ID + tax verification) |
| E-commerce / dropshipping | Shopify store + custom app token, Stripe, a supplier |
| Deal-alert bot | Telegram bot (@BotFather) or Discord webhook, Amazon Associates |
| Digital products | Etsy seller + developer app, and/or Gumroad token |
| Micro-SaaS | Stripe, plus wherever the SaaS itself is hosted |
| Any (AI copy/automation) | Anthropic API key ([console.anthropic.com](https://console.anthropic.com/)) |

Set the keys for whichever program you want to run in `.env`, enable its job
in `jobs.json` (step 6), and it'll pick them up automatically — `doctor`
(step 7) confirms what's set.

---

## Security notes

- **Secrets stay in `.env`**, which is git-ignored. Never paste keys into code,
  commits, or the dashboard.
- Run `chmod 600 .env` so only your user can read it (the config loader warns
  you if it's readable by others).
- The dashboard listens on loopback only by default — it is not exposed to your
  network or the internet.
- `data/` (database, logs, scheduler state) is git-ignored too.

---

## Project layout

```
miniature-pancake/
├─ orchestrator/          # the shared foundation (this is the engine)
│  ├─ config.py           #   secure credentials from .env
│  ├─ database.py         #   shared SQLite store
│  ├─ logger.py           #   get_logger(...) API for modules
│  ├─ notifier.py         #   optional webhook push on review-flag
│  ├─ scheduler.py        #   cron + portable daemon
│  ├─ dashboard.py        #   local web dashboard + review queue
│  ├─ demo.py, cli.py     #   helpers / CLI
│  └─ jobs.example.json   #   scheduler job templates
├─ modules/               # the five income programs, all built
│  ├─ stock_licensing/        #   built: AI metadata drafts for review
│  ├─ ecommerce_dropshipping/ #   built: Shopify order sync + margin tracking
│  ├─ deal_alert_bot/         #   built: CheapShark -> Discord deal alerts
│  ├─ digital_products/       #   built: AI listing-copy drafts for review
│  └─ micro_saas/             #   built: health checks + Stripe billing
├─ data/                  # local DB + logs (git-ignored)
├─ .env.example           # credentials template (copy to .env)
└─ SETUP.md               # this file
```
