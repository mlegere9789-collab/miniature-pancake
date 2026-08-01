"""Remember which deals we've already alerted on, so we don't repost them.

State lives in ``data/deal_alert_seen.json`` (git-ignored, alongside the other
runtime data). CheapShark issues a new ``dealID`` when a game's price changes,
so keying on ``dealID`` means a genuinely better price later shows up as a new
alert, while an unchanged deal stays silent.

Old entries are pruned so the file cannot grow forever.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

from orchestrator.paths import DATA_DIR, ensure_data_dir

SEEN_FILE = DATA_DIR / "deal_alert_seen.json"
DEFAULT_TTL_DAYS = 30


class SeenStore:
    def __init__(self, ttl_days: int = DEFAULT_TTL_DAYS) -> None:
        self.ttl_days = ttl_days
        self._seen: dict[str, str] = {}
        self._load()

    def _load(self) -> None:
        if SEEN_FILE.exists():
            try:
                self._seen = json.loads(SEEN_FILE.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, OSError):
                self._seen = {}

    def is_seen(self, deal_id: str) -> bool:
        return deal_id in self._seen

    def mark(self, deal_id: str) -> None:
        self._seen[deal_id] = datetime.now(timezone.utc).isoformat()

    def prune(self) -> None:
        cutoff = datetime.now(timezone.utc).timestamp() - self.ttl_days * 86400
        keep: dict[str, str] = {}
        for deal_id, ts in self._seen.items():
            try:
                when = datetime.fromisoformat(ts).timestamp()
            except ValueError:
                continue
            if when >= cutoff:
                keep[deal_id] = ts
        self._seen = keep

    def save(self) -> None:
        ensure_data_dir()
        self.prune()
        SEEN_FILE.write_text(json.dumps(self._seen, indent=2), encoding="utf-8")
