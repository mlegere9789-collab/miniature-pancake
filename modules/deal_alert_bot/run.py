"""Deal-Alert Bot entry point.

Flow each run:
    1. Pull current deals from CheapShark (per configured store).
    2. Keep only deals that meet the savings / price / historic-low threshold.
    3. Skip anything we've already alerted on (dedup).
    4. Format each qualifying deal and either:
         - DRY-RUN: log what it *would* post (default), or
         - LIVE:    post to the Discord webhook.
    5. Log everything (posts + errors) to the shared database → dashboard.

Nothing here is allowed to raise out of `run()`: API/webhook failures are
caught and logged so the scheduler keeps ticking.

Usage:
    python -m modules.deal_alert_bot.run            # uses .env settings
    python -m modules.deal_alert_bot.run --live     # force real posting
    python -m modules.deal_alert_bot.run --dry-run  # force dry-run
    python -m modules.deal_alert_bot.run --limit 3  # cap posts this run
"""

from __future__ import annotations

import argparse
import sys
from typing import Any

from orchestrator import get_logger

from . import cheapshark, formatter
from .config import Settings
from .dedup import SeenStore
from .discord_notifier import DiscordError, post_webhook

MODULE = "deal_alert_bot"


def qualifies(
    deal: dict[str, Any], settings: Settings, cheapest_ever: float | None = None
) -> bool:
    """Pure predicate: does this deal meet the configured threshold?

    ``cheapest_ever`` is only consulted when historic-low filtering is on;
    pass it in so this function stays free of network calls (and testable).
    """
    try:
        savings = float(deal.get("savings", 0) or 0)
        sale = float(deal.get("salePrice", 0) or 0)
    except (TypeError, ValueError):
        return False

    if savings < settings.min_savings:
        return False
    if settings.max_price is not None and sale > settings.max_price:
        return False
    if settings.require_historic_low:
        if cheapest_ever is None:
            return False
        # Tie or beat the historic low (allow a 1-cent rounding tolerance).
        if sale > cheapest_ever + 0.01:
            return False
    return True


def _collect_deals(settings: Settings, log) -> tuple[list[dict[str, Any]], int]:
    """Fetch deals from every configured store. Returns (deals, stores_failed)."""
    deals: list[dict[str, Any]] = []
    failed = 0
    for store_id in settings.store_ids:
        try:
            batch = cheapshark.fetch_deals(store_id, page_size=settings.page_size)
            deals.extend(batch)
        except cheapshark.CheapSharkError as exc:
            failed += 1
            log.error(
                f"CheapShark fetch failed for store {store_id}: {exc}",
                event="api_error",
            )
    return deals, failed


def run(dry_run_override: bool | None = None, limit_override: int | None = None) -> int:
    """Run one polling cycle. Returns the number of deals posted (or would-post)."""
    log = get_logger(MODULE)
    settings = Settings.load()
    if dry_run_override is not None:
        settings.dry_run = dry_run_override
    if limit_override is not None:
        settings.max_posts_per_run = max(1, limit_override)

    mode = "DRY-RUN" if settings.dry_run else "LIVE"
    if settings.dry_run and not settings.webhook_url:
        mode = "DRY-RUN (no webhook set)"
    log.status("running", f"Scanning CheapShark · {mode}")

    deals, stores_failed = _collect_deals(settings, log)
    if not deals:
        msg = (
            "No deals returned (all store fetches failed)"
            if stores_failed
            else "No deals returned from CheapShark"
        )
        log.warning(msg, event="no_deals")
        log.status("warning" if stores_failed else "ok", msg)
        return 0

    seen = SeenStore()

    # Stage 1: cheap, network-free filter (savings + price cap).
    prelim = [d for d in deals if qualifies(d, settings)]

    # Stage 2: optional historic-low check (one extra API call per candidate).
    qualifying: list[dict[str, Any]] = []
    for deal in prelim:
        if settings.require_historic_low:
            try:
                low = cheapshark.cheapest_price_ever(deal.get("dealID", ""))
            except cheapshark.CheapSharkError as exc:
                log.warning(
                    f"Historic-low lookup failed for " f"{deal.get('title')}: {exc}",
                    event="api_error",
                )
                continue
            if not qualifies(deal, settings, cheapest_ever=low):
                continue
        qualifying.append(deal)

    # Stage 3: drop anything already alerted on.
    fresh = [d for d in qualifying if not seen.is_seen(str(d.get("dealID")))]

    posted = 0
    errors = 0
    for deal in fresh[: settings.max_posts_per_run]:
        url, is_aff = formatter.build_deal_url(deal, settings.affiliate_template)
        text = formatter.format_plaintext(deal, url, is_aff)
        meta = {
            "deal_id": deal.get("dealID"),
            "title": deal.get("title"),
            "sale_price": deal.get("salePrice"),
            "savings": deal.get("savings"),
            "url": url,
        }

        if settings.dry_run:
            # Do NOT mark as seen: when you flip to live, these still post once.
            log.activity(
                f"[DRY-RUN] would post: {text}", event="deal_dryrun", metadata=meta
            )
            posted += 1
            continue

        payload = formatter.format_discord_payload(deal, url, is_aff)
        try:
            post_webhook(settings.webhook_url, payload)
        except DiscordError as exc:
            errors += 1
            log.error(
                f"Failed to post '{deal.get('title')}': {exc}",
                event="post_error",
                metadata=meta,
            )
            continue
        seen.mark(str(deal.get("dealID")))
        log.activity(f"Posted deal: {text}", event="deal_posted", metadata=meta)
        posted += 1

    seen.save()

    summary = (
        f"{mode}: scanned {len(deals)}, {len(fresh)} new qualifying, "
        f"{posted} {'logged' if settings.dry_run else 'posted'}"
        f"{f', {errors} errors' if errors else ''}"
    )
    state = "warning" if (errors and posted == 0) or stores_failed else "ok"
    log.activity(summary, event="run_summary")
    log.status(state, summary)
    return posted


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Deal-Alert Bot (CheapShark → Discord)"
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--live", action="store_true", help="force real posting")
    group.add_argument("--dry-run", action="store_true", help="force dry-run")
    parser.add_argument(
        "--limit", type=int, default=None, help="max posts this run (overrides config)"
    )
    args = parser.parse_args(argv)

    dry_override: bool | None = None
    if args.live:
        dry_override = False
    elif args.dry_run:
        dry_override = True

    try:
        run(dry_run_override=dry_override, limit_override=args.limit)
        return 0
    except Exception as exc:  # last-resort guard so cron never sees a crash
        get_logger(MODULE).error(f"Unexpected failure: {exc}", event="fatal")
        get_logger(MODULE).status("error", f"Crashed: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
