"""Income Orchestrator — shared foundation for the five income programs.

Public API most modules need::

    from orchestrator import get_logger, config, init_db

    log = get_logger("micro_saas")
    log.status("running")
"""

from __future__ import annotations

from .config import Config, MissingCredentialError, config
from .database import init_db
from .logger import ModuleLogger, get_logger
from .paths import MODULES, PROJECT_ROOT

__all__ = [
    "config",
    "Config",
    "MissingCredentialError",
    "init_db",
    "get_logger",
    "ModuleLogger",
    "MODULES",
    "PROJECT_ROOT",
]
