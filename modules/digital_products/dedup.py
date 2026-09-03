"""Remember which briefs we've already drafted copy for.

State lives in ``data/digital_products_seen.json`` (git-ignored). Keyed on a
brief's own ``id``, not a generated one, so editing a brief's other fields
and rerunning does NOT trigger a redraft — only a new (or explicitly
removed-then-reused) id does.

Unlike the other modules' dedup stores, entries here are not pruned by time:
a brief id is meant to be a stable, permanent identifier for one product, so
"forgetting" it after a TTL would just cause an unwanted, costly redraft.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

from orchestrator.paths import DATA_DIR, ensure_data_dir

SEEN_FILE = DATA_DIR / "digital_products_seen.json"


class SeenStore:
    def __init__(self) -> None:
        self._seen: dict[str, str] = {}
        self._load()

    def _load(self) -> None:
        if SEEN_FILE.exists():
            try:
                self._seen = json.loads(SEEN_FILE.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, OSError):
                self._seen = {}

    def is_seen(self, brief_id: str) -> bool:
        return brief_id in self._seen

    def mark(self, brief_id: str) -> None:
        self._seen[brief_id] = datetime.now(timezone.utc).isoformat()

    def save(self) -> None:
        ensure_data_dir()
        SEEN_FILE.write_text(json.dumps(self._seen, indent=2), encoding="utf-8")
