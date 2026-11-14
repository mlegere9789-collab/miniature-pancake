# Stock Asset Licensing

**Goal:** earn passive royalties by licensing stock assets (photos, video,
vectors, audio, AI-generated imagery) through contributor marketplaces such as
Adobe Stock, Shutterstock, and similar.

## What this program does

`python -m modules.stock_licensing.run` drafts contributor title/description/
keyword metadata with Claude from assets you queue, and puts every draft in
front of you before anything goes near a marketplace:

1. Reads the queued **asset list** from a small git-ignored file you
   maintain (`data/stock_assets.json` — filename, category, subject, notes;
   see `assets.py` for the exact shape).
2. Skips assets already keyworded on a prior run (dedup, by the asset's own
   `id` — editing other fields and rerunning does not trigger a redraft).
3. For each fresh asset (capped at `STOCK_LICENSING_MAX_PER_RUN` per run, so
   a large backlog doesn't burn a lot of API spend in one go), asks Claude
   for a title, description, and keywords, instructed to reply in a fixed
   JSON shape.
4. Flags the draft to the **review queue** — AI-generated metadata always
   needs a human look before it's used anywhere, so nothing here
   auto-uploads. A reply that doesn't parse as the expected JSON (a model
   wobble) is still flagged, with the raw text attached, so nothing is
   silently lost — it just needs manual title/keywords instead of an
   auto-parsed draft.

No credentials configured → the module reports **idle** and does nothing,
rather than erroring.

### Not implemented yet: uploading to Adobe Stock / Shutterstock, and tracking royalties

The original goal above also described uploading assets to contributor
accounts and tracking which were accepted/rejected and their resulting
royalties. Neither marketplace exposes a public contributor API to build
reliably against — there's no documented, stable way to submit an asset or
query royalty status programmatically, so building against a guessed
endpoint would be more likely to break silently than to work. This pass
covers the metadata-drafting step end to end — everything the shared
foundation (the review queue) can already act on — and stops at *your*
approval and manual upload, by design. Once you have royalty figures (from
a marketplace's own statement/export, however they provide it), record them
the same way every other module does: `log.earning(amount, source="adobe_stock")`
or `source="shutterstock"` — see `orchestrator/README.md` for the logger API,
or use `python -m orchestrator export-earnings` to get everything back out
again for your own records.

## Foundation hooks it uses

- Secrets: `ANTHROPIC_API_KEY` (for drafting metadata).
- `log.status(...)`, `log.activity(...)`.
- `log.flag_for_review(...)` for every draft — nothing here calls
  `log.earning(...)` yet, since nothing is actually licensed until you've
  uploaded and been paid, which this module doesn't do (see above).

## Configuration

All optional except `ANTHROPIC_API_KEY` — see
`modules/stock_licensing/config.py` for the full list and defaults
(`STOCK_LICENSING_MODEL`, `STOCK_LICENSING_MAX_TOKENS`,
`STOCK_LICENSING_MAX_PER_RUN`).

## Suggested cadence

Daily batch (see the `stock-licensing-daily-keywording` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need

- An Anthropic API key (`ANTHROPIC_API_KEY`).
- Your own `data/stock_assets.json` — nothing gets drafted without assets to
  draft from.
- Contributor accounts (Adobe Stock, Shutterstock, etc.) — these require ID
  verification and tax forms; allow a few days for approval. Not required
  for drafting metadata, only for the manual upload step this module
  doesn't do.

## Testing

```bash
python -m unittest modules.stock_licensing.test_stock_licensing
```

Runs fully offline (no network) and covers asset loading, prompt building,
reply parsing (including the markdown-fence and malformed-JSON cases), and
dedup.

---

## How errors are handled

- **No `ANTHROPIC_API_KEY` set:** status goes `idle`; the run is a no-op.
- **No assets queued, or all queued assets already keyworded:** status `ok`,
  nothing happens.
- **Anthropic API down / network error:** that asset is logged as an error
  and skipped; it is *not* marked seen, so it's retried next run; other
  assets in the same run still get drafted.
- **Reply doesn't parse as the expected JSON:** flagged for review anyway,
  with the raw text, instead of being dropped.
- **Anything unexpected:** caught by a top-level guard that logs `fatal` and
  returns non-zero without throwing.

> Status: **implemented** — asset-driven metadata drafting via Claude,
> always flagged for review. Uploading (Adobe Stock/Shutterstock) and
> royalty tracking are still future work (see above).
