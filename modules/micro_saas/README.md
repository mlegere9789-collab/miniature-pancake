# Micro-SaaS Tool

**Goal:** run a small subscription software product — recurring revenue from a
focused tool that solves one problem well.

## What this program does

`python -m modules.micro_saas.run` does two independent things, each skipped
if its own credentials aren't configured:

### Health check (`SAAS_HEALTH_URL`)

One GET against your live service. Healthy → logged. Unhealthy (bad status,
timeout, connection error) → flagged to the **review queue** as an incident.

### Billing reconciliation (`STRIPE_SECRET_KEY`)

1. Fetches active subscriptions from Stripe and computes **MRR**, normalizing
   every billing interval (daily/weekly/monthly/yearly, any `interval_count`)
   to a monthly-equivalent amount.
2. Diffs this run's subscription ids against the last run's
   (`data/micro_saas_snapshot.json`, git-ignored) to find **new signups** and
   **churn**.
3. Fetches charges since the last run and nets out `amount - amount_refunded`
   on succeeded charges — that net total is logged as an earning
   (`source="stripe"`).
4. Flags to the review queue instead of silently logging: any **failed**
   charge, any charge with a **refund**, and a run where churn is at/above
   `MICRO_SAAS_HIGH_CHURN_THRESHOLD`.

Neither configured → the module reports **idle** and does nothing, rather
than erroring.

### Not implemented yet: reconciling your own app database

The original goal above also described flagging "plan changes" and
reconciling against the app's own subscription/billing tables
(`SAAS_DATABASE_URL`, `SAAS_JWT_SECRET`). That schema is specific to
whatever the SaaS itself is built with — there's nothing generic to build
against. Stripe is the source of truth this module reconciles against
instead; a plan change already shows up as a subscription's item amount
changing MRR between runs.

## Foundation hooks it uses

- Secrets: `STRIPE_SECRET_KEY`, `SAAS_HEALTH_URL`.
- `log.status(...)`, `log.activity(...)`, `log.earning(..., source="stripe")`.
- `log.flag_for_review(...)` for incidents, failed charges, refunds, and high
  churn.

## Configuration

All optional — see `modules/micro_saas/config.py` for the full list and
defaults (`MICRO_SAAS_HEALTH_TIMEOUT`, `MICRO_SAAS_SUB_LIMIT`,
`MICRO_SAAS_CHARGE_LIMIT`, `MICRO_SAAS_LOOKBACK_HOURS`,
`MICRO_SAAS_HIGH_CHURN_THRESHOLD`).

## Suggested cadence

Daily health + billing run (see the `micro-saas-health-and-billing` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need

- Wherever the SaaS itself is hosted (this module monitors/reconciles
  against it; the app itself is a separate deployment) — set
  `SAAS_HEALTH_URL` to a URL that returns 2xx/3xx when it's up.
- A Stripe account configured with your products/prices.

## Testing

```bash
python -m unittest modules.micro_saas.test_micro_saas
```

Runs fully offline (no real network — the one health-check test hits an
unroutable local port to exercise the failure path) and covers MRR
normalization, churn diffing, charge summarization, and snapshot round-trips.

---

## How errors are handled

- **Neither `SAAS_HEALTH_URL` nor `STRIPE_SECRET_KEY` set:** status goes
  `idle`; the run is a no-op.
- **Health check fails:** flagged to the review queue as an incident; billing
  reconciliation still runs if configured.
- **Stripe API down / network error:** logged as an error; that part of the
  run is skipped (subscriptions failing skips billing entirely; charges
  failing still logs the MRR/churn summary with $0 collected).
- **Anything unexpected:** caught by a top-level guard that logs `fatal` and
  returns non-zero without throwing.

> Status: **implemented** — health checks and Stripe billing reconciliation
> (MRR, churn, revenue, failed charges, refunds). Reconciling the SaaS's own
> database is still future work (see above).
