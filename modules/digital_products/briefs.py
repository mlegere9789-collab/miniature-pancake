"""Product briefs: what you want listing copy drafted for.

This module doesn't invent products — you describe what to list in a small
git-ignored file you maintain:

    data/product_briefs.json
    [
      {
        "id": "notion-budget-tracker",
        "name": "Notion Budget Tracker Template",
        "category": "Notion template",
        "features": ["monthly budget view", "recurring bill tracker", "goal progress bars"],
        "audience": "young professionals building a budgeting habit",
        "price_usd": 9.0
      }
    ]

Each brief is drafted into listing copy once (see ``dedup.py``); add a new
``id`` (or delete the old one from the seen-store file) to redraft.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from orchestrator.paths import DATA_DIR

BRIEFS_FILE = DATA_DIR / "product_briefs.json"

REQUIRED_FIELDS = ("id", "name")


def load_briefs(path: Path = BRIEFS_FILE) -> list[dict[str, Any]]:
    """Return the queued briefs, skipping any malformed entries."""
    if not path.exists():
        return []
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return []
    if not isinstance(raw, list):
        return []
    briefs: list[dict[str, Any]] = []
    for entry in raw:
        if not isinstance(entry, dict):
            continue
        if not all(entry.get(field) for field in REQUIRED_FIELDS):
            continue
        briefs.append(entry)
    return briefs
