# Stock Asset Licensing

**Goal:** earn passive royalties by licensing stock assets (photos, video,
vectors, audio, AI-generated imagery) through contributor marketplaces such as
Adobe Stock, Shutterstock, and similar.

## What this program will do (later)
- Prepare/keyword assets and upload them to one or more contributor accounts.
- Track which assets were accepted vs. rejected.
- Record royalty earnings as they come in.
- Flag borderline items (e.g. possible trademark/property-release issues) to
  the **review queue** for your manual approval before submission.

## Foundation hooks it will use
- Secrets: `SHUTTERSTOCK_API_TOKEN`, `ADOBE_STOCK_API_KEY` (in `.env`).
- `log.status(...)`, `log.activity(...)`, `log.earning(..., source="adobe_stock")`.
- `log.flag_for_review(...)` for anything needing a human check.

## Suggested cadence
Daily upload/keyword batch (see the `stock-licensing-daily-upload` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need
- Contributor accounts (Adobe Stock, Shutterstock, etc.) — these require ID
  verification and tax forms; allow a few days for approval.
- API/contributor credentials where the marketplace offers them.

> Status: **stub** — foundation only. No code here yet.
