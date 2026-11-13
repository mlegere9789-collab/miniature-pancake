"""E-commerce / Dropshipping entry point.

Flow each run:
    1. Pull open orders from Shopify (skipped entirely if no store/token is
       configured — the module reports idle rather than erroring).
    2. Skip anything already processed on a prior run (dedup).
    3. For each fresh order, compute net margin from the local supplier cost
       book and check for problems (incomplete address, an existing refund,
       a suspiciously high line-item quantity, an unknown SKU cost, or a
       margin below the configured floor).
    4. Clean orders: log the margin as an earning. Problem orders: flag them
       to the review queue instead, with the reasons, so nothing questionable
       gets auto-logged.
    5. Log everything (activity + errors) to the shared database -> dashboard.

Nothing here is allowed to raise out of `run()`: API failures are caught and
logged so the scheduler keeps ticking.

Note on scope: the module READMEs describe this program as eventually
"routing orders to suppliers for fulfillment." That step needs a specific
supplier's API (AliExpress, CJ, Spocket, ... — there is no universal one) and
is not implemented here; this pass covers order sync, margin tracking, and
risk flagging, which is everything the foundation (earnings + review queue)
can act on today.

Usage:
    python -m modules.ecommerce_dropshipping.run            # uses .env settings
    python -m modules.ecommerce_dropshipping.run --limit 10 # cap orders this run
"""

from __future__ import annotations

import argparse
import sys
from typing import Any

from orchestrator import get_logger

from . import shopify_client
from .config import Settings
from .costs import CostBook
from .dedup import SeenStore
from .formatter import format_order_summary, format_review_description
from .pricing import evaluate_order

MODULE = "ecommerce_dropshipping"


def run(limit_override: int | None = None) -> int:
    """Run one sync cycle. Returns the number of orders processed (logged as earnings)."""
    log = get_logger(MODULE)
    settings = Settings.load()
    if limit_override is not None:
        settings.max_orders_per_run = max(1, limit_override)

    if not settings.configured:
        msg = "No Shopify credentials configured (SHOPIFY_STORE_URL / SHOPIFY_ADMIN_API_TOKEN) — nothing to sync"
        log.status("idle", msg)
        return 0

    log.status("running", f"Syncing open orders from {settings.store_url}")

    try:
        orders = shopify_client.fetch_open_orders(
            settings.store_url,
            settings.admin_token,
            api_version=settings.api_version,
            limit=settings.max_orders_per_run,
        )
    except shopify_client.ShopifyError as exc:
        log.error(f"Shopify fetch failed: {exc}", event="api_error")
        log.status("error", f"Shopify fetch failed: {exc}")
        return 0

    if not orders:
        log.activity("No open orders returned from Shopify", event="no_orders")
        log.status("ok", "No open orders")
        return 0

    seen = SeenStore()
    cost_book = CostBook.load()
    fresh = [o for o in orders if not seen.is_seen(str(o.get("id")))]

    processed = 0
    flagged = 0
    for order in fresh[: settings.max_orders_per_run]:
        assessment = evaluate_order(
            order,
            cost_book,
            high_quantity_threshold=settings.high_quantity_threshold,
            min_margin_to_auto_log=settings.min_margin_to_auto_log,
        )
        meta: dict[str, Any] = {
            "order_id": order.get("id"),
            "name": order.get("name"),
            "margin": assessment.margin,
        }

        if assessment.needs_review:
            log.flag_for_review(
                f"Review order {order.get('name', order.get('id'))}",
                description=format_review_description(order, assessment),
                payload=meta,
            )
            flagged += 1
        else:
            log.activity(
                format_order_summary(order, assessment),
                event="order_processed",
                metadata=meta,
            )
            log.earning(
                assessment.margin,
                source="shopify",
                description=f"Order {order.get('name', order.get('id'))} net margin",
            )
            processed += 1

        seen.mark(str(order.get("id")))

    seen.save()

    summary = (
        f"synced {len(orders)} open orders, {len(fresh)} new: "
        f"{processed} processed, {flagged} flagged for review"
    )
    log.activity(summary, event="run_summary")
    log.status("ok", summary)
    return processed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="E-commerce / Dropshipping order sync (Shopify)"
    )
    parser.add_argument(
        "--limit", type=int, default=None, help="max orders this run (overrides config)"
    )
    args = parser.parse_args(argv)

    try:
        run(limit_override=args.limit)
        return 0
    except Exception as exc:  # last-resort guard so cron never sees a crash
        get_logger(MODULE).error(f"Unexpected failure: {exc}", event="fatal")
        get_logger(MODULE).status("error", f"Crashed: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
