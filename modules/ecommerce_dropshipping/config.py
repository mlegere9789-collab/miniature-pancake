"""E-commerce / Dropshipping settings.

Everything is configurable via `.env` (loaded through the orchestrator's
`config`), with sensible defaults. No secrets or tunables are hardcoded into
the logic.

Relevant `.env` keys (all optional except the two Shopify credentials):

    SHOPIFY_STORE_URL                 Your store, e.g. "my-shop" or
                                      "my-shop.myshopify.com". Unset = the
                                      module has nothing to sync and stays
                                      idle rather than erroring.
    SHOPIFY_ADMIN_API_TOKEN           Custom-app access token (store admin ->
                                      Apps -> "Develop apps").
    ECOMMERCE_API_VERSION             Shopify Admin API version
                                      (default "2024-01").
    ECOMMERCE_MAX_ORDERS_PER_RUN      Cap on orders fetched/processed per run
                                      (default 25).
    ECOMMERCE_HIGH_QUANTITY_THRESHOLD Line-item quantity at/above which an
                                      order is flagged as suspicious
                                      (default 10).
    ECOMMERCE_MIN_MARGIN_TO_AUTO_LOG  Orders with a computed net margin below
                                      this (USD) are flagged for review
                                      instead of auto-logged as earnings
                                      (default 0 — i.e. flag any loss).
"""

from __future__ import annotations

from dataclasses import dataclass

from orchestrator import config


def _get_float(key: str, default: float) -> float:
    raw = config.get(key)
    if raw is None or raw.strip() == "":
        return default
    try:
        return float(raw)
    except ValueError:
        return default


def _get_int(key: str, default: int) -> int:
    raw = config.get(key)
    if raw is None or raw.strip() == "":
        return default
    try:
        return int(raw)
    except ValueError:
        return default


@dataclass
class Settings:
    store_url: str | None
    admin_token: str | None
    api_version: str
    max_orders_per_run: int
    high_quantity_threshold: int
    min_margin_to_auto_log: float

    @property
    def configured(self) -> bool:
        return bool(self.store_url and self.admin_token)

    @classmethod
    def load(cls) -> "Settings":
        return cls(
            store_url=config.get("SHOPIFY_STORE_URL") or None,
            admin_token=config.get("SHOPIFY_ADMIN_API_TOKEN") or None,
            api_version=config.get("ECOMMERCE_API_VERSION", "2024-01") or "2024-01",
            max_orders_per_run=max(1, _get_int("ECOMMERCE_MAX_ORDERS_PER_RUN", 25)),
            high_quantity_threshold=max(
                1, _get_int("ECOMMERCE_HIGH_QUANTITY_THRESHOLD", 10)
            ),
            min_margin_to_auto_log=_get_float("ECOMMERCE_MIN_MARGIN_TO_AUTO_LOG", 0.0),
        )
