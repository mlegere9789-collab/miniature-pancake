"""Remember which orders we've already processed, so a rerun doesn't double
count margin or flag the same order twice.

State lives in ``data/ecommerce_seen.json`` (git-ignored, alongside the other
runtime data), keyed by Shopify order id. Entries are pruned after
``DEFAULT_TTL_DAYS`` so the file cannot grow forever — by then the order has
long since shipped or been handled, so forgetting it is safe.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

from orchestrator.paths import DATA_DIR, ensure_data_dir

SEEN_FILE = DATA_DIR / "ecommerce_seen.json"
DEFAULT_TTL_DAYS = 90


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

    def is_seen(self, order_id: str) -> bool:
        return order_id in self._seen

    def mark(self, order_id: str) -> None:
        self._seen[order_id] = datetime.now(timezone.utc).isoformat()

    def prune(self) -> None:
        cutoff = datetime.now(timezone.utc).timestamp() - self.ttl_days * 86400
        keep: dict[str, str] = {}
        for order_id, ts in self._seen.items():
            try:
                when = datetime.fromisoformat(ts).timestamp()
            except ValueError:
                continue
            if when >= cutoff:
                keep[order_id] = ts
        self._seen = keep

    def save(self) -> None:
        ensure_data_dir()
        self.prune()
        SEEN_FILE.write_text(json.dumps(self._seen, indent=2), encoding="utf-8")
