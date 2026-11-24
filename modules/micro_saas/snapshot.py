"""Remember the last run's active-subscription ids and when it ran.

State lives in ``data/micro_saas_snapshot.json`` (git-ignored, alongside the
other runtime data). Comparing this run's active-subscription ids against
the last snapshot is how churn and new signups get detected; the timestamp
is how "charges since last run" gets bounded.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

from orchestrator.paths import DATA_DIR, ensure_data_dir

SNAPSHOT_FILE = DATA_DIR / "micro_saas_snapshot.json"


class SnapshotStore:
    def __init__(self) -> None:
        self.previous_ids: set[str] = set()
        self.previous_run_at: str | None = None
        self._load()

    def _load(self) -> None:
        if not SNAPSHOT_FILE.exists():
            return
        try:
            raw = json.loads(SNAPSHOT_FILE.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            return
        if not isinstance(raw, dict):
            return
        self.previous_ids = set(raw.get("active_sub_ids") or [])
        self.previous_run_at = raw.get("run_at")

    def previous_run_unix(self, default_lookback_hours: int) -> int:
        """Seconds since epoch of the last run, or `default_lookback_hours` ago."""
        now = datetime.now(timezone.utc)
        if self.previous_run_at:
            try:
                when = datetime.fromisoformat(self.previous_run_at)
                return int(when.timestamp())
            except ValueError:
                pass
        fallback = now.timestamp() - default_lookback_hours * 3600
        return int(fallback)

    def save(self, active_ids: set[str]) -> None:
        ensure_data_dir()
        SNAPSHOT_FILE.write_text(
            json.dumps(
                {
                    "active_sub_ids": sorted(active_ids),
                    "run_at": datetime.now(timezone.utc).isoformat(),
                },
                indent=2,
            ),
            encoding="utf-8",
        )
