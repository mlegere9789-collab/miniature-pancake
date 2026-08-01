# Digital Product Creation

**Goal:** create and sell digital products (templates, printables, e-books,
presets, Notion/Canva templates) on marketplaces like Etsy and Gumroad.

## What this program will do (later)
- Generate/assemble product assets and listing copy (Claude can help draft
  titles, descriptions, tags).
- Publish or update listings on Etsy / Gumroad.
- Record sales as earnings.
- Flag new listings or AI-generated content to the **review queue** so you
  approve them before they go live.

## Foundation hooks it will use
- Secrets: `ETSY_API_KEY`, `ETSY_SHOP_ID`, `GUMROAD_ACCESS_TOKEN`,
  `ANTHROPIC_API_KEY` (for drafting copy).
- `log.status(...)`, `log.activity(...)`, `log.earning(..., source="gumroad")`.
- `log.flag_for_review(...)` before publishing.

## Suggested cadence
Weekly batch (see the `digital-products-weekly-batch` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need
- An Etsy seller account + an Etsy developer app (for the API keystring), and/or
  a Gumroad account with an access token.
- Any design tools you use to produce the assets.

> Status: **stub** — foundation only. No code here yet.
