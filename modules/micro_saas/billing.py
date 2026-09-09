"""MRR, churn, and charge-summary calculations.

Kept separate from networking so it is trivial to unit-test offline: every
function here takes plain dicts/sets (Stripe's object shapes) and does no I/O.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

# How many times a subscription with this billing interval renews per month,
# for a single `interval_count` (e.g. "every 2 months" halves this).
_MONTHLY_OCCURRENCES = {
    "day": 30.0,
    "week": 30.0 / 7.0,
    "month": 1.0,
    "year": 1.0 / 12.0,
}


def to_float(value: Any, default: float = 0.0) -> float:
    """Parse a Stripe numeric field that may be missing, `None` (a
    present-but-null field, e.g. from a restricted API key), or otherwise
    not a plain number. Every numeric field pulled off a raw Stripe object
    anywhere in this module goes through this rather than a bare
    `dict.get(key, 0)` — that only substitutes the default when the key is
    *absent*, not when it's present and `None`, so a null `amount` on a
    charge would otherwise reach a raw `/ 100.0` and raise.
    """
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _item_monthly_amount(item: dict[str, Any]) -> float:
    price = item.get("price") or {}
    recurring = price.get("recurring") or {}
    interval = recurring.get("interval", "month")
    interval_count = max(1, int(recurring.get("interval_count") or 1))
    occurrences = _MONTHLY_OCCURRENCES.get(interval, 1.0) / interval_count
    unit_amount = to_float(price.get("unit_amount")) / 100.0
    quantity = to_float(item.get("quantity"), 1.0)
    return unit_amount * quantity * occurrences


def compute_mrr(subscriptions: list[dict[str, Any]]) -> float:
    """Sum each active subscription's items, normalized to a monthly amount."""
    total = 0.0
    for sub in subscriptions:
        items = (sub.get("items") or {}).get("data") or []
        total += sum(_item_monthly_amount(item) for item in items)
    return total


def diff_subscriptions(
    previous_ids: set[str], current_ids: set[str]
) -> tuple[set[str], set[str]]:
    """Return (new_ids, churned_ids) between two snapshots of active-sub ids."""
    new_ids = current_ids - previous_ids
    churned_ids = previous_ids - current_ids
    return new_ids, churned_ids


@dataclass
class ChargeSummary:
    collected: float = 0.0
    failed: list[dict[str, Any]] = field(default_factory=list)
    refunded: list[dict[str, Any]] = field(default_factory=list)


def summarize_charges(charges: list[dict[str, Any]]) -> ChargeSummary:
    """Net revenue collected, plus any charge needing a human look."""
    summary = ChargeSummary()
    for charge in charges:
        status = charge.get("status")
        refunded_amount = to_float(charge.get("amount_refunded"))
        if status == "succeeded":
            net_cents = to_float(charge.get("amount")) - refunded_amount
            summary.collected += net_cents / 100.0
        elif status == "failed":
            summary.failed.append(charge)
        if refunded_amount > 0:
            summary.refunded.append(charge)
    return summary
