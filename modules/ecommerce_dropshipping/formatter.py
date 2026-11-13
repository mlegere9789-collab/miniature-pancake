"""Turn a Shopify order + assessment into log/review-queue text.

Kept separate from networking and pricing so it is trivial to unit-test
offline.
"""

from __future__ import annotations

from typing import Any

from .pricing import OrderAssessment


def format_order_summary(order: dict[str, Any], assessment: OrderAssessment) -> str:
    """A one-line summary used for the activity log."""
    name = order.get("name", f"#{order.get('id', '?')}")
    items = len(order.get("line_items") or [])
    return f"Order {name} — ${assessment.margin:.2f} net margin ({items} item(s))"


def format_review_description(
    order: dict[str, Any], assessment: OrderAssessment
) -> str:
    """A multi-line description for a `flag_for_review` item."""
    name = order.get("name", f"#{order.get('id', '?')}")
    total = order.get("total_price", "?")
    currency = order.get("currency", "USD")
    lines = [
        f"Order {name} — total {total} {currency}, computed margin "
        f"${assessment.margin:.2f}",
        "Needs a look because:",
    ]
    lines.extend(f"  - {reason}" for reason in assessment.reasons)
    return "\n".join(lines)
