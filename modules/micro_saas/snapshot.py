"""Remember the last run's active-subscription ids and when it ran.

State lives in ``data/micro_saas_snapshot.json`` (git-ignored, alongside the
other runtime data). Comparing this run's active-subscription ids against
the last snapshot is how churn and new signups get detected; the timestamp
is how "charges since last run" gets bounded.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

from orchestrator.paths import DATA_DIR, atomic_write_text, ensure_data_dir

SNAPSHOT_FILE = DATA_DIR / "micro_saas_snapshot.json"

# Distinguishes "caller didn't pass run_at, default to now" from "caller
# explicitly passed None" -- save()'s own docstring explains why that
# distinction is load-bearing.
_UNSET = object()


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

    def save(self, active_ids: set[str], *, run_at: str | None = _UNSET) -> None:  # type: ignore[assignment]
        """Persist this run's active-subscription ids, and `run_at` (default:
        now).

        Pass the *previous* `run_at` back in (`self.previous_run_at`) when
        the charges fetch for this window failed, rather than letting it
        default to now — advancing it anyway would silently mark that
        window as reconciled, and the next run's `previous_run_unix()`
        would never look at it again. Subscription ids still get saved
        either way: they reflect the current, successfully-fetched
        subscription list regardless of whether the *charges* fetch
        succeeded, and are needed for correct new/churn diffing next run.

        `run_at` genuinely omitted (the ordinary, successful-run case)
        defaults to now — but `self.previous_run_at` on the very first run
        ever (nothing has been reconciled yet) is itself `None`, and that
        `None` must round-trip back out as `None`, not silently collapse to
        "now" the way a plain `run_at or now()` would: a first-ever run
        whose charges fetch fails would otherwise still advance run_at,
        permanently losing whatever window preceded it even though nothing
        was ever actually reconciled. `_UNSET` (never a value a caller would
        legitimately pass) is what makes "not given" and "given as None"
        distinguishable here.
        """
        ensure_data_dir()
        if run_at is _UNSET:
            run_at = datetime.now(timezone.utc).isoformat()
        atomic_write_text(
            SNAPSHOT_FILE,
            json.dumps(
                {
                    "active_sub_ids": sorted(active_ids),
                    "run_at": run_at,
                },
                indent=2,
            ),
        )
