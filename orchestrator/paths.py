"""Central path definitions for the orchestrator.

Every other module imports paths from here so there is a single source of
truth for where the database, config, logs, and modules live.
"""

from __future__ import annotations

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
