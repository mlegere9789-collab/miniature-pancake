"""Local supplier cost book, used to compute net margin per order.

Shopify has no notion of "what your supplier charges you" — that lives
wherever you source products from, which varies per store. Rather than
guess at a specific supplier's API, costs are read from a small git-ignored
JSON file you maintain yourself:

    data/product_costs.json
    {
      "SKU-RED-M": 4.50,
      "SKU-BLUE-L": 5.10
    }

A SKU missing from this file is not treated as free — ``cost_for`` returns
``None`` and the caller flags the order for review instead of trusting a
fabricated number.
"""

from __future__ import annotations

import json
from pathlib import Path

from orchestrator.paths import DATA_DIR

COSTS_FILE = DATA_DIR / "product_costs.json"


class CostBook:
    def __init__(self, costs: dict[str, float]) -> None:
        self._costs = costs

    @classmethod
    def load(cls, path: Path = COSTS_FILE) -> "CostBook":
        if not path.exists():
            return cls({})
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            return cls({})
        if not isinstance(raw, dict):
            return cls({})
        costs: dict[str, float] = {}
        for sku, value in raw.items():
            try:
                costs[str(sku)] = float(value)
            except (TypeError, ValueError):
                continue
        return cls(costs)

    def cost_for(self, sku: str | None) -> float | None:
        if not sku:
            return None
        return self._costs.get(sku)
