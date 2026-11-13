"""Stock Asset Licensing entry point.

Flow each run:
    1. Load the queued assets (data/stock_assets.json — you maintain this
       file; see assets.py).
    2. Skip assets already keyworded on a prior run (dedup, by asset id).
    3. For each fresh asset (capped at STOCK_LICENSING_MAX_PER_RUN), ask
       Claude to draft a title/description/keywords from what you described.
    4. Flag the draft to the review queue — AI-generated metadata always
       needs a human look, and nothing here uploads anywhere. A reply that
       doesn't parse as the expected JSON shape is still flagged, with the
       raw text, so nothing gets lost to a formatting slip.
    5. Log everything (activity + errors) to the shared database -> dashboard.

Nothing here is allowed to raise out of `run()`: API failures are caught and
logged so the scheduler keeps ticking.

Note on scope: the module README describes this program as eventually
uploading assets to contributor marketplaces (Adobe Stock, Shutterstock) and
tracking accepted/rejected royalties. Neither marketplace exposes a public
contributor API to build reliably against, so this pass covers what's
actually buildable without guessing an endpoint: drafting title/description/
keyword metadata for your review, ready to paste in by hand when you upload.
Royalties are still recorded the same way every other module does — via
`log.earning(...)`, e.g. from `orchestrator export-earnings` once you're
tracking payouts yourself, or by scripting your own import if a marketplace
ever offers one.

Usage:
    python -m modules.stock_licensing.run
"""

from __future__ import annotations

import sys

from orchestrator import get_logger

from . import anthropic_client
from .assets import load_assets
from .config import Settings
from .dedup import SeenStore
from .formatter import format_parse_failure_description, format_review_description
from .keywords import MetadataParseError, build_prompt, parse_reply

MODULE = "stock_licensing"


def run() -> int:
    """Run one keywording cycle. Returns the number of drafts flagged for review."""
    log = get_logger(MODULE)
    settings = Settings.load()

    if not settings.configured:
        log.status("idle", "No ANTHROPIC_API_KEY configured — nothing to draft")
        return 0

    assets = load_assets()
    if not assets:
        log.status("ok", "No assets queued (data/stock_assets.json)")
        return 0

    seen = SeenStore()
    fresh = [a for a in assets if not seen.is_seen(a["id"])]
    if not fresh:
        log.status("ok", f"All {len(assets)} queued assets already keyworded")
        return 0

    log.status("running", f"Drafting metadata for {len(fresh)} asset(s)")

    drafted = 0
    errors = 0
    for asset in fresh[: settings.max_per_run]:
        prompt = build_prompt(asset)
        try:
            reply = anthropic_client.complete(
                settings.anthropic_api_key,
                prompt,
                model=settings.model,
                max_tokens=settings.max_tokens,
            )
        except anthropic_client.AnthropicError as exc:
            errors += 1
            log.error(
                f"Drafting failed for asset '{asset['id']}': {exc}",
                event="api_error",
            )
            continue  # not marked seen: retried next run

        try:
            metadata = parse_reply(reply)
        except MetadataParseError as exc:
            log.warning(
                f"Reply for asset '{asset['id']}' didn't parse: {exc}",
                event="parse_error",
            )
            log.flag_for_review(
                f"Needs manual keywords: {asset.get('filename', asset['id'])}",
                description=format_parse_failure_description(asset, reply),
                payload={"asset_id": asset["id"], "raw_reply": reply},
            )
            seen.mark(asset["id"])
            drafted += 1
            continue

        log.flag_for_review(
            f"Approve metadata: {asset.get('filename', asset['id'])}",
            description=format_review_description(asset, metadata),
            payload={
                "asset_id": asset["id"],
                "title": metadata.title,
                "description": metadata.description,
                "keywords": metadata.keywords,
            },
        )
        log.activity(
            f"Drafted metadata for '{asset['id']}': {metadata.title}",
            event="draft_created",
            metadata={"asset_id": asset["id"]},
        )
        seen.mark(asset["id"])
        drafted += 1

    seen.save()

    summary = (
        f"drafted {drafted}/{len(fresh)} assets{f', {errors} errors' if errors else ''}"
    )
    log.activity(summary, event="run_summary")
    log.status("warning" if errors and drafted == 0 else "ok", summary)
    return drafted


def main(argv: list[str] | None = None) -> int:
    del argv  # no CLI options yet — everything is `.env`-driven
    try:
        run()
        return 0
    except Exception as exc:  # last-resort guard so cron never sees a crash
        get_logger(MODULE).error(f"Unexpected failure: {exc}", event="fatal")
        get_logger(MODULE).status("error", f"Crashed: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
