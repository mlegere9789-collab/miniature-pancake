"""Build the drafting prompt and parse the model's reply.

Kept separate from networking so both directions are trivial to unit-test
offline: the prompt builder takes a plain brief dict, and the parser takes a
plain string (a canned reply, in tests — the model's real one in production).
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

PROMPT_TEMPLATE = """\
You are drafting an e-commerce listing for a digital product. Write
marketplace-ready copy from the brief below, then reply with ONLY a JSON
object — no markdown fences, no commentary — shaped exactly like:

{{"title": "...", "description": "...", "tags": ["...", "..."]}}

Rules:
- title: under 140 characters, no clickbait, no ALL CAPS.
- description: 2-4 short paragraphs, plain text (no markdown), sell the
  benefit to the stated audience, mention the listed features naturally.
- tags: 5-13 lowercase, marketplace-style search tags, no duplicates.

Brief:
  Product: {name}
  Category: {category}
  Price: ${price}
  Audience: {audience}
  Features: {features}
"""


class CopyParseError(RuntimeError):
    """Raised when the model's reply isn't the expected JSON shape."""


def build_prompt(brief: dict[str, Any]) -> str:
    return PROMPT_TEMPLATE.format(
        name=brief.get("name", "Unnamed product"),
        category=brief.get("category", "digital product"),
        price=brief.get("price_usd", "not set"),
        audience=brief.get("audience", "a general audience"),
        features=", ".join(brief.get("features") or []) or "not specified",
    )


@dataclass
class ListingCopy:
    title: str
    description: str
    tags: list[str]


def parse_reply(text: str) -> ListingCopy:
    """Parse the model's JSON reply. Raises CopyParseError if it's malformed."""
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
        raise CopyParseError(f"Reply was not valid JSON: {exc}") from exc

    if not isinstance(data, dict):
        raise CopyParseError("Reply JSON was not an object")

    title = data.get("title")
    description = data.get("description")
    tags = data.get("tags")
    if not isinstance(title, str) or not title.strip():
        raise CopyParseError("Reply JSON missing a non-empty 'title'")
    if not isinstance(description, str) or not description.strip():
        raise CopyParseError("Reply JSON missing a non-empty 'description'")
    if not isinstance(tags, list) or not all(isinstance(t, str) for t in tags):
        raise CopyParseError("Reply JSON 'tags' must be a list of strings")

    return ListingCopy(title=title.strip(), description=description.strip(), tags=tags)
