# Micro-SaaS Tool

**Goal:** run a small subscription software product — recurring revenue from a
focused tool that solves one problem well.

## What this program will do (later)
- Run health checks against the live service and alert on problems.
- Reconcile subscriptions/billing (Stripe) and record revenue.
- Summarize signups, churn, and MRR.
- Flag operational decisions (refund requests, plan changes, incidents) to the
  **review queue**.

## Foundation hooks it will use
- Secrets: `STRIPE_SECRET_KEY`, `SAAS_DATABASE_URL`, `SAAS_JWT_SECRET`.
- `log.status(...)`, `log.activity(...)`, `log.earning(..., source="stripe")`.
- `log.flag_for_review(...)` for anything needing your decision.

## Suggested cadence
Daily health + billing run (see the `micro-saas-health-and-billing` job in
`orchestrator/jobs.example.json`).

## Manual setup you'll need
- Wherever the SaaS itself is hosted (this module orchestrates/monitors it; the
  app itself is a separate deployment).
- A Stripe account configured with your products/prices.

> Status: **stub** — foundation only. No code here yet.
