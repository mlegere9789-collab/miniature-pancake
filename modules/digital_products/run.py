"""Digital Product Creation entry point.

Flow each run:
    1. Load queued product briefs (data/product_briefs.json — you maintain
       this file; see briefs.py).
    2. Skip briefs already drafted on a prior run (dedup, by brief id).
    3. For each fresh brief (capped at DIGITAL_PRODUCTS_MAX_PER_RUN), ask
       Claude to draft a title/description/tags from the brief.
    4. Flag the draft to the review queue — AI-generated copy always needs a
       human look before it goes live anywhere. A reply that doesn't parse
       as the expected JSON shape is still flagged, with the raw text, so
       nothing gets lost to a formatting slip.
    5. Log everything (activity + errors) to the shared database -> dashboard.

Nothing here is allowed to raise out of `run()`: API failures are caught and
logged so the scheduler keeps ticking.

Note on scope: the module README describes this program as eventually
publishing/updating listings on Etsy or Gumroad. That needs real product
files and a marketplace account to mean anything, and is not implemented
here; this pass covers drafting copy for your review, which is everything
the foundation (the review queue) can act on before a human approves it.

Usage:
    python -m modules.digital_products.run
"""

from __future__ import annotations

import sys

from orchestrator import get_logger

from . import anthropic_client
from .briefs import load_briefs
from .config import Settings
from .copywriter import CopyParseError, build_prompt, parse_reply
from .dedup import SeenStore
from .formatter import format_parse_failure_description, format_review_description

MODULE = "digital_products"


def run() -> int:
    """Run one drafting cycle. Returns the number of drafts flagged for review."""
    log = get_logger(MODULE)
    settings = Settings.load()

    if not settings.configured:
        log.status("idle", "No ANTHROPIC_API_KEY configured — nothing to draft")
        return 0

    briefs = load_briefs()
    if not briefs:
        log.status("ok", "No product briefs queued (data/product_briefs.json)")
        return 0

    seen = SeenStore()
    fresh = [b for b in briefs if not seen.is_seen(b["id"])]
    if not fresh:
        log.status("ok", f"All {len(briefs)} queued briefs already drafted")
        return 0

    log.status("running", f"Drafting copy for {len(fresh)} brief(s)")

    drafted = 0
    errors = 0
    for brief in fresh[: settings.max_per_run]:
        prompt = build_prompt(brief)
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
                f"Drafting failed for brief '{brief['id']}': {exc}",
                event="api_error",
            )
            continue  # not marked seen: retried next run

        try:
            copy = parse_reply(reply)
        except CopyParseError as exc:
            log.warning(
                f"Reply for brief '{brief['id']}' didn't parse: {exc}",
                event="parse_error",
            )
            log.flag_for_review(
                f"Needs manual rewrite: {brief.get('name', brief['id'])}",
                description=format_parse_failure_description(brief, reply),
                payload={"brief_id": brief["id"], "raw_reply": reply},
            )
            seen.mark(brief["id"])
            drafted += 1
            continue

        log.flag_for_review(
            f"Approve listing copy: {brief.get('name', brief['id'])}",
            description=format_review_description(brief, copy),
            payload={
                "brief_id": brief["id"],
                "title": copy.title,
                "description": copy.description,
                "tags": copy.tags,
            },
        )
        log.activity(
            f"Drafted copy for '{brief['id']}': {copy.title}",
            event="draft_created",
            metadata={"brief_id": brief["id"]},
        )
        seen.mark(brief["id"])
        drafted += 1

    seen.save()

    summary = (
        f"drafted {drafted}/{len(fresh)} briefs{f', {errors} errors' if errors else ''}"
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
