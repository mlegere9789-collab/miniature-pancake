"""Post messages to a Discord channel via an incoming webhook (stdlib only).

A webhook URL looks like:
    https://discord.com/api/webhooks/<id>/<token>

On success Discord returns HTTP 204 (No Content). We surface rate limits and
other failures as :class:`DiscordError` so the caller can log them and keep
going — a failing webhook must never crash the scheduler.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any

DEFAULT_TIMEOUT = 15


class DiscordError(RuntimeError):
    """Raised when posting to the Discord webhook fails."""


def post_webhook(
    webhook_url: str, payload: dict[str, Any], *, timeout: int = DEFAULT_TIMEOUT
) -> None:
    """Post one message. Raises DiscordError on any failure."""
    if not webhook_url:
        raise DiscordError("No webhook URL configured")

    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        webhook_url,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            # Discord returns 204 on success; 200 with ?wait=true.
            if resp.status not in (200, 204):
                raise DiscordError(f"Unexpected HTTP {resp.status} from Discord")
    except urllib.error.HTTPError as exc:
        detail = ""
        try:
            detail = exc.read().decode("utf-8")[:200]
        except Exception:  # pragma: no cover - best effort
            pass
        if exc.code == 429:
            raise DiscordError(f"Rate limited by Discord (429). {detail}") from exc
        raise DiscordError(f"Discord webhook HTTP {exc.code}. {detail}") from exc
    except urllib.error.URLError as exc:
        raise DiscordError(f"Network error posting to Discord: {exc.reason}") from exc
    except TimeoutError as exc:
        raise DiscordError("Timed out posting to Discord") from exc
