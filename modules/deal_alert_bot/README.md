# Deal-Alert Bot

Polls the **CheapShark API** (free, no key) for video-game deals, filters for
ones that beat your threshold, formats them, and posts them to a **Discord**
channel via webhook. Every post (and every error) is logged to the shared
database from the foundation, so it all shows up on the dashboard.

**Ships in dry-run mode** — it logs what it *would* post without sending
anything, so you can review before going live.

---

## Files

| File | What it does |
|------|--------------|
| `run.py` | Entry point: fetch → filter → dedup → post/log. Never crashes the scheduler. |
| `cheapshark.py` | CheapShark API client (stdlib only). |
| `formatter.py` | Turns a deal into a Discord message + affiliate link. |
| `discord_notifier.py` | Posts to the Discord webhook, with error handling. |
| `config.py` | All settings, read from `.env` (nothing hardcoded). |
| `dedup.py` | Remembers posted deals so they aren't reposted. |
| `test_deal_alert.py` | Offline unit tests (no network needed). |

---

## Quick start (dry-run — safe, posts nothing)

From the project root:

```bash
python -m orchestrator init                 # if you haven't already
python -m modules.deal_alert_bot.run        # dry-run by default
python -m orchestrator dashboard            # see the results at :8787
```

You'll see `[DRY-RUN] would post: …` lines in the console and on the dashboard
activity feed. No Discord needed yet.

---

## Step-by-step: create your Discord server + webhook

Do this once, when you're ready to go live. Takes ~3 minutes.

1. **Create a server** (skip if you have one): open Discord → click the **`+`**
   on the left rail → **Create My Own** → name it (e.g. "Game Deals").
2. **Create a channel** for the alerts, e.g. `#deals` (a normal text channel).
3. **Open the channel's webhook settings:** hover the channel → the **⚙️ gear**
   (Edit Channel) → **Integrations** → **Webhooks** → **New Webhook**.
4. **Name it** (e.g. "Deal Bot"), pick the `#deals` channel, then click
   **Copy Webhook URL**. It looks like:
   ```
   https://discord.com/api/webhooks/123456789012345678/AbCdEf...long-token...
   ```
   > Treat this URL like a password — anyone with it can post to your channel.

### Where to put the webhook URL

Open your **`.env`** file at the project root (copy it from `.env.example`
first if you haven't) and paste it on the `DISCORD_WEBHOOK_URL` line:

```dotenv
DISCORD_WEBHOOK_URL=https://discord.com/api/webhooks/123.../AbCd...
```

That's the only required change. `.env` is git-ignored, so the URL never gets
committed. **While this line is blank, the bot refuses to post and stays in
dry-run** — so you can't leak or misfire by accident.

---

## Going live

1. Put the webhook URL in `.env` (above).
2. Set dry-run off in `.env`:
   ```dotenv
   DEAL_ALERT_DRY_RUN=false
   ```
3. Test one real post immediately:
   ```bash
   python -m modules.deal_alert_bot.run --live --limit 1
   ```
   Check your Discord channel. You should see one deal.

You can always force a mode regardless of `.env`:
`--dry-run` (log only) or `--live` (post for real).

---

## Configuration (all in `.env`, all optional)

| Key | Default | Meaning |
|-----|---------|---------|
| `DISCORD_WEBHOOK_URL` | *(blank)* | Where to post. Blank ⇒ forced dry-run. |
| `DEAL_ALERT_DRY_RUN` | `true` | `false` to post for real. |
| `DEAL_MIN_SAVINGS` | `50` | Minimum discount % to qualify. |
| `DEAL_MAX_PRICE` | *(none)* | Only deals at/below this sale price (USD). |
| `DEAL_REQUIRE_HISTORIC_LOW` | `false` | `true` = only all-time-low prices (1 extra API call per candidate). |
| `DEAL_STORE_IDS` | `1` | CheapShark store IDs, comma-separated. `1`=Steam. |
| `DEAL_PAGE_SIZE` | `60` | Deals pulled per store per run (max 60). |
| `DEAL_MAX_POSTS_PER_RUN` | `5` | Cap on alerts per run (anti-flood). |
| `DEAL_AFFILIATE_LINK_TEMPLATE` | *(blank)* | See below. |

**Common CheapShark store IDs:** `1` Steam · `7` GOG · `8` Origin/EA ·
`11` Humble · `13` Uplay · `25` Epic · `27` Gamesplanet.
(Full list: <https://www.cheapshark.com/api/1.0/stores>.)

---

## Affiliate links (placeholder until you're approved)

Right now each deal links through CheapShark's redirect to the store, and the
message shows an **"affiliate link placeholder"** note. Once your affiliate
program is approved, set a template containing `{deal_url}`:

```dotenv
DEAL_AFFILIATE_LINK_TEMPLATE=https://partner.example/redirect?url={deal_url}&id=YOURID
```

The bot will wrap every link with it automatically — no code change needed.

---

## Scheduling (every 1–4 hours)

The `deal-alert-scan` job is already defined in
`orchestrator/jobs.example.json`, set to **every 3 hours** and **disabled**.
To enable it:

```bash
cp orchestrator/jobs.example.json orchestrator/jobs.json   # once
# edit jobs.json: set "enabled": true for deal-alert-scan
#                 change "interval_hours" (1–4) if you like
python -m orchestrator.scheduler list       # preview
python -m orchestrator.scheduler install    # cron (Linux/macOS)
#   ...or, on any OS:
python -m orchestrator.scheduler run        # portable daemon
```

Tip: keep it in dry-run for the first day of scheduled runs, watch the
dashboard, then flip `DEAL_ALERT_DRY_RUN=false` when you're happy.

---

## Testing

```bash
python -m unittest modules.deal_alert_bot.test_deal_alert
```

Runs fully offline (API responses are faked) and covers the filter logic,
formatting, affiliate wrapping, and dedup.

---

## How errors are handled

- **CheapShark down / network error:** logged as an error, status set to
  `warning`, the run ends cleanly — the scheduler keeps going.
- **Discord webhook fails (incl. rate-limit 429):** that deal is logged as a
  `post_error` and skipped; other deals still post; the deal is *not* marked
  seen, so it'll be retried next run.
- **Anything unexpected:** caught by a top-level guard that logs `fatal` and
  returns non-zero without throwing.
