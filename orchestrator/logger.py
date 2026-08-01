"""The logging interface every module uses.

A module gets a logger scoped to its own name and uses it to record
activity, heartbeat status, earnings, and items needing your approval —
all of which land in the shared SQLite database and show up on the dashboard.

Example (from any script under ``modules/``)::

    from orchestrator import get_logger

    log = get_logger("deal_alert_bot")

    log.status("running", "Scanning eBay + Amazon for price drops")
    log.activity("Scanned 214 listings, found 3 candidate deals")

    log.earning(4.20, source="amazon", description="Affiliate commission")

    log.flag_for_review(
        "Post this deal to the channel?",
        description="Sony WH-1000XM5 — 38% off",
        payload={"url": "https://...", "price": 248.00},
    )

    log.info("done for this run")   # also mirrors to console
"""

from __future__ import annotations

import logging
from typing import Any

from . import database as db
from .paths import MODULES

# Console logging so you also see output when running a module by hand.
_console = logging.getLogger("orchestrator")
if not _console.handlers:
    _handler = logging.StreamHandler()
    _handler.setFormatter(logging.Formatter("%(asctime)s [%(name)s] %(message)s"))
    _console.addHandler(_handler)
    _console.setLevel(logging.INFO)


class ModuleLogger:
    """A logging handle scoped to a single module."""

    def __init__(self, module: str) -> None:
        if module not in MODULES:
            known = ", ".join(MODULES)
            raise ValueError(f"Unknown module '{module}'. Expected one of: {known}")
        self.module = module
        self._log = _console.getChild(module)

    # -- activity stream ---------------------------------------------------- #
    def activity(
        self,
        message: str,
        *,
        level: str = "info",
        event: str = "",
        metadata: dict[str, Any] | None = None,
    ) -> int:
        getattr(
            self._log,
            level if level in ("debug", "info", "warning", "error") else "info",
        )(message)
        return db.record_activity(
            self.module, message, level=level, event=event, metadata=metadata
        )

    # Convenience level shortcuts.
    def debug(self, message: str, **kw: Any) -> int:
        return self.activity(message, level="debug", **kw)

    def info(self, message: str, **kw: Any) -> int:
        return self.activity(message, level="info", **kw)

    def warning(self, message: str, **kw: Any) -> int:
        return self.activity(message, level="warning", **kw)

    def error(self, message: str, **kw: Any) -> int:
        return self.activity(message, level="error", **kw)

    # -- heartbeat / status ------------------------------------------------- #
    def status(self, state: str, detail: str = "") -> None:
        """Set this module's current state (idle|running|ok|warning|error)."""
        db.set_status(self.module, state, detail)

    # -- money -------------------------------------------------------------- #
    def earning(
        self,
        amount: float,
        *,
        currency: str = "USD",
        description: str = "",
        source: str = "",
        occurred_at: str | None = None,
    ) -> int:
        rid = db.record_earning(
            self.module,
            amount,
            currency=currency,
            description=description,
            source=source,
            occurred_at=occurred_at,
        )
        self._log.info("earning +%.2f %s (%s)", amount, currency, source or description)
        return rid

    # -- manual approval ---------------------------------------------------- #
    def flag_for_review(
        self,
        title: str,
        *,
        description: str = "",
        payload: dict[str, Any] | None = None,
    ) -> int:
        """Queue something for your manual approval on the dashboard."""
        rid = db.add_review_item(
            self.module, title, description=description, payload=payload
        )
        self._log.info("flagged for review (#%d): %s", rid, title)
        return rid


def get_logger(module: str) -> ModuleLogger:
    """Return a :class:`ModuleLogger` bound to *module*."""
    return ModuleLogger(module)
