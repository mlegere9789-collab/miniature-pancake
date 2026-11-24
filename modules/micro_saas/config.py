"""Micro-SaaS Tool settings.

Everything is configurable via `.env` (loaded through the orchestrator's
`config`), with sensible defaults. No secrets or tunables are hardcoded into
the logic.

Relevant `.env` keys (all optional):

    STRIPE_SECRET_KEY               Enables billing reconciliation (MRR,
                                    churn, charge summary). Unset = that part
                                    of the run is skipped.
    SAAS_HEALTH_URL                 Enables the health check. Unset = that
                                    part of the run is skipped.
    MICRO_SAAS_HEALTH_TIMEOUT       Seconds before the health check times out
                                    (default 10).
    MICRO_SAAS_SUB_LIMIT            Max active subscriptions fetched per run
                                    (default 100).
    MICRO_SAAS_CHARGE_LIMIT         Max charges fetched per run (default 100).
    MICRO_SAAS_LOOKBACK_HOURS       On the first-ever run (no prior
                                    snapshot), how far back to look for
                                    charges (default 24).
    MICRO_SAAS_HIGH_CHURN_THRESHOLD Churned-subscription count at/above which
                                    a run gets flagged for review (default 3).

Neither Stripe nor a health URL configured -> the module reports idle and
does nothing, rather than erroring.
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
    stripe_secret_key: str | None
    health_url: str | None
    health_timeout: int
    sub_limit: int
    charge_limit: int
    lookback_hours: int
    high_churn_threshold: int

    @property
    def billing_configured(self) -> bool:
        return bool(self.stripe_secret_key)

    @property
    def health_configured(self) -> bool:
        return bool(self.health_url)

    @classmethod
    def load(cls) -> "Settings":
        return cls(
            stripe_secret_key=config.get("STRIPE_SECRET_KEY") or None,
            health_url=config.get("SAAS_HEALTH_URL") or None,
            health_timeout=max(1, _get_int("MICRO_SAAS_HEALTH_TIMEOUT", 10)),
            sub_limit=max(1, _get_int("MICRO_SAAS_SUB_LIMIT", 100)),
            charge_limit=max(1, _get_int("MICRO_SAAS_CHARGE_LIMIT", 100)),
            lookback_hours=max(1, _get_int("MICRO_SAAS_LOOKBACK_HOURS", 24)),
            high_churn_threshold=max(1, _get_int("MICRO_SAAS_HIGH_CHURN_THRESHOLD", 3)),
        )
