"""Turn drafted copy (or a failed draft) into review-queue text.

Kept separate from networking and parsing so it is trivial to unit-test
offline.
"""

from __future__ import annotations

from typing import Any

from .copywriter import ListingCopy


def format_review_description(brief: dict[str, Any], copy: ListingCopy) -> str:
    tags = ", ".join(copy.tags)
    return (
        f"Title: {copy.title}\n\n"
        f"{copy.description}\n\n"
        f"Tags: {tags}\n\n"
        f"Drafted from brief '{brief.get('id')}' — AI-generated, review before "
        f"publishing anywhere."
    )


def format_parse_failure_description(brief: dict[str, Any], raw_text: str) -> str:
    return (
        f"Drafting brief '{brief.get('id')}' produced a reply that wasn't the "
        f"expected JSON shape, so it needs a manual rewrite instead of an "
        f"auto-parsed listing. Raw model reply:\n\n{raw_text}"
    )
