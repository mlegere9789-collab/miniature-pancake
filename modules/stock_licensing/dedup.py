"""Remember which assets we've already drafted keyword metadata for.

State lives in ``data/stock_licensing_seen.json`` (git-ignored). Keyed on an
asset's own ``id``, not a generated one, so editing an asset's other fields
and rerunning does NOT trigger a redraft — only a new (or explicitly
removed-then-reused) id does.

Unlike the time-bounded dedup stores elsewhere in this project, entries here
are not pruned by time: an asset id is meant to be a stable, permanent
identifier for one file, so "forgetting" it after a TTL would just cause an
unwanted, costly redraft.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

from orchestrator.paths import DATA_DIR, ensure_data_dir

SEEN_FILE = DATA_DIR / "stock_licensing_seen.json"


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

    def is_seen(self, asset_id: str) -> bool:
        return asset_id in self._seen

    def mark(self, asset_id: str) -> None:
        self._seen[asset_id] = datetime.now(timezone.utc).isoformat()

    def save(self) -> None:
        ensure_data_dir()
        SEEN_FILE.write_text(json.dumps(self._seen, indent=2), encoding="utf-8")
