"""Secure configuration & credentials loading.

Design goals
------------
* **One place for secrets.** All API keys live in a single git-ignored `.env`
  file at the project root. Nothing is ever hardcoded into module scripts.
* **Zero dependencies.** We parse `.env` ourselves so you don't need
  `python-dotenv` installed.
* **Fail loudly, helpfully.** `require()` raises a clear error naming the
  missing key and pointing you at `.env.example`, instead of a module blowing
  up deep inside an API call with a confusing "invalid auth" message.
* **OS environment wins.** A real environment variable (e.g. one injected by a
  server or CI secret store) overrides the value in `.env`, so the same code
  works locally and in production without changes.

Usage
-----
    from orchestrator.config import config

    key = config.require("ANTHROPIC_API_KEY")   # raises if missing
    shop = config.get("SHOPIFY_STORE_URL", "")   # returns "" if missing
"""

from __future__ import annotations

import os
import stat
from pathlib import Path

from .paths import ENV_PATH


class MissingCredentialError(RuntimeError):
    """Raised when a required credential is not configured."""


def _parse_env_file(path: Path) -> dict[str, str]:
    """Minimal, dependency-free `.env` parser.

    Supports `KEY=value`, `export KEY=value`, `#` comments, blank lines, and
    single/double quoted values. Values are NOT interpolated (no `$VAR`),
    which keeps the semantics simple and predictable for secrets.
    """
    values: dict[str, str] = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export ") :].strip()
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip()
        # Strip matching surrounding quotes.
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        if key:
            values[key] = value
    return values


class Config:
    """Lazy, cached view over `.env` + the process environment."""

    def __init__(self, env_path: Path = ENV_PATH) -> None:
        self._env_path = env_path
        self._file_values: dict[str, str] | None = None

    def _load(self) -> dict[str, str]:
        if self._file_values is None:
            self._file_values = _parse_env_file(self._env_path)
            self._warn_if_world_readable()
        return self._file_values

    def _warn_if_world_readable(self) -> None:
        """Nudge the user if their secrets file is readable by others."""
        try:
            mode = self._env_path.stat().st_mode
        except FileNotFoundError:
            return
        if mode & (stat.S_IRWXG | stat.S_IRWXO):
            print(
                f"[config] WARNING: {self._env_path} is accessible to other "
                "users. Run: chmod 600 .env"
            )

    def reload(self) -> None:
        """Forget cached file values (call after editing `.env` at runtime)."""
        self._file_values = None

    def get(self, key: str, default: str | None = None) -> str | None:
        """Return a config value. Real env vars take precedence over `.env`."""
        if key in os.environ:
            return os.environ[key]
        return self._load().get(key, default)

    def require(self, key: str) -> str:
        """Return a config value or raise a helpful error if it is missing."""
        value = self.get(key)
        if value is None or value == "":
            raise MissingCredentialError(
                f"Required credential '{key}' is not set.\n"
                f"  1. Copy .env.example to .env  (cp .env.example .env)\n"
                f"  2. Add a line:  {key}=your-value-here\n"
                f"  3. Keep .env private (chmod 600 .env) — it is git-ignored."
            )
        return value

    def has(self, key: str) -> bool:
        value = self.get(key)
        return value is not None and value != ""

    def as_dict(self, *, redacted: bool = True) -> dict[str, str]:
        """Return all known config values, redacted by default (for dashboards)."""
        merged = dict(self._load())
        for k, v in os.environ.items():
            if k in merged:  # only surface keys we actually track via .env
                merged[k] = v
        if redacted:
            return {k: _redact(v) for k, v in merged.items()}
        return merged


def _redact(value: str) -> str:
    if not value:
        return ""
    if len(value) <= 8:
        return "*" * len(value)
    return f"{value[:4]}…{value[-2:]}"


# Module-level singleton — import this everywhere.
config = Config()
