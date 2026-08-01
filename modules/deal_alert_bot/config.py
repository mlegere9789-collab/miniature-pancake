"""Deal-Alert Bot settings.

Everything is configurable via `.env` (loaded through the orchestrator's
`config`), with sensible defaults so the bot works out of the box in dry-run
mode. No secrets or tunables are hardcoded into the logic.

Relevant `.env` keys (all optional except the webhook when going live):

    DISCORD_WEBHOOK_URL           Where deals get posted. If unset, the bot
                                  stays in dry-run no matter what.
    DEAL_ALERT_DRY_RUN            "true" (default) logs what it *would* post;
                                  "false" actually posts to Discord.
    DEAL_MIN_SAVINGS             Minimum discount %% to qualify (default 50).
    DEAL_MAX_PRICE               Only deals at/below this sale price (USD).
                                  Empty = no price cap.
    DEAL_REQUIRE_HISTORIC_LOW    "true" = only alert when the sale price ties
                                  or beats the cheapest price ever seen
                                  (costs one extra API call per candidate).
    DEAL_STORE_IDS               Comma list of CheapShark store IDs
                                  (default "1" = Steam). See `stores` in
                                  the README.
    DEAL_PAGE_SIZE               How many deals to pull per store per run
                                  (default 60, max 60 per CheapShark).
    DEAL_MAX_POSTS_PER_RUN       Cap on alerts per run so you never flood the
                                  channel (default 5).
    DEAL_AFFILIATE_LINK_TEMPLATE Optional. A URL template with {deal_url} in
                                  it; used once your affiliate program is
                                  approved. Empty = plain store link + a
                                  visible "affiliate link placeholder" note.
"""

from __future__ import annotations

from dataclasses import dataclass

from orchestrator import config


def _get_float(key: str, default: float | None) -> float | None:
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


def _get_bool(key: str, default: bool) -> bool:
    raw = config.get(key)
    if raw is None or raw.strip() == "":
        return default
    return raw.strip().lower() in ("1", "true", "yes", "on")


@dataclass
class Settings:
    webhook_url: str | None
    dry_run: bool
    min_savings: float
    max_price: float | None
    require_historic_low: bool
    store_ids: list[str]
    page_size: int
    max_posts_per_run: int
    affiliate_template: str

    @classmethod
    def load(cls) -> "Settings":
        webhook = config.get("DISCORD_WEBHOOK_URL") or None
        # Dry-run if explicitly requested OR if there's no webhook to post to.
        dry_run = _get_bool("DEAL_ALERT_DRY_RUN", True) or not webhook
        raw_stores = config.get("DEAL_STORE_IDS", "1") or "1"
        store_ids = [s.strip() for s in raw_stores.split(",") if s.strip()]
        return cls(
            webhook_url=webhook,
            dry_run=dry_run,
            min_savings=_get_float("DEAL_MIN_SAVINGS", 50.0) or 0.0,
            max_price=_get_float("DEAL_MAX_PRICE", None),
            require_historic_low=_get_bool("DEAL_REQUIRE_HISTORIC_LOW", False),
            store_ids=store_ids or ["1"],
            page_size=max(1, min(_get_int("DEAL_PAGE_SIZE", 60), 60)),
            max_posts_per_run=max(1, _get_int("DEAL_MAX_POSTS_PER_RUN", 5)),
            affiliate_template=config.get("DEAL_AFFILIATE_LINK_TEMPLATE", "") or "",
        )
