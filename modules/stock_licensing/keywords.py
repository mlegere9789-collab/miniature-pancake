"""Build the metadata-drafting prompt and parse the model's reply.

Kept separate from networking so both directions are trivial to unit-test
offline: the prompt builder takes a plain asset dict, and the parser takes a
plain string (a canned reply, in tests — the model's real one in production).
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

PROMPT_TEMPLATE = """\
You are drafting contributor metadata for a stock {category} asset, for a
human to review and use when uploading to a marketplace like Adobe Stock or
Shutterstock. Reply with ONLY a JSON object — no markdown fences, no
commentary — shaped exactly like:

{{"title": "...", "description": "...", "keywords": ["...", "..."]}}

Rules:
- title: under 200 characters, plainly describes the subject, no clickbait.
- description: 1-3 short sentences, plain text (no markdown), factual.
- keywords: 15-30 lowercase, single-word-or-short-phrase search terms,
  ordered most-relevant first, no duplicates.

Subject: {subject}
Notes: {notes}
"""


class MetadataParseError(RuntimeError):
    """Raised when the model's reply isn't the expected JSON shape."""


def build_prompt(asset: dict[str, Any]) -> str:
    return PROMPT_TEMPLATE.format(
        category=asset.get("category", "photo"),
        subject=asset.get("subject") or asset.get("filename", "an unspecified asset"),
        notes=asset.get("notes") or "none",
    )


@dataclass
class AssetMetadata:
    title: str
    description: str
    keywords: list[str]


def parse_reply(text: str) -> AssetMetadata:
    """Parse the model's JSON reply. Raises MetadataParseError if malformed."""
    stripped = text.strip()
    # Models sometimes wrap JSON in a markdown fence despite instructions.
    if stripped.startswith("```"):
        stripped = stripped.strip("`")
        if stripped.startswith("json"):
            stripped = stripped[len("json") :]
        stripped = stripped.strip()

    try:
        data = json.loads(stripped)
    except json.JSONDecodeError as exc:
        raise MetadataParseError(f"Reply was not valid JSON: {exc}") from exc

    if not isinstance(data, dict):
        raise MetadataParseError("Reply JSON was not an object")

    title = data.get("title")
    description = data.get("description")
    keywords = data.get("keywords")
    if not isinstance(title, str) or not title.strip():
        raise MetadataParseError("Reply JSON missing a non-empty 'title'")
    if not isinstance(description, str) or not description.strip():
        raise MetadataParseError("Reply JSON missing a non-empty 'description'")
    if not isinstance(keywords, list) or not all(isinstance(k, str) for k in keywords):
        raise MetadataParseError("Reply JSON 'keywords' must be a list of strings")

    return AssetMetadata(
        title=title.strip(), description=description.strip(), keywords=keywords
    )
