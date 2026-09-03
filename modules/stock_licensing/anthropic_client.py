"""Thin Anthropic Messages API client (stdlib only).

Docs: https://docs.claude.com/en/api/messages

One endpoint: ``POST /v1/messages``, authenticated with an ``x-api-key``
header (not Bearer) plus a pinned ``anthropic-version``.

Every network call is wrapped so a failure raises :class:`AnthropicError`
rather than leaking a raw urllib exception — the caller turns that into a
logged error and a graceful exit, so a flaky API never crashes the scheduler.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any

API_URL = "https://api.anthropic.com/v1/messages"
API_VERSION = "2023-06-01"
USER_AGENT = "income-orchestrator-stock-licensing/1.0 (+local)"
DEFAULT_TIMEOUT = 60


class AnthropicError(RuntimeError):
    """Raised when the Anthropic API cannot be reached or returns bad data."""


def complete(
    api_key: str,
    prompt: str,
    *,
    model: str,
    max_tokens: int = 500,
    timeout: int = DEFAULT_TIMEOUT,
) -> str:
    """Send one user-turn prompt, return the concatenated text of the reply."""
    body = json.dumps(
        {
            "model": model,
            "max_tokens": max_tokens,
            "messages": [{"role": "user", "content": prompt}],
        }
    ).encode("utf-8")
    req = urllib.request.Request(
        API_URL,
        data=body,
        headers={
            "x-api-key": api_key,
            "anthropic-version": API_VERSION,
            "Content-Type": "application/json",
            "User-Agent": USER_AGENT,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            if resp.status != 200:
                raise AnthropicError(f"HTTP {resp.status} from {API_URL}")
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = ""
        try:
            detail = exc.read().decode("utf-8")[:300]
        except Exception:  # pragma: no cover - best effort
            pass
        raise AnthropicError(f"HTTP {exc.code} from {API_URL}. {detail}") from exc
    except urllib.error.URLError as exc:
        raise AnthropicError(f"Network error reaching {API_URL}: {exc.reason}") from exc
    except (json.JSONDecodeError, ValueError) as exc:
        raise AnthropicError(f"Bad JSON from {API_URL}: {exc}") from exc
    except TimeoutError as exc:
        raise AnthropicError(f"Timed out reaching {API_URL}") from exc

    return _extract_text(data)


def _extract_text(data: dict[str, Any]) -> str:
    blocks = data.get("content")
    if not isinstance(blocks, list):
        raise AnthropicError("Expected a 'content' list in the Anthropic response")
    text = "".join(
        block.get("text", "") for block in blocks if block.get("type") == "text"
    )
    if not text:
        raise AnthropicError("Anthropic response had no text content")
    return text
