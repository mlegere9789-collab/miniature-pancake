"""Micro-SaaS Tool entry point.

Flow each run, in two independent parts (either can be skipped if its
credentials aren't configured):

    Health check (if `SAAS_HEALTH_URL` is set):
        One GET against the live service. Healthy -> logged. Unhealthy ->
        flagged to the review queue as an incident.

    Billing reconciliation (if `STRIPE_SECRET_KEY` is set):
        1. Fetch active subscriptions, compute MRR.
        2. Diff against the last run's subscription ids -> new vs. churned.
        3. Fetch charges since the last run, net collected revenue is logged
           as an earning.
        4. Failed charges, refunds, and high churn are flagged to the review
           queue instead of silently logged.
        5. Save this run's subscription ids + timestamp as the new snapshot.

Nothing here is allowed to raise out of `run()`: API/network failures are
caught and logged so the scheduler keeps ticking.

Note on scope: reconciling *your own* billing/JWT database
(`SAAS_DATABASE_URL`, `SAAS_JWT_SECRET` in `.env.example`) is not
implemented — that schema is specific to whatever the SaaS itself is built
with, and there's nothing generic to build against. Stripe is the source of
truth this module reconciles against instead.

Usage:
    python -m modules.micro_saas.run
"""

from __future__ import annotations

import sys
from typing import Any

from orchestrator import get_logger

from . import stripe_client
from .billing import ChargeSummary, compute_mrr, diff_subscriptions, summarize_charges
from .config import Settings
from .formatter import (
    format_billing_summary,
    format_churn_review,
    format_failed_charge,
    format_refunded_charge,
)
from .health import check_health
from .snapshot import SnapshotStore

MODULE = "micro_saas"


def _run_health_check(settings: Settings, log) -> None:
    result = check_health(settings.health_url, timeout=settings.health_timeout)
    if result.ok:
        log.activity(
            f"Health check OK ({result.detail}, {result.latency_ms:.0f}ms)",
            event="health_ok",
        )
    else:
        log.flag_for_review(
            "Service health check failed",
            description=f"{settings.health_url} — {result.detail}",
            payload={"url": settings.health_url, "detail": result.detail},
        )


def _run_billing(settings: Settings, log) -> tuple[float, int]:
    """Returns (net_collected, flagged_count)."""
    try:
        subscriptions = stripe_client.list_active_subscriptions(
            settings.stripe_secret_key, limit=settings.sub_limit
        )
    except stripe_client.StripeError as exc:
        log.error(f"Stripe subscriptions fetch failed: {exc}", event="api_error")
        return 0.0, 0

    snap = SnapshotStore()
    current_ids = {s["id"] for s in subscriptions if s.get("id")}
    new_ids, churned_ids = diff_subscriptions(snap.previous_ids, current_ids)
    mrr = compute_mrr(subscriptions)

    since = snap.previous_run_unix(settings.lookback_hours)
    try:
        charges = stripe_client.list_charges_since(
            settings.stripe_secret_key, since, limit=settings.charge_limit
        )
    except stripe_client.StripeError as exc:
        log.error(f"Stripe charges fetch failed: {exc}", event="api_error")
        charges = []

    summary: ChargeSummary = summarize_charges(charges)

    flagged = 0
    for charge in summary.failed:
        log.flag_for_review(
            f"Failed charge {charge.get('id')}",
            description=format_failed_charge(charge),
            payload={"charge_id": charge.get("id")},
        )
        flagged += 1
    for charge in summary.refunded:
        log.flag_for_review(
            f"Refund on charge {charge.get('id')}",
            description=format_refunded_charge(charge),
            payload={"charge_id": charge.get("id")},
        )
        flagged += 1
    if len(churned_ids) >= settings.high_churn_threshold:
        log.flag_for_review(
            "High churn this run",
            description=format_churn_review(churned_ids, settings.high_churn_threshold),
            payload={"churned_ids": sorted(churned_ids)},
        )
        flagged += 1

    if summary.collected > 0:
        log.earning(
            summary.collected,
            source="stripe",
            description="Net charges collected since last run",
        )

    meta: dict[str, Any] = {
        "mrr": mrr,
        "new_subscriptions": len(new_ids),
        "churned_subscriptions": len(churned_ids),
        "collected": summary.collected,
    }
    log.activity(
        format_billing_summary(mrr, new_ids, churned_ids, summary),
        event="billing_summary",
        metadata=meta,
    )

    snap.save(current_ids)
    return summary.collected, flagged


def run() -> float:
    """Run one health + billing cycle. Returns net revenue collected."""
    log = get_logger(MODULE)
    settings = Settings.load()

    if not settings.health_configured and not settings.billing_configured:
        log.status(
            "idle",
            "No SAAS_HEALTH_URL or STRIPE_SECRET_KEY configured — nothing to check",
        )
        return 0.0

    log.status("running", "Checking health and reconciling billing")

    if settings.health_configured:
        _run_health_check(settings, log)

    collected = 0.0
    flagged = 0
    if settings.billing_configured:
        collected, flagged = _run_billing(settings, log)

    summary = f"health {'checked' if settings.health_configured else 'skipped'}, "
    summary += f"billing {'reconciled' if settings.billing_configured else 'skipped'}"
    if settings.billing_configured:
        summary += f" (${collected:.2f} collected, {flagged} flagged)"
    log.status("ok", summary)
    return collected


def main(argv: list[str] | None = None) -> int:
    del argv  # no CLI options yet — everything is `.env`-driven
    try:
        run()
        return 0
    except Exception as exc:  # last-resort guard so cron never sees a crash
        get_logger(MODULE).error(f"Unexpected failure: {exc}", event="fatal")
        get_logger(MODULE).status("error", f"Crashed: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
