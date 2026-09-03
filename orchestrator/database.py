"""Shared SQLite database for all modules.

This is the single local store every income program writes to. It holds:

* ``modules``       – registry of the five programs (name, description, enabled)
* ``activity_log``  – append-only stream of events each module emits
* ``status``        – latest heartbeat/state per module (one row per module)
* ``earnings``      – money events (each program logs what it earns)
* ``review_queue``  – items a module flags for your manual approval

Everything uses stdlib ``sqlite3`` — no external dependencies. WAL mode is
enabled so the dashboard can read while modules write concurrently.
"""

from __future__ import annotations

import json
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from typing import Any, Iterator

from .paths import DB_PATH, MODULES, ensure_data_dir

SCHEMA = """
CREATE TABLE IF NOT EXISTS modules (
    name         TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    description  TEXT NOT NULL DEFAULT '',
    enabled      INTEGER NOT NULL DEFAULT 1,
    created_at   TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS activity_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    module     TEXT NOT NULL,
    level      TEXT NOT NULL DEFAULT 'info',   -- debug|info|warning|error
    event      TEXT NOT NULL DEFAULT '',       -- short machine-ish label
    message    TEXT NOT NULL DEFAULT '',
    metadata   TEXT,                            -- JSON blob, optional
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_activity_module_time
    ON activity_log (module, created_at DESC);

CREATE TABLE IF NOT EXISTS status (
    module     TEXT PRIMARY KEY,
    state      TEXT NOT NULL DEFAULT 'idle',   -- idle|running|ok|warning|error
    detail     TEXT NOT NULL DEFAULT '',
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS earnings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    module      TEXT NOT NULL,
    amount      REAL NOT NULL,
    currency    TEXT NOT NULL DEFAULT 'USD',
    description TEXT NOT NULL DEFAULT '',
    source      TEXT NOT NULL DEFAULT '',
    occurred_at TEXT NOT NULL,
    created_at  TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_earnings_module ON earnings (module);

CREATE TABLE IF NOT EXISTS review_queue (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    module       TEXT NOT NULL,
    title        TEXT NOT NULL,
    description  TEXT NOT NULL DEFAULT '',
    payload      TEXT,                          -- JSON blob the module attaches
    status       TEXT NOT NULL DEFAULT 'pending', -- pending|approved|rejected
    created_at   TEXT NOT NULL,
    resolved_at  TEXT,
    resolution_note TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_review_status ON review_queue (status, created_at DESC);
"""


def utcnow() -> str:
    """ISO-8601 UTC timestamp string used consistently across all tables."""
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


@contextmanager
def get_connection() -> Iterator[sqlite3.Connection]:
    """Yield a configured connection, committing on success and closing always."""
    ensure_data_dir()
    conn = sqlite3.connect(DB_PATH, timeout=30)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA foreign_keys=ON;")
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def init_db() -> None:
    """Create tables (idempotent) and seed the five module rows."""
    with get_connection() as conn:
        conn.executescript(SCHEMA)
        now = utcnow()
        for name, display in MODULES.items():
            conn.execute(
                """INSERT INTO modules (name, display_name, description, enabled, created_at)
                   VALUES (?, ?, '', 1, ?)
                   ON CONFLICT(name) DO UPDATE SET display_name=excluded.display_name""",
                (name, display, now),
            )
            conn.execute(
                """INSERT INTO status (module, state, detail, updated_at)
                   VALUES (?, 'idle', 'Not started yet', ?)
                   ON CONFLICT(module) DO NOTHING""",
                (name, now),
            )


# --------------------------------------------------------------------------- #
#  Write helpers (used by orchestrator.logger; safe to call directly too)
# --------------------------------------------------------------------------- #
def record_activity(
    module: str,
    message: str,
    *,
    level: str = "info",
    event: str = "",
    metadata: dict[str, Any] | None = None,
) -> int:
    with get_connection() as conn:
        cur = conn.execute(
            """INSERT INTO activity_log (module, level, event, message, metadata, created_at)
               VALUES (?, ?, ?, ?, ?, ?)""",
            (
                module,
                level,
                event,
                message,
                json.dumps(metadata) if metadata else None,
                utcnow(),
            ),
        )
        return int(cur.lastrowid)


def set_status(module: str, state: str, detail: str = "") -> None:
    with get_connection() as conn:
        conn.execute(
            """INSERT INTO status (module, state, detail, updated_at)
               VALUES (?, ?, ?, ?)
               ON CONFLICT(module) DO UPDATE SET
                 state=excluded.state, detail=excluded.detail,
                 updated_at=excluded.updated_at""",
            (module, state, detail, utcnow()),
        )


def record_earning(
    module: str,
    amount: float,
    *,
    currency: str = "USD",
    description: str = "",
    source: str = "",
    occurred_at: str | None = None,
) -> int:
    with get_connection() as conn:
        cur = conn.execute(
            """INSERT INTO earnings
               (module, amount, currency, description, source, occurred_at, created_at)
               VALUES (?, ?, ?, ?, ?, ?, ?)""",
            (
                module,
                float(amount),
                currency,
                description,
                source,
                occurred_at or utcnow(),
                utcnow(),
            ),
        )
        return int(cur.lastrowid)


def add_review_item(
    module: str,
    title: str,
    *,
    description: str = "",
    payload: dict[str, Any] | None = None,
) -> int:
    with get_connection() as conn:
        cur = conn.execute(
            """INSERT INTO review_queue (module, title, description, payload, status, created_at)
               VALUES (?, ?, ?, ?, 'pending', ?)""",
            (
                module,
                title,
                description,
                json.dumps(payload) if payload else None,
                utcnow(),
            ),
        )
        return int(cur.lastrowid)


