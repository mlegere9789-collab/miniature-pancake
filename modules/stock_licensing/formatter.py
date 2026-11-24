"""Turn drafted metadata (or a failed draft) into review-queue text.

Kept separate from networking and parsing so it is trivial to unit-test
offline.
"""

from __future__ import annotations

from typing import Any

from .keywords import AssetMetadata


def format_review_description(asset: dict[str, Any], metadata: AssetMetadata) -> str:
    keywords = ", ".join(metadata.keywords)
    return (
        f"Title: {metadata.title}\n\n"
        f"{metadata.description}\n\n"
        f"Keywords: {keywords}\n\n"
        f"Drafted from asset '{asset.get('id')}' ({asset.get('filename')}) — "
        f"AI-generated, review before uploading anywhere."
    )


def format_parse_failure_description(asset: dict[str, Any], raw_text: str) -> str:
    return (
        f"Keywording asset '{asset.get('id')}' produced a reply that wasn't "
        f"the expected JSON shape, so it needs manual title/keywords instead "
        f"of an auto-parsed draft. Raw model reply:\n\n{raw_text}"
    )
