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

from orchestrator.paths import DATA_DIR, atomic_write_text, ensure_data_dir

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

    def is_seen(self, asset_id: object) -> bool:
        # str(), not a bare `in` -- an asset id from stock_assets.json can
        # be a JSON number (nothing enforces it must be a string), and
        # json.dumps() in save() silently coerces a non-string dict key to
        # a string on write. Without coercing here too, is_seen(1) after a
        # reload would check `1 in {"1": ...}`, which is always False --
        # dedup would silently never stick and every run would re-draft
        # the same asset's keyword metadata forever.
        return str(asset_id) in self._seen

    def mark(self, asset_id: object) -> None:
        self._seen[str(asset_id)] = datetime.now(timezone.utc).isoformat()

    def save(self) -> None:
        ensure_data_dir()
        atomic_write_text(SEEN_FILE, json.dumps(self._seen, indent=2))
