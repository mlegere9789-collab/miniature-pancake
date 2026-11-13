# Digital Product Creation

**Goal:** create and sell digital products (templates, printables, e-books,
presets, Notion/Canva templates) on marketplaces like Etsy and Gumroad.

## What this program does

`python -m modules.digital_products.run` drafts marketplace listing copy
with Claude from briefs you queue, and puts every draft in front of you
before anything goes live:

1. Reads queued **product briefs** from a small git-ignored file you
   maintain (`data/product_briefs.json` — name, category, features,
   audience, price; see `briefs.py` for the exact shape).
2. Skips briefs already drafted on a prior run (dedup, by the brief's own
   `id` — editing other fields and rerunning does not trigger a redraft).
3. For each fresh brief (capped at `DIGITAL_PRODUCTS_MAX_PER_RUN` per run,
   so a large backlog doesn't burn a lot of API spend in one go), asks
   Claude for a title, description, and tags, instructed to reply in a
   fixed JSON shape.
4. Flags the draft to the **review queue** — AI-generated copy always needs
   a human look before it ships anywhere, so nothing here auto-publishes.
   A reply that doesn't parse as the expected JSON (a model wobble) is
   still flagged, with the raw text attached, so nothing is silently lost —
   it just needs a manual rewrite instead of an auto-parsed listing.

No credentials configured → the module reports **idle** and does nothing,
rather than erroring.

### Not implemented yet: publishing to Etsy / Gumroad, and product assets

The original goal above also described generating product assets and
publishing/updating live marketplace listings. Both need more than this
module can respect safely as a first pass: assets ("templates, printables,
e-books, presets") are open-ended creative output with no generic way to
"generate" them well, and publishing needs a real marketplace account,
category, and shipping/tax settings you'd want full control over. This pass
covers the copy-drafting step end to end — everything the shared foundation
(the review queue) can already act on — and stops at *your* approval, by
design.

## Foundation hooks it uses

- Secrets: `ANTHROPIC_API_KEY` (for drafting copy).
- `log.status(...)`, `log.activity(...)`.
- `log.flag_for_review(...)` for every draft — nothing here calls
  `log.earning(...)` yet, since nothing is actually sold until publishing
  (Etsy/Gumroad) is built.

## Configuration

All optional except `ANTHROPIC_API_KEY` — see
`modules/digital_products/config.py` for the full list and defaults
(`DIGITAL_PRODUCTS_MODEL`, `DIGITAL_PRODUCTS_MAX_TOKENS`,
`DIGITAL_PRODUCTS_MAX_PER_RUN`).

## Suggested cadence

Weekly batch (see the `digital-products-weekly-batch` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need

- An Anthropic API key (`ANTHROPIC_API_KEY`).
- Your own `data/product_briefs.json` — nothing gets drafted without briefs
  to draft from.
- An Etsy seller account + developer app, and/or a Gumroad account, once
  publishing is built — not required for drafting.

## Testing

```bash
python -m unittest modules.digital_products.test_digital_products
```

Runs fully offline (no network) and covers brief loading, prompt building,
reply parsing (including the markdown-fence and malformed-JSON cases), and
dedup.

---

## How errors are handled

- **No `ANTHROPIC_API_KEY` set:** status goes `idle`; the run is a no-op.
- **No briefs queued, or all queued briefs already drafted:** status `ok`,
  nothing happens.
- **Anthropic API down / network error:** that brief is logged as an error
  and skipped; it is *not* marked seen, so it's retried next run; other
  briefs in the same run still get drafted.
- **Reply doesn't parse as the expected JSON:** flagged for review anyway,
  with the raw text, instead of being dropped.
- **Anything unexpected:** caught by a top-level guard that logs `fatal` and
  returns non-zero without throwing.

> Status: **implemented** — brief-driven copy drafting via Claude, always
> flagged for review. Publishing (Etsy/Gumroad) and asset generation are
> still future work (see above).
