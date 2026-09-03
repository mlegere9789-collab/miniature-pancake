"""Order margin calculation and risk checks.

Kept separate from networking so it is trivial to unit-test offline: every
function here takes plain dicts (Shopify's order/line-item shape) and does
no I/O.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .costs import CostBook


def _f(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def compute_margin(
    order: dict[str, Any], cost_book: CostBook
) -> tuple[float, list[str]]:
    """Return (net_margin, missing_skus).

    Revenue is the sum of each line item's price * quantity (shipping and
    tax excluded — those are pass-through, not margin). Cost is the sum of
    each line item's supplier cost * quantity, looked up by SKU. A line item
    whose SKU has no known cost contributes zero to cost but is listed in
    ``missing_skus``, so the caller can flag the order instead of trusting
    an understated margin.
    """
    revenue = 0.0
    cost = 0.0
    missing: list[str] = []
    for item in order.get("line_items") or []:
        qty = _f(item.get("quantity"), 0.0)
        revenue += _f(item.get("price")) * qty
        sku = item.get("sku")
        unit_cost = cost_book.cost_for(sku)
        if unit_cost is None:
            missing.append(sku or f"<no sku: {item.get('title', 'unknown item')}>")
            continue
        cost += unit_cost * qty
    return revenue - cost, missing


def has_incomplete_address(order: dict[str, Any]) -> bool:
    """True if any shippable line item has no usable shipping address."""
    line_items = order.get("line_items") or []
    if not any(item.get("requires_shipping", True) for item in line_items):
        return False
    address = order.get("shipping_address")
    if not address:
        return True
    required = ("address1", "city", "zip", "country")
    return any(not (address.get(field_) or "").strip() for field_ in required)


def has_refund(order: dict[str, Any]) -> bool:
    return bool(order.get("refunds"))


def has_high_quantity(order: dict[str, Any], threshold: int) -> bool:
    return any(
        _f(item.get("quantity"), 0.0) >= threshold
        for item in order.get("line_items") or []
    )


@dataclass
class OrderAssessment:
    margin: float
    missing_skus: list[str] = field(default_factory=list)
    reasons: list[str] = field(default_factory=list)

    @property
    def needs_review(self) -> bool:
        return bool(self.reasons)


def evaluate_order(
    order: dict[str, Any],
    cost_book: CostBook,
    *,
    high_quantity_threshold: int,
    min_margin_to_auto_log: float,
) -> OrderAssessment:
    """Pure predicate + margin calc: does this order need a human look?"""
    margin, missing_skus = compute_margin(order, cost_book)
    reasons: list[str] = []

    if has_incomplete_address(order):
        reasons.append("incomplete or missing shipping address")
    if has_refund(order):
        reasons.append("order already has a refund on it")
    if has_high_quantity(order, high_quantity_threshold):
        reasons.append(f"a line item quantity is >= {high_quantity_threshold}")
    if missing_skus:
        reasons.append(f"unknown supplier cost for: {', '.join(missing_skus)}")
    if not missing_skus and margin < min_margin_to_auto_log:
        reasons.append(
            f"margin ${margin:.2f} is below the ${min_margin_to_auto_log:.2f} floor"
        )

    return OrderAssessment(margin=margin, missing_skus=missing_skus, reasons=reasons)
