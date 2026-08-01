"""Thin CheapShark API client (stdlib only).

CheapShark is free and needs no API key. Docs: https://apidocs.cheapshark.com/

We use two endpoints:

* ``GET /deals``        — a list of current deals (filtered/sorted server-side).
* ``GET /deals?id=..``  — details for one deal, including the cheapest price
                          ever seen (used for the optional historic-low filter).

Every network call is wrapped so a failure raises :class:`CheapSharkError`
rather than leaking a raw urllib exception — the caller turns that into a
logged error and a graceful exit, so a flaky API never crashes the scheduler.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

API_BASE = "https://www.cheapshark.com/api/1.0"
USER_AGENT = "income-orchestrator-deal-alert-bot/1.0 (+local)"
DEFAULT_TIMEOUT = 20


class CheapSharkError(RuntimeError):
    """Raised when the CheapShark API cannot be reached or returns bad data."""


def _get_json(path: str, params: dict[str, Any], timeout: int) -> Any:
    query = urllib.parse.urlencode(params)
    url = f"{API_BASE}{path}?{query}" if query else f"{API_BASE}{path}"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            if resp.status != 200:
                raise CheapSharkError(f"HTTP {resp.status} from {url}")
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        raise CheapSharkError(f"HTTP {exc.code} from {url}") from exc
    except urllib.error.URLError as exc:
        raise CheapSharkError(f"Network error reaching {url}: {exc.reason}") from exc
    except (json.JSONDecodeError, ValueError) as exc:
        raise CheapSharkError(f"Bad JSON from {url}: {exc}") from exc
    except TimeoutError as exc:
        raise CheapSharkError(f"Timed out reaching {url}") from exc


def fetch_deals(
    store_id: str,
    *,
    page_size: int = 60,
    sort_by: str = "Savings",
    on_sale: bool = True,
    timeout: int = DEFAULT_TIMEOUT,
) -> list[dict[str, Any]]:
    """Return current deals for one store, best savings first."""
    params = {
        "storeID": store_id,
        "pageSize": page_size,
        "sortBy": sort_by,
        "onSale": 1 if on_sale else 0,
    }
    data = _get_json("/deals", params, timeout)
    if not isinstance(data, list):
        raise CheapSharkError("Expected a list of deals from /deals")
    return data


def cheapest_price_ever(
    deal_id: str, *, timeout: int = DEFAULT_TIMEOUT
) -> float | None:
    """Return the cheapest price ever recorded for a deal, or None if unknown."""
    data = _get_json("/deals", {"id": deal_id}, timeout)
    if not isinstance(data, dict):
        return None
    cheapest = data.get("cheapestPrice") or {}
    price = cheapest.get("price")
    try:
        return float(price) if price is not None else None
    except (TypeError, ValueError):
        return None
