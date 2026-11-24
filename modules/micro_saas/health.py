"""A single HTTP health check against the live service (stdlib only).

Deliberately minimal: one GET, a timeout, and a 2xx/3xx check. Anything more
specific (a JSON `{"status": "ok"}` body contract, auth, multiple endpoints)
is application-specific and belongs to whoever built the SaaS, not to a
generic orchestrator module.
"""

from __future__ import annotations

import time
import urllib.error
import urllib.request
from dataclasses import dataclass

USER_AGENT = "income-orchestrator-micro-saas/1.0 (+local)"
DEFAULT_TIMEOUT = 10


@dataclass
class HealthResult:
    ok: bool
    detail: str
    latency_ms: float


def check_health(url: str, *, timeout: int = DEFAULT_TIMEOUT) -> HealthResult:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    start = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            latency_ms = (time.monotonic() - start) * 1000
            if 200 <= resp.status < 400:
                return HealthResult(True, f"HTTP {resp.status}", latency_ms)
            return HealthResult(False, f"HTTP {resp.status}", latency_ms)
    except urllib.error.HTTPError as exc:
        latency_ms = (time.monotonic() - start) * 1000
        return HealthResult(False, f"HTTP {exc.code}", latency_ms)
    except urllib.error.URLError as exc:
        latency_ms = (time.monotonic() - start) * 1000
        return HealthResult(False, f"Network error: {exc.reason}", latency_ms)
    except TimeoutError:
        latency_ms = (time.monotonic() - start) * 1000
        return HealthResult(False, f"Timed out after {timeout}s", latency_ms)
