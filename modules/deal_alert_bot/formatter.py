"""Turn a raw CheapShark deal into a link + a Discord message.

Kept separate from networking so it is trivial to unit-test offline.
"""

from __future__ import annotations

from typing import Any

REDIRECT_BASE = "https://www.cheapshark.com/redirect?dealID="


def _f(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def build_deal_url(
    deal: dict[str, Any], affiliate_template: str = ""
) -> tuple[str, bool]:
    """Return (url, is_affiliate).

    The base link is CheapShark's redirect, which forwards to the actual store.
    If an affiliate template is configured (containing ``{deal_url}``), we wrap
    the base link with it. Otherwise we return the plain link and signal that an
    affiliate wrapper is still a placeholder.
    """
    base = f"{REDIRECT_BASE}{deal.get('dealID', '')}"
    if affiliate_template and "{deal_url}" in affiliate_template:
        return affiliate_template.format(deal_url=base), True
    return base, False


def format_plaintext(deal: dict[str, Any], url: str, is_affiliate: bool) -> str:
    """A one-line summary used for logs and dry-run output."""
    title = deal.get("title", "Unknown title")
    sale = _f(deal.get("salePrice"))
    normal = _f(deal.get("normalPrice"))
    savings = round(_f(deal.get("savings")))
    tag = "" if is_affiliate else "  [affiliate link placeholder]"
    return f"{title} — ${sale:.2f} (was ${normal:.2f}, {savings}% off) — {url}{tag}"


def format_discord_payload(
    deal: dict[str, Any], url: str, is_affiliate: bool
) -> dict[str, Any]:
    """Build the JSON payload for a Discord webhook (a single rich embed)."""
    title = deal.get("title", "Unknown title")
    sale = _f(deal.get("salePrice"))
    normal = _f(deal.get("normalPrice"))
    savings = round(_f(deal.get("savings")))
    thumb = deal.get("thumb")

    footer = (
        "Affiliate link"
        if is_affiliate
        else "Affiliate link placeholder — add your ID in .env"
    )
    embed: dict[str, Any] = {
        "title": f"🎮 {title}",
        "url": url,
        "color": 0x1A7F37,
        "fields": [
            {
                "name": "Price",
                "value": f"**${sale:.2f}** ~~${normal:.2f}~~",
                "inline": True,
            },
            {"name": "Discount", "value": f"{savings}% off", "inline": True},
        ],
        "footer": {"text": footer},
    }
    if thumb:
        embed["thumbnail"] = {"url": thumb}

    return {
        "content": f"**{savings}% off** — {title} for **${sale:.2f}**",
        "embeds": [embed],
    }
