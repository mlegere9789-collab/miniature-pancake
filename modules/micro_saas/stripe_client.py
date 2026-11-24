"""Thin Stripe API client (stdlib only).

Docs: https://stripe.com/docs/api

Stripe authenticates with HTTP Basic auth: the secret key as the username,
an empty password. We use two read-only endpoints:

* ``GET /v1/subscriptions``  — active/all subscriptions, for MRR + churn.
* ``GET /v1/charges``        — charges since a given time, for realized
                               revenue and failed/refunded payments.

Every network call is wrapped so a failure raises :class:`StripeError`
rather than leaking a raw urllib exception — the caller turns that into a
logged error and a graceful exit, so a flaky API never crashes the scheduler.
"""

from __future__ import annotations

import base64
import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

API_BASE = "https://api.stripe.com/v1"
USER_AGENT = "income-orchestrator-micro-saas/1.0 (+local)"
DEFAULT_TIMEOUT = 20


class StripeError(RuntimeError):
    """Raised when the Stripe API cannot be reached or returns bad data."""


def _auth_header(api_key: str) -> str:
    token = base64.b64encode(f"{api_key}:".encode("utf-8")).decode("ascii")
    return f"Basic {token}"


def _get_json(api_key: str, path: str, params: dict[str, Any], timeout: int) -> Any:
    query = urllib.parse.urlencode(params)
    url = f"{API_BASE}{path}?{query}" if query else f"{API_BASE}{path}"
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": _auth_header(api_key),
            "User-Agent": USER_AGENT,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            if resp.status != 200:
                raise StripeError(f"HTTP {resp.status} from {path}")
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = ""
        try:
            detail = exc.read().decode("utf-8")[:200]
        except Exception:  # pragma: no cover - best effort
            pass
        raise StripeError(f"HTTP {exc.code} from {path}. {detail}") from exc
    except urllib.error.URLError as exc:
        raise StripeError(f"Network error reaching {path}: {exc.reason}") from exc
    except (json.JSONDecodeError, ValueError) as exc:
        raise StripeError(f"Bad JSON from {path}: {exc}") from exc
    except TimeoutError as exc:
        raise StripeError(f"Timed out reaching {path}") from exc


def list_active_subscriptions(
    api_key: str, *, limit: int = 100, timeout: int = DEFAULT_TIMEOUT
) -> list[dict[str, Any]]:
    """Return all currently-active subscriptions (auto-paginated up to `limit` total)."""
    return _list_all(api_key, "/subscriptions", {"status": "active"}, limit, timeout)


def list_charges_since(
    api_key: str, since_unix: int, *, limit: int = 100, timeout: int = DEFAULT_TIMEOUT
) -> list[dict[str, Any]]:
    """Return charges created at or after `since_unix` (auto-paginated up to `limit` total)."""
    return _list_all(api_key, "/charges", {"created[gte]": since_unix}, limit, timeout)


def _list_all(
    api_key: str,
    path: str,
    base_params: dict[str, Any],
    limit: int,
    timeout: int,
) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    starting_after: str | None = None
    page_size = min(100, max(1, limit))
    while len(items) < limit:
        params = dict(base_params)
        params["limit"] = min(page_size, limit - len(items))
        if starting_after:
            params["starting_after"] = starting_after
        data = _get_json(api_key, path, params, timeout)
        page = data.get("data")
        if not isinstance(page, list):
            raise StripeError(f"Expected a 'data' list from {path}")
        if not page:
            break
        items.extend(page)
        if not data.get("has_more"):
            break
        starting_after = page[-1].get("id")
        if not starting_after:
            break
    return items[:limit]
