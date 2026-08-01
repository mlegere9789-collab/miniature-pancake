"""Seed the database with sample data so you can explore the dashboard.

Run once with:  python -m orchestrator demo
Everything it writes is normal data; delete data/orchestrator.db to reset.
"""

from __future__ import annotations

from .database import init_db
from .logger import get_logger


def seed() -> None:
    init_db()

    stock = get_logger("stock_licensing")
    stock.status("ok", "Last batch of 12 photos uploaded to Adobe Stock")
    stock.activity("Uploaded 12 assets, 8 accepted", event="upload")
    stock.earning(23.40, source="adobe_stock", description="Royalties (weekly)")

    ecom = get_logger("ecommerce_dropshipping")
    ecom.status("running", "Syncing today's orders with supplier")
    ecom.activity("Fulfilled 4 orders, 1 needs address check", level="warning")
    ecom.earning(156.00, source="shopify", description="3 orders, net margin")
    ecom.flag_for_review(
        "Order #1042 shipping address looks incomplete",
        description="Missing apartment number — confirm before fulfilling?",
        payload={"order_id": 1042, "customer": "J. Rivera", "total": 52.0},
    )

    deals = get_logger("deal_alert_bot")
    deals.status("ok", "Scanned 214 listings this hour")
    deals.activity("Found 3 candidate deals over 30% off")
    deals.earning(4.20, source="amazon", description="Affiliate commission")
    deals.flag_for_review(
        "Post this deal to the channel?",
        description="Sony WH-1000XM5 — 38% off ($248)",
        payload={"url": "https://example.com/deal", "discount": "38%"},
    )

    digital = get_logger("digital_products")
    digital.status("idle", "Next batch scheduled Monday 08:00")
    digital.activity("Published 2 new Notion templates to Gumroad")
    digital.earning(38.00, source="gumroad", description="2 template sales")

    saas = get_logger("micro_saas")
    saas.status("ok", "All health checks green · 14 active subscribers")
    saas.activity("Nightly billing run: 14 charges succeeded")
    saas.earning(126.00, source="stripe", description="Monthly subscriptions")

    print("Seeded sample data. Now run:  python -m orchestrator dashboard")


if __name__ == "__main__":
    seed()
