"""Asset queue: what you want title/keyword metadata drafted for.

This module doesn't invent or generate images/video/audio — you describe
what's already shot/rendered in a small git-ignored file you maintain:

    data/stock_assets.json
    [
      {
        "id": "sunset-beach-01",
        "filename": "sunset-beach-01.jpg",
        "category": "photo",
        "subject": "golden hour beach with silhouetted palm trees",
        "notes": "shot on a 50mm, warm color grade"
      }
    ]

Each asset gets keyworded once (see ``dedup.py``); add a new ``id`` (or
delete the old one from the seen-store file) to redraft.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from orchestrator.paths import DATA_DIR

ASSETS_FILE = DATA_DIR / "stock_assets.json"

REQUIRED_FIELDS = ("id", "filename")


def load_assets(path: Path = ASSETS_FILE) -> list[dict[str, Any]]:
    """Return the queued assets, skipping any malformed entries."""
    if not path.exists():
        return []
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return []
    if not isinstance(raw, list):
        return []
    assets: list[dict[str, Any]] = []
    for entry in raw:
        if not isinstance(entry, dict):
            continue
        if not all(entry.get(field) for field in REQUIRED_FIELDS):
            continue
        assets.append(entry)
    return assets
