"""Thin Shopify Admin REST API client (stdlib only).

Docs: https://shopify.dev/docs/api/admin-rest/latest/resources/order

Uses the standard REST Admin API with a custom-app access token (created in
the store admin under Apps -> "Develop apps"), sent as the
``X-Shopify-Access-Token`` header — never as a query parameter, so it never
ends up in a log line or a proxy's URL history.

Every network call is wrapped so a failure raises :class:`ShopifyError`
rather than leaking a raw urllib exception — the caller turns that into a
logged error and a graceful exit, so a flaky API never crashes the scheduler.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

USER_AGENT = "income-orchestrator-ecommerce-dropshipping/1.0 (+local)"
DEFAULT_TIMEOUT = 20
DEFAULT_API_VERSION = "2024-01"
ORDER_FIELDS = (
    "id,name,created_at,financial_status,fulfillment_status,total_price,"
    "currency,shipping_address,line_items,refunds,tags"
)


class ShopifyError(RuntimeError):
    """Raised when the Shopify Admin API cannot be reached or returns bad data."""


def normalize_shop_domain(raw: str) -> str:
    """Turn a store URL or bare handle into ``<handle>.myshopify.com``.

    Accepts any of: ``my-shop``, ``my-shop.myshopify.com``,
    ``https://my-shop.myshopify.com``, ``https://my-shop.myshopify.com/``.
    """
    domain = raw.strip()
    domain = domain.removeprefix("https://").removeprefix("http://")
    domain = domain.rstrip("/")
    if not domain.endswith(".myshopify.com"):
        domain = f"{domain}.myshopify.com"
    return domain


def fetch_open_orders(
    shop_url: str,
    access_token: str,
    *,
    api_version: str = DEFAULT_API_VERSION,
    limit: int = 25,
    timeout: int = DEFAULT_TIMEOUT,
) -> list[dict[str, Any]]:
    """Return open (unarchived) orders, newest first, up to ``limit`` (max 250)."""
    domain = normalize_shop_domain(shop_url)
    params = {
        "status": "open",
        "limit": max(1, min(limit, 250)),
        "fields": ORDER_FIELDS,
    }
    query = urllib.parse.urlencode(params)
    url = f"https://{domain}/admin/api/{api_version}/orders.json?{query}"
    req = urllib.request.Request(
        url,
        headers={
            "X-Shopify-Access-Token": access_token,
            "Accept": "application/json",
            "User-Agent": USER_AGENT,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            if resp.status != 200:
                raise ShopifyError(f"HTTP {resp.status} from {url}")
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = ""
        try:
            detail = exc.read().decode("utf-8")[:200]
        except Exception:  # pragma: no cover - best effort
            pass
        raise ShopifyError(f"HTTP {exc.code} from {url}. {detail}") from exc
    except urllib.error.URLError as exc:
        raise ShopifyError(f"Network error reaching {url}: {exc.reason}") from exc
    except (json.JSONDecodeError, ValueError) as exc:
        raise ShopifyError(f"Bad JSON from {url}: {exc}") from exc
    except TimeoutError as exc:
        raise ShopifyError(f"Timed out reaching {url}") from exc

    orders = data.get("orders")
    if not isinstance(orders, list):
        raise ShopifyError("Expected an 'orders' list in the Shopify response")
    return orders
