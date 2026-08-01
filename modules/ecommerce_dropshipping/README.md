# E-commerce / Dropshipping Ops

**Goal:** run the operational side of a dropshipping / e-commerce store —
order syncing, fulfillment routing, inventory and pricing checks — with as
little manual work as possible.

## What this program will do (later)
- Pull new orders from Shopify and route them to suppliers for fulfillment.
- Watch for problems (out-of-stock items, incomplete addresses, price changes).
- Record net margin per order as earnings.
- Flag risky actions (refunds, price overrides, suspicious orders) to the
  **review queue** before acting.

## Foundation hooks it will use
- Secrets: `SHOPIFY_STORE_URL`, `SHOPIFY_ADMIN_API_TOKEN`, `STRIPE_SECRET_KEY`.
- `log.status(...)`, `log.activity(...)`, `log.earning(..., source="shopify")`.
- `log.flag_for_review(...)` for anything needing your sign-off.

## Suggested cadence
Hourly order sync (see the `ecommerce-order-sync` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need
- A Shopify store + a **custom app** (Shopify admin → Apps → *Develop apps*)
  to get an Admin API access token.
- A Stripe account if you handle payments/refunds directly.
- Supplier account(s) / API access for whoever fulfills your orders.

> Status: **stub** — foundation only. No code here yet.
