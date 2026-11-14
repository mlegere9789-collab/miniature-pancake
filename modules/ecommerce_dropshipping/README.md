# E-commerce / Dropshipping Ops

**Goal:** run the operational side of a dropshipping / e-commerce store —
order syncing, margin tracking, and risk flagging — with as little manual
work as possible.

## What this program does

- `python -m modules.ecommerce_dropshipping.run` pulls **open orders** from
  your Shopify store (Admin REST API) and, for each one not already seen on a
  prior run:
  - computes **net margin** (line-item revenue minus supplier cost, looked up
    per SKU from a local cost book — shipping and tax are excluded, since
    those are pass-through, not margin);
  - checks for problems: an incomplete/missing shipping address, an existing
    refund, a suspiciously high line-item quantity, an unknown SKU cost, or a
    margin below a configured floor.
- A clean order gets `log.earning(...)`'d with its margin. A problem order is
  sent to the **review queue** with the specific reasons instead — nothing
  questionable is auto-logged.
- Already-processed orders are remembered (`data/ecommerce_seen.json`, 90-day
  TTL) so reruns never double-count.
- No credentials configured → the module reports **idle** and does nothing,
  rather than erroring.

### Supplier cost book

Shopify has no concept of "what your supplier charges you" — that varies per
store. Costs are read from a small git-ignored file you maintain:

```json
// data/product_costs.json
{
  "SKU-RED-M": 4.50,
  "SKU-BLUE-L": 5.10
}
```

A SKU missing from this file is never treated as free: the order is flagged
for review instead of trusting an understated margin.

### Not implemented yet: supplier fulfillment routing

The original goal above also described "route orders to suppliers for
fulfillment." That needs a specific supplier's API — AliExpress, CJ,
Spocket, and so on all differ, and there's no universal one to build against
generically. This pass covers everything the shared foundation can already
act on (earnings, the review queue); routing is separate, larger scope once
a specific supplier is chosen.

## Foundation hooks it uses

- Secrets: `SHOPIFY_STORE_URL`, `SHOPIFY_ADMIN_API_TOKEN`.
- `log.status(...)`, `log.activity(...)`, `log.earning(..., source="shopify")`.
- `log.flag_for_review(...)` for anything needing your sign-off.

## Configuration

All optional except the two Shopify credentials — see
`modules/ecommerce_dropshipping/config.py` for the full list and defaults
(`ECOMMERCE_API_VERSION`, `ECOMMERCE_MAX_ORDERS_PER_RUN`,
`ECOMMERCE_HIGH_QUANTITY_THRESHOLD`, `ECOMMERCE_MIN_MARGIN_TO_AUTO_LOG`).

## Suggested cadence

Hourly order sync (see the `ecommerce-order-sync` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need

- A Shopify store + a **custom app** (Shopify admin → Apps → *Develop apps*)
  to get an Admin API access token.
- Your own `data/product_costs.json` cost book (see above) — without it,
  every order gets flagged for review rather than auto-logged.
- Supplier account(s) for whoever fulfills your orders — needed before the
  (not yet built) fulfillment-routing step.

## Testing

```bash
python -m unittest modules.ecommerce_dropshipping.test_ecommerce
```

Runs fully offline (no network) and covers margin calculation, the risk
checks, the Shopify domain normalizer, and dedup.

---

## How errors are handled

- **No Shopify credentials set:** status goes `idle`; the run is a no-op.
- **Shopify API down / network error / bad response:** logged as an error,
  status set to `error`, the run ends cleanly — the scheduler keeps going.
- **Anything unexpected:** caught by a top-level guard that logs `fatal` and
  returns non-zero without throwing.

> Status: **implemented** — order sync, margin tracking, and risk flagging.
> Supplier fulfillment routing is still future work (see above).
