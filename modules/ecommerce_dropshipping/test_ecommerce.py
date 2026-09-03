"""Offline unit tests for the E-commerce / Dropshipping module.

No network required — all Shopify responses are faked. Run with:
    python -m unittest modules.ecommerce_dropshipping.test_ecommerce
or via the whole suite:
    python -m unittest discover -s modules -p "test_*.py"
"""

from __future__ import annotations

import unittest

from .config import Settings
from .costs import CostBook
from .formatter import format_order_summary, format_review_description
from .pricing import (
    compute_margin,
    has_high_quantity,
    has_incomplete_address,
    has_refund,
)
from .shopify_client import normalize_shop_domain

GOOD_ADDRESS = {
    "address1": "123 Main St",
    "city": "Springfield",
    "zip": "12345",
    "country": "US",
}

SAMPLE_ORDER = {
    "id": 991,
    "name": "#1001",
    "total_price": "39.98",
    "currency": "USD",
    "shipping_address": GOOD_ADDRESS,
    "line_items": [
        {"sku": "SKU-A", "quantity": 2, "price": "19.99", "requires_shipping": True},
    ],
    "refunds": [],
}


def make_settings(**overrides) -> Settings:
    base = dict(
        store_url="my-shop.myshopify.com",
        admin_token="tok",
        api_version="2024-01",
        max_orders_per_run=25,
        high_quantity_threshold=10,
        min_margin_to_auto_log=0.0,
    )
    base.update(overrides)
    return Settings(**base)


class TestSettings(unittest.TestCase):
    def test_configured_requires_both_fields(self):
        self.assertTrue(make_settings().configured)
        self.assertFalse(make_settings(store_url=None).configured)
        self.assertFalse(make_settings(admin_token=None).configured)


class TestNormalizeShopDomain(unittest.TestCase):
    def test_bare_handle(self):
        self.assertEqual(normalize_shop_domain("my-shop"), "my-shop.myshopify.com")

    def test_full_domain_unchanged(self):
        self.assertEqual(
            normalize_shop_domain("my-shop.myshopify.com"), "my-shop.myshopify.com"
        )

    def test_strips_scheme_and_trailing_slash(self):
        self.assertEqual(
            normalize_shop_domain("https://my-shop.myshopify.com/"),
            "my-shop.myshopify.com",
        )


class TestCostBook(unittest.TestCase):
    def test_missing_sku_returns_none(self):
        book = CostBook({"SKU-A": 4.5})
        self.assertIsNone(book.cost_for("SKU-B"))
        self.assertIsNone(book.cost_for(None))

    def test_known_sku_returns_cost(self):
        book = CostBook({"SKU-A": 4.5})
        self.assertEqual(book.cost_for("SKU-A"), 4.5)

    def test_load_missing_file_is_empty(self):
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            book = CostBook.load(Path(d) / "nope.json")
            self.assertIsNone(book.cost_for("anything"))

    def test_load_round_trips(self):
        import json
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "costs.json"
            path.write_text(json.dumps({"SKU-A": 4.5}), encoding="utf-8")
            book = CostBook.load(path)
            self.assertEqual(book.cost_for("SKU-A"), 4.5)


