"""Turn billing/health results into log and review-queue text.

Kept separate from networking and calculation so it is trivial to unit-test
offline.
"""

from __future__ import annotations

from typing import Any

from .billing import ChargeSummary


def format_billing_summary(
    mrr: float, new_ids: set[str], churned_ids: set[str], charges: ChargeSummary
) -> str:
    return (
        f"MRR ${mrr:.2f} — {len(new_ids)} new, {len(churned_ids)} churned — "
        f"${charges.collected:.2f} collected since last run"
    )


def format_churn_review(churned_ids: set[str], threshold: int) -> str:
    ids = ", ".join(sorted(churned_ids))
    return (
        f"{len(churned_ids)} subscriptions churned since last run "
        f"(>= {threshold} triggers review): {ids}"
    )


def format_failed_charge(charge: dict[str, Any]) -> str:
    amount = charge.get("amount", 0) / 100.0
    reason = charge.get("failure_message") or "no failure message"
    return f"Charge {charge.get('id')} for ${amount:.2f} failed: {reason}"


def format_refunded_charge(charge: dict[str, Any]) -> str:
    refunded = charge.get("amount_refunded", 0) / 100.0
    return f"Charge {charge.get('id')} has ${refunded:.2f} refunded"
