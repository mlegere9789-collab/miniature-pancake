# Deal-Alert Bot

**Goal:** monitor products/prices and push alerts about notable deals to a
channel (Telegram/Discord), monetized via affiliate links.

## What this program will do (later)
- Scan sources (retailer pages, price APIs, feeds) for meaningful price drops.
- Post curated alerts to your Telegram/Discord channel with affiliate links.
- Track affiliate commissions as earnings.
- Optionally flag each deal to the **review queue** so you approve posts before
  they go out (useful early on, before you trust it to auto-post).

## Foundation hooks it will use
- Secrets: `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_ID`, `DISCORD_WEBHOOK_URL`,
  `AMAZON_AFFILIATE_TAG`.
- `log.status(...)`, `log.activity(...)`, `log.earning(..., source="amazon")`.
- `log.flag_for_review(...)` for human-in-the-loop posting.

## Suggested cadence
Hourly scan (see the `deal-alert-scan` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need
- A Telegram bot (talk to **@BotFather**) and/or a Discord webhook URL.
- Affiliate program enrollment (e.g. Amazon Associates) for your affiliate tag.
- Respect each source's Terms of Service / rate limits when scanning.

> Status: **stub** — foundation only. No code here yet.