def resolve_review_item(item_id: int, decision: str, note: str = "") -> bool:
    """Mark a review item approved/rejected. Returns True if a row changed."""
    if decision not in ("approved", "rejected"):
        raise ValueError("decision must be 'approved' or 'rejected'")
    with get_connection() as conn:
        cur = conn.execute(
            """UPDATE review_queue
               SET status=?, resolved_at=?, resolution_note=?
               WHERE id=? AND status='pending'""",
            (decision, utcnow(), note, item_id),
        )
        return cur.rowcount > 0


# --------------------------------------------------------------------------- #
#  Read helpers (used by the dashboard)
# --------------------------------------------------------------------------- #
def module_overview() -> list[dict[str, Any]]:
    """One row per module with status, last activity, totals, pending count."""
    with get_connection() as conn:
        rows = conn.execute(
            """
            SELECT
              m.name, m.display_name, m.enabled,
              s.state, s.detail, s.updated_at,
              (SELECT message FROM activity_log a
                 WHERE a.module = m.name ORDER BY a.id DESC LIMIT 1) AS last_activity,
              (SELECT created_at FROM activity_log a
                 WHERE a.module = m.name ORDER BY a.id DESC LIMIT 1) AS last_activity_at,
              COALESCE((SELECT SUM(amount) FROM earnings e
                 WHERE e.module = m.name), 0) AS total_earnings,
              (SELECT COUNT(*) FROM review_queue r
                 WHERE r.module = m.name AND r.status='pending') AS pending_reviews
            FROM modules m
            LEFT JOIN status s ON s.module = m.name
            ORDER BY m.name
            """
        ).fetchall()
        return [dict(r) for r in rows]


def recent_activity(limit: int = 25) -> list[dict[str, Any]]:
    with get_connection() as conn:
        rows = conn.execute(
            "SELECT * FROM activity_log ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()
        return [dict(r) for r in rows]


def pending_reviews() -> list[dict[str, Any]]:
    with get_connection() as conn:
        rows = conn.execute(
            "SELECT * FROM review_queue WHERE status='pending' ORDER BY created_at DESC"
        ).fetchall()
        return [dict(r) for r in rows]


def resolved_reviews(limit: int = 10) -> list[dict[str, Any]]:
    """The most recently approved/rejected review items, newest first.

    An audit trail for past decisions — `pending_reviews` drops an item the
    moment it's resolved, so without this there's no way to see what you
    approved or rejected, or when.
    """
    with get_connection() as conn:
        rows = conn.execute(
            """SELECT * FROM review_queue
               WHERE status IN ('approved', 'rejected')
               ORDER BY resolved_at DESC, id DESC
               LIMIT ?""",
            (limit,),
        ).fetchall()
        return [dict(r) for r in rows]


REVIEWS_CSV_FIELDS = [
    "resolved_at",
    "module",
    "title",
    "status",
    "resolution_note",
    "created_at",
]


def list_resolved_reviews(
    *, module: str | None = None, since: str | None = None
) -> list[dict[str, Any]]:
    """Every approved/rejected review item, oldest first — the full audit log.

    Unlike `resolved_reviews()` (capped at `limit`, for the dashboard's
    "Recently resolved" panel), this returns everything, for a CSV export or
    any other report that needs the complete decision history rather than
    just the last few. `since` filters on `resolved_at`.
    """
    query = "SELECT * FROM review_queue WHERE status IN ('approved', 'rejected')"
    params: list[Any] = []
    if module is not None:
        query += " AND module = ?"
        params.append(module)
    if since is not None:
        query += " AND resolved_at >= ?"
        params.append(since)
    query += " ORDER BY resolved_at ASC, id ASC"
    with get_connection() as conn:
        rows = conn.execute(query, params).fetchall()
        return [dict(r) for r in rows]


EARNINGS_CSV_FIELDS = [
    "occurred_at",
    "module",
    "amount",
    "currency",
    "source",
    "description",
]


def list_earnings(
    *, module: str | None = None, since: str | None = None
) -> list[dict[str, Any]]:
    """Individual earning rows, oldest first — the raw ledger for export.

    `since` filters on `occurred_at` (an ISO-8601 date or timestamp string;
    lexical comparison, so any prefix of the stored format works, e.g. just
    a date). `totals()`/`module_overview()` only ever give sums; this is
    what a CSV export or any other row-by-row report needs instead.
    """
    query = "SELECT * FROM earnings WHERE 1=1"
    params: list[Any] = []
    if module is not None:
        query += " AND module = ?"
        params.append(module)
    if since is not None:
        query += " AND occurred_at >= ?"
        params.append(since)
    query += " ORDER BY occurred_at ASC, id ASC"
    with get_connection() as conn:
        rows = conn.execute(query, params).fetchall()
        return [dict(r) for r in rows]


def totals() -> dict[str, Any]:
    with get_connection() as conn:
        earn = conn.execute(
            "SELECT COALESCE(SUM(amount),0) AS t FROM earnings"
        ).fetchone()["t"]
        pend = conn.execute(
            "SELECT COUNT(*) AS c FROM review_queue WHERE status='pending'"
        ).fetchone()["c"]
        return {"total_earnings": earn, "pending_reviews": pend}


if __name__ == "__main__":
    init_db()
    print(f"Initialised database at {DB_PATH}")
