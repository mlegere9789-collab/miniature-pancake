"""Offline unit tests for the Micro-SaaS Tool.

No network required — all Stripe/health responses are faked. Run with:
    python -m unittest modules.micro_saas.test_micro_saas
or via the whole suite:
    python -m unittest discover -s modules -p "test_*.py"
"""

from __future__ import annotations

import unittest

from .billing import compute_mrr, diff_subscriptions, summarize_charges
from .config import Settings
from .formatter import format_billing_summary, format_failed_charge

MONTHLY_SUB = {
    "id": "sub_month",
    "items": {
        "data": [
            {
                "quantity": 1,
                "price": {
                    "unit_amount": 1999,
                    "recurring": {"interval": "month", "interval_count": 1},
                },
            }
        ]
    },
}

YEARLY_SUB = {
    "id": "sub_year",
    "items": {
        "data": [
            {
                "quantity": 2,
                "price": {
                    "unit_amount": 12000,
                    "recurring": {"interval": "year", "interval_count": 1},
                },
            }
        ]
    },
}


def make_settings(**overrides) -> Settings:
    base = dict(
        stripe_secret_key="sk_test",
        health_url=None,
        health_timeout=10,
        sub_limit=100,
        charge_limit=100,
        lookback_hours=24,
        high_churn_threshold=3,
    )
    base.update(overrides)
    return Settings(**base)


class TestSettings(unittest.TestCase):
    def test_billing_configured(self):
        self.assertTrue(make_settings().billing_configured)
        self.assertFalse(make_settings(stripe_secret_key=None).billing_configured)

    def test_health_configured(self):
        self.assertFalse(make_settings().health_configured)
        self.assertTrue(
            make_settings(health_url="https://example.com/health").health_configured
        )


class TestComputeMrr(unittest.TestCase):
    def test_monthly_subscription(self):
        self.assertAlmostEqual(compute_mrr([MONTHLY_SUB]), 19.99)

    def test_yearly_subscription_normalized_to_monthly(self):
        # 2 seats * $120/yr = $240/yr -> $20/mo
        self.assertAlmostEqual(compute_mrr([YEARLY_SUB]), 20.0)

    def test_combined(self):
        self.assertAlmostEqual(compute_mrr([MONTHLY_SUB, YEARLY_SUB]), 39.99)

    def test_empty(self):
        self.assertEqual(compute_mrr([]), 0.0)

    def test_missing_items_does_not_crash(self):
        self.assertEqual(compute_mrr([{"id": "sub_x"}]), 0.0)


class TestDiffSubscriptions(unittest.TestCase):
    def test_new_and_churned(self):
        previous = {"sub_a", "sub_b"}
        current = {"sub_b", "sub_c"}
        new_ids, churned_ids = diff_subscriptions(previous, current)
        self.assertEqual(new_ids, {"sub_c"})
        self.assertEqual(churned_ids, {"sub_a"})

    def test_no_previous_all_new(self):
        new_ids, churned_ids = diff_subscriptions(set(), {"sub_a"})
        self.assertEqual(new_ids, {"sub_a"})
        self.assertEqual(churned_ids, set())

    def test_identical_no_change(self):
        ids = {"sub_a", "sub_b"}
        new_ids, churned_ids = diff_subscriptions(ids, ids)
        self.assertEqual(new_ids, set())
        self.assertEqual(churned_ids, set())


class TestSummarizeCharges(unittest.TestCase):
    def test_succeeded_charge_collected(self):
        summary = summarize_charges(
            [
                {
                    "id": "ch_1",
                    "status": "succeeded",
                    "amount": 1000,
                    "amount_refunded": 0,
                }
            ]
        )
        self.assertAlmostEqual(summary.collected, 10.0)
        self.assertEqual(summary.failed, [])
        self.assertEqual(summary.refunded, [])

    def test_partial_refund_reduces_collected_and_is_flagged(self):
        summary = summarize_charges(
            [
                {
                    "id": "ch_2",
                    "status": "succeeded",
                    "amount": 1000,
                    "amount_refunded": 300,
                }
            ]
        )
        self.assertAlmostEqual(summary.collected, 7.0)
        self.assertEqual(len(summary.refunded), 1)

    def test_failed_charge_not_collected_but_flagged(self):
        charge = {
            "id": "ch_3",
            "status": "failed",
            "amount": 500,
            "amount_refunded": 0,
            "failure_message": "card declined",
        }
        summary = summarize_charges([charge])
        self.assertEqual(summary.collected, 0.0)
        self.assertEqual(summary.failed, [charge])

    def test_empty(self):
        summary = summarize_charges([])
        self.assertEqual(summary.collected, 0.0)
        self.assertEqual(summary.failed, [])
        self.assertEqual(summary.refunded, [])


class TestFormatter(unittest.TestCase):
    def test_billing_summary_includes_mrr_and_collected(self):
        from .billing import ChargeSummary

        text = format_billing_summary(19.99, {"sub_c"}, {"sub_a"}, ChargeSummary(10.0))
        self.assertIn("19.99", text)
        self.assertIn("1 new", text)
        self.assertIn("1 churned", text)
        self.assertIn("10.00", text)

    def test_failed_charge_includes_reason(self):
        text = format_failed_charge(
            {"id": "ch_1", "amount": 500, "failure_message": "card declined"}
        )
        self.assertIn("ch_1", text)
        self.assertIn("card declined", text)
        self.assertIn("5.00", text)


class TestSnapshotStore(unittest.TestCase):
    def test_round_trip(self):
        import tempfile
        from pathlib import Path

        from . import snapshot as snap_mod

        with tempfile.TemporaryDirectory() as d:
            orig = snap_mod.SNAPSHOT_FILE
            snap_mod.SNAPSHOT_FILE = Path(d) / "snap.json"
            try:
                store = snap_mod.SnapshotStore()
                self.assertEqual(store.previous_ids, set())
                store.save({"sub_a", "sub_b"})
                reloaded = snap_mod.SnapshotStore()
                self.assertEqual(reloaded.previous_ids, {"sub_a", "sub_b"})
            finally:
                snap_mod.SNAPSHOT_FILE = orig

    def test_previous_run_unix_falls_back_to_lookback(self):
        import tempfile
        import time
        from pathlib import Path

        from . import snapshot as snap_mod

        with tempfile.TemporaryDirectory() as d:
            orig = snap_mod.SNAPSHOT_FILE
            snap_mod.SNAPSHOT_FILE = Path(d) / "snap.json"
            try:
                store = snap_mod.SnapshotStore()
                since = store.previous_run_unix(default_lookback_hours=24)
                now = time.time()
                self.assertLess(now - since, 24 * 3600 + 5)
                self.assertGreater(now - since, 24 * 3600 - 5)
            finally:
                snap_mod.SNAPSHOT_FILE = orig


class TestHealth(unittest.TestCase):
    def test_check_health_handles_unreachable_host(self):
        from .health import check_health

        result = check_health("http://127.0.0.1:1/does-not-exist", timeout=1)
        self.assertFalse(result.ok)
        self.assertTrue(result.detail)


if __name__ == "__main__":
    unittest.main()
