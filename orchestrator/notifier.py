"""Push a review-queue item to an external webhook (stdlib only, optional).

Nothing currently pings you when something lands in the review queue — you
only find out by opening the dashboard. This is an opt-in way to also get a
push notification wherever a webhook can deliver one (Slack, Discord, ntfy,
a generic relay), configured with one URL.

Unconfigured (`REVIEW_NOTIFY_WEBHOOK_URL` blank) is the default and is a
silent no-op — nothing here ever becomes required just by existing.

Payload shape is picked by `REVIEW_NOTIFY_FORMAT`:
    slack     {"text": "..."}      (Slack incoming webhooks)
    discord   {"content": "..."}   (Discord incoming webhooks; see also
                                    modules/deal_alert_bot/discord_notifier.py
                                    for a module posting richer embeds)
    generic   {"text": ..., "content": ..., "message": "..."} (default —
                                    covers most other receivers, which
                                    generally read one of these three keys
                                    and ignore what they don't recognize)
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request

DEFAULT_TIMEOUT = 15
VALID_FORMATS = ("generic", "slack", "discord")


class NotifyError(RuntimeError):
    """Raised when posting to the configured webhook fails."""


def _payload_for(text: str, fmt: str) -> dict[str, str]:
    if fmt == "slack":
        return {"text": text}
    if fmt == "discord":
        return {"content": text}
    return {"text": text, "content": text, "message": text}


def notify(
    webhook_url: str,
    text: str,
    *,
    format: str = "generic",  # noqa: A002 - matches the .env key name
    timeout: int = DEFAULT_TIMEOUT,
) -> None:
    """Post one notification. Raises NotifyError on any failure."""
    if not webhook_url:
        raise NotifyError("No webhook URL configured")

    fmt = format if format in VALID_FORMATS else "generic"
    body = json.dumps(_payload_for(text, fmt)).encode("utf-8")
    req = urllib.request.Request(
        webhook_url,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            # Discord returns 204; Slack and most others return 200.
            if resp.status not in (200, 204):
                raise NotifyError(f"Unexpected HTTP {resp.status} from webhook")
    except urllib.error.HTTPError as exc:
        detail = ""
        try:
            detail = exc.read().decode("utf-8")[:200]
        except Exception:  # pragma: no cover - best effort
            pass
        if exc.code == 429:
            raise NotifyError(f"Rate limited by webhook (429). {detail}") from exc
        raise NotifyError(f"Webhook HTTP {exc.code}. {detail}") from exc
    except urllib.error.URLError as exc:
        raise NotifyError(f"Network error posting to webhook: {exc.reason}") from exc
    except TimeoutError as exc:
        raise NotifyError("Timed out posting to webhook") from exc
