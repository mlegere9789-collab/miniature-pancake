"""Digital Product Creation settings.

Everything is configurable via `.env` (loaded through the orchestrator's
`config`), with sensible defaults. No secrets or tunables are hardcoded into
the logic.

Relevant `.env` keys (all optional except the API key):

    ANTHROPIC_API_KEY              Enables copy drafting. Unset = the module
                                   stays idle rather than erroring.
    DIGITAL_PRODUCTS_MODEL         Claude model used to draft copy
                                   (default "claude-3-5-haiku-20241022" — a
                                   small, fast model; copywriting doesn't
                                   need a frontier one).
    DIGITAL_PRODUCTS_MAX_TOKENS    Max tokens per draft (default 700).
    DIGITAL_PRODUCTS_MAX_PER_RUN   Max briefs drafted per run (default 5, so
                                   a large backlog doesn't burn a huge amount
                                   of API spend in one run).
"""

from __future__ import annotations

from dataclasses import dataclass

from orchestrator import config


def _get_int(key: str, default: int) -> int:
    raw = config.get(key)
    if raw is None or raw.strip() == "":
        return default
    try:
        return int(raw)
    except ValueError:
        return default


@dataclass
class Settings:
    anthropic_api_key: str | None
    model: str
    max_tokens: int
    max_per_run: int

    @property
    def configured(self) -> bool:
        return bool(self.anthropic_api_key)

    @classmethod
    def load(cls) -> "Settings":
        return cls(
            anthropic_api_key=config.get("ANTHROPIC_API_KEY") or None,
            model=config.get("DIGITAL_PRODUCTS_MODEL", "claude-3-5-haiku-20241022")
            or "claude-3-5-haiku-20241022",
            max_tokens=max(100, _get_int("DIGITAL_PRODUCTS_MAX_TOKENS", 700)),
            max_per_run=max(1, _get_int("DIGITAL_PRODUCTS_MAX_PER_RUN", 5)),
        )
