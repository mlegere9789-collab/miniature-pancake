"""Central path definitions for the orchestrator.

Every other module imports paths from here so there is a single source of
truth for where the database, config, logs, and modules live.
"""

from __future__ import annotations

import os
from pathlib import Path

# The project root is the parent of the `orchestrator/` package directory.
PROJECT_ROOT: Path = Path(__file__).resolve().parent.parent

# Where the shared SQLite database and any other runtime data lives.
# This directory is git-ignored so local data / secrets never get committed.
DATA_DIR: Path = PROJECT_ROOT / "data"
DB_PATH: Path = DATA_DIR / "orchestrator.db"

# Secrets file (git-ignored). Copy `.env.example` -> `.env` and fill it in.
ENV_PATH: Path = PROJECT_ROOT / ".env"

# Scheduler job definitions. Copy the example to `jobs.json` to customise.
JOBS_PATH: Path = PROJECT_ROOT / "orchestrator" / "jobs.json"
JOBS_EXAMPLE_PATH: Path = PROJECT_ROOT / "orchestrator" / "jobs.example.json"

# Where each income program lives.
MODULES_DIR: Path = PROJECT_ROOT / "modules"

# Canonical list of the five income programs (folder name -> display name).
MODULES: dict[str, str] = {
    "stock_licensing": "Stock Asset Licensing",
    "ecommerce_dropshipping": "E-commerce / Dropshipping",
    "deal_alert_bot": "Deal-Alert Bot",
    "digital_products": "Digital Product Creation",
    "micro_saas": "Micro-SaaS Tool",
}


def ensure_data_dir() -> Path:
    """Create the data directory if it does not exist and return it."""
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    return DATA_DIR


def atomic_write_text(path: Path, text: str) -> None:
    """Write `text` to `path` without ever leaving a truncated/partial file
    behind if the process dies mid-write.

    A plain `path.write_text(...)` opens the file in truncate mode, so a
    crash (SIGKILL, OOM, the host going down) between the truncate and the
    new content landing leaves invalid JSON on disk. Every module's own
    `*_seen.json` dedup store already tolerates a *corrupt* file (its
    `_load()` catches `JSONDecodeError` and falls back to "nothing seen
    yet") -- but that fallback is exactly the failure mode a truncated
    write would trigger, silently forgetting every deal already alerted on,
    every order already processed, every brief already drafted, and redoing
    (or re-flagging, or re-posting) all of it on the next run.

    Writes to a temp file in the same directory, then `os.replace()`s it
    into place -- atomic on both POSIX and Windows, since the replacement
    is either fully visible or not there at all, never partial.
    """
    tmp_path = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    tmp_path.write_text(text, encoding="utf-8")
    os.replace(tmp_path, path)