class TestPricing(unittest.TestCase):
    def test_margin_known_costs(self):
        book = CostBook({"SKU-A": 4.5})
        margin, missing = compute_margin(SAMPLE_ORDER, book)
        # revenue = 2 * 19.99 = 39.98; cost = 2 * 4.5 = 9.0
        self.assertAlmostEqual(margin, 30.98)
        self.assertEqual(missing, [])

    def test_margin_unknown_sku_is_flagged_not_guessed(self):
        book = CostBook({})
        margin, missing = compute_margin(SAMPLE_ORDER, book)
        self.assertAlmostEqual(margin, 39.98)  # cost contributes 0
        self.assertEqual(missing, ["SKU-A"])

    def test_complete_address_ok(self):
        self.assertFalse(has_incomplete_address(SAMPLE_ORDER))

    def test_missing_address_flagged_when_shipping_required(self):
        order = {**SAMPLE_ORDER, "shipping_address": None}
        self.assertTrue(has_incomplete_address(order))

    def test_missing_address_ignored_when_no_shipping_required(self):
        order = {
            **SAMPLE_ORDER,
            "shipping_address": None,
            "line_items": [
                {**SAMPLE_ORDER["line_items"][0], "requires_shipping": False}
            ],
        }
        self.assertFalse(has_incomplete_address(order))

    def test_incomplete_address_missing_field(self):
        order = {**SAMPLE_ORDER, "shipping_address": {**GOOD_ADDRESS, "zip": ""}}
        self.assertTrue(has_incomplete_address(order))

    def test_no_refund(self):
        self.assertFalse(has_refund(SAMPLE_ORDER))

    def test_has_refund(self):
        order = {**SAMPLE_ORDER, "refunds": [{"id": 1}]}
        self.assertTrue(has_refund(order))

    def test_high_quantity_below_threshold(self):
        self.assertFalse(has_high_quantity(SAMPLE_ORDER, 10))

    def test_high_quantity_at_threshold(self):
        self.assertTrue(has_high_quantity(SAMPLE_ORDER, 2))


class TestEvaluateOrder(unittest.TestCase):
    def setUp(self):
        from .pricing import evaluate_order

        self.evaluate = evaluate_order

    def test_clean_order_needs_no_review(self):
        book = CostBook({"SKU-A": 4.5})
        assessment = self.evaluate(
            SAMPLE_ORDER, book, high_quantity_threshold=10, min_margin_to_auto_log=0.0
        )
        self.assertFalse(assessment.needs_review)
        self.assertAlmostEqual(assessment.margin, 30.98)

    def test_unknown_cost_forces_review(self):
        book = CostBook({})
        assessment = self.evaluate(
            SAMPLE_ORDER, book, high_quantity_threshold=10, min_margin_to_auto_log=0.0
        )
        self.assertTrue(assessment.needs_review)
        self.assertIn("unknown supplier cost", assessment.reasons[0])

    def test_low_margin_forces_review(self):
        book = CostBook({"SKU-A": 100.0})  # cost exceeds revenue -> negative margin
        assessment = self.evaluate(
            SAMPLE_ORDER, book, high_quantity_threshold=10, min_margin_to_auto_log=0.0
        )
        self.assertTrue(assessment.needs_review)
        self.assertTrue(any("margin" in r for r in assessment.reasons))

    def test_multiple_reasons_all_reported(self):
        order = {**SAMPLE_ORDER, "shipping_address": None, "refunds": [{"id": 1}]}
        book = CostBook({"SKU-A": 4.5})
        assessment = self.evaluate(
            order, book, high_quantity_threshold=10, min_margin_to_auto_log=0.0
        )
        self.assertEqual(len(assessment.reasons), 2)


class TestFormatter(unittest.TestCase):
    def test_summary_includes_name_and_margin(self):
        from .pricing import OrderAssessment

        assessment = OrderAssessment(margin=30.98)
        text = format_order_summary(SAMPLE_ORDER, assessment)
        self.assertIn("#1001", text)
        self.assertIn("30.98", text)

    def test_review_description_lists_reasons(self):
        from .pricing import OrderAssessment

        assessment = OrderAssessment(
            margin=-5.0, reasons=["incomplete or missing shipping address"]
        )
        text = format_review_description(SAMPLE_ORDER, assessment)
        self.assertIn("#1001", text)
        self.assertIn("incomplete or missing shipping address", text)


class TestSeenStore(unittest.TestCase):
    def test_mark_and_check(self):
        import tempfile
        from pathlib import Path

        from . import dedup

        with tempfile.TemporaryDirectory() as d:
            orig = dedup.SEEN_FILE
            dedup.SEEN_FILE = Path(d) / "seen.json"
            try:
                store = dedup.SeenStore()
                self.assertFalse(store.is_seen("991"))
                store.mark("991")
                store.save()
                reloaded = dedup.SeenStore()
                self.assertTrue(reloaded.is_seen("991"))
            finally:
                dedup.SEEN_FILE = orig


if __name__ == "__main__":
    unittest.main()
