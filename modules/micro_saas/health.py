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
    start = time.monotonic()
    try:
        # Request(...) construction itself can raise -- a SAAS_HEALTH_URL
        # with no scheme (a pasted-in-a-hurry "myapp.example.com/health"
        # missing its "https://") makes it raise a bare
        # ValueError("unknown url type"), not one of the exceptions below.
        # This function's whole contract (see its own module docstring and
        # every other branch here) is that it always returns a
        # HealthResult and never raises -- run.py calls it with no
        # try/except of its own, unlike the billing half of the same
        # run(), so an uncaught exception here would abort not just the
        # health check but the billing reconciliation that runs after it
        # too, on every single run until the URL is fixed.
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
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
    except ValueError as exc:
        latency_ms = (time.monotonic() - start) * 1000
        return HealthResult(False, f"Malformed health URL: {exc}", latency_ms)
