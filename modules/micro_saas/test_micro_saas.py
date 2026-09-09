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
from .formatter import (
    format_billing_summary,
    format_failed_charge,
    format_refunded_charge,
)

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

    def test_failed_charge_handles_a_present_but_null_amount(self):
        # Stripe can return "amount": null (e.g. a restricted API key) --
        # dict.get(key, 0) only substitutes the default when the key is
        # *absent*, not when it's present and None, so a raw `/ 100.0`
        # would raise TypeError here without to_float().
        text = format_failed_charge(
            {"id": "ch_1", "amount": None, "failure_message": "card declined"}
        )
        self.assertIn("$0.00", text)

    def test_refunded_charge_handles_a_present_but_null_amount(self):
        text = format_refunded_charge({"id": "ch_1", "amount_refunded": None})
        self.assertIn("$0.00", text)


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

    def test_save_can_preserve_an_explicit_run_at(self):
        # _run_billing passes this back in when the charges fetch for this
        # window failed -- advancing run_at to "now" anyway would silently
        # mark that window as reconciled and it would never be retried.
        import tempfile
        from pathlib import Path

        from . import snapshot as snap_mod

        with tempfile.TemporaryDirectory() as d:
            orig = snap_mod.SNAPSHOT_FILE
            snap_mod.SNAPSHOT_FILE = Path(d) / "snap.json"
            try:
                store = snap_mod.SnapshotStore()
                store.save({"sub_a"}, run_at="2026-01-01T00:00:00+00:00")
                reloaded = snap_mod.SnapshotStore()
                self.assertEqual(reloaded.previous_run_at, "2026-01-01T00:00:00+00:00")
                self.assertEqual(reloaded.previous_ids, {"sub_a"})
            finally:
                snap_mod.SNAPSHOT_FILE = orig

    def test_save_defaults_run_at_to_now_when_not_given(self):
        import tempfile
        import time
        from pathlib import Path

        from . import snapshot as snap_mod

        with tempfile.TemporaryDirectory() as d:
            orig = snap_mod.SNAPSHOT_FILE
            snap_mod.SNAPSHOT_FILE = Path(d) / "snap.json"
            try:
                store = snap_mod.SnapshotStore()
                store.save({"sub_a"})
                reloaded = snap_mod.SnapshotStore()
                since = reloaded.previous_run_unix(default_lookback_hours=24)
                self.assertLess(time.time() - since, 5)
            finally:
                snap_mod.SNAPSHOT_FILE = orig

    def test_save_with_explicit_none_stores_null_not_now(self):
        # run.py passes self.previous_run_at back in verbatim when the
        # charges fetch failed -- on the very first run ever that is itself
        # still None. save() must store that None as-is (round-tripping
        # back to previous_run_at is None on reload) rather than treating
        # an explicit None the same as "not given" and defaulting to now,
        # the same distinction _UNSET exists to make.
        import tempfile
        from pathlib import Path

        from . import snapshot as snap_mod

        with tempfile.TemporaryDirectory() as d:
            orig = snap_mod.SNAPSHOT_FILE
            snap_mod.SNAPSHOT_FILE = Path(d) / "snap.json"
            try:
                store = snap_mod.SnapshotStore()
                store.save({"sub_a"}, run_at=None)
                reloaded = snap_mod.SnapshotStore()
                self.assertIsNone(reloaded.previous_run_at)
                self.assertEqual(reloaded.previous_ids, {"sub_a"})
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


class TestRunEndToEnd(unittest.TestCase):
    """Drives the real run() against a temp database/snapshot file and a
    faked Stripe client -- no module's own run() orchestration was
    exercised end-to-end anywhere in this project before this session (see
    the same addition for ecommerce_dropshipping), only the pure functions
    underneath it. This locks in the two billing bugs fixed earlier in this
    PR (a null charge amount, and the snapshot's run_at not advancing past
    a failed charges fetch) as permanent regressions, not just the one-off
    scripts they were originally verified with."""

    def setUp(self):
        import tempfile
        from pathlib import Path

        from orchestrator import database as db

        self._tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmpdir.cleanup)
        self._orig_db_path = db.DB_PATH
        db.DB_PATH = Path(self._tmpdir.name) / "test.db"
        self.addCleanup(self._restore_db_path)
        db.init_db()
        self.db = db

        from . import snapshot as snap_mod

        self._orig_snapshot_file = snap_mod.SNAPSHOT_FILE
        snap_mod.SNAPSHOT_FILE = Path(self._tmpdir.name) / "snapshot.json"
        self.addCleanup(self._restore_snapshot_file)

    def _restore_db_path(self):
        self.db.DB_PATH = self._orig_db_path

    def _restore_snapshot_file(self):
        from . import snapshot as snap_mod

        snap_mod.SNAPSHOT_FILE = self._orig_snapshot_file

    def _patched_run(self, *, subscriptions=None, charges=None, charges_error=None):
        from unittest.mock import patch

        from . import run as run_mod

        settings = make_settings(health_url=None)

        def fake_list_charges_since(*args, **kwargs):
            if charges_error is not None:
                raise charges_error
            return charges or []

        return (
            patch.object(run_mod.Settings, "load", return_value=settings),
            patch.object(
                run_mod.stripe_client,
                "list_active_subscriptions",
                return_value=subscriptions or [],
            ),
            patch.object(
                run_mod.stripe_client,
                "list_charges_since",
                side_effect=fake_list_charges_since,
            ),
        )

    def test_a_succeeded_charge_logs_a_net_earning(self):
        from . import run as run_mod

        charge = {
            "id": "ch_1",
            "status": "succeeded",
            "amount": 1999,
            "amount_refunded": 0,
        }
        p1, p2, p3 = self._patched_run(charges=[charge])
        with p1, p2, p3:
            collected = run_mod.run()
        self.assertEqual(collected, 19.99)
        self.assertEqual(self.db.totals()["total_earnings"], 19.99)

    def test_a_charge_with_a_null_amount_does_not_crash_the_run(self):
        # Regression test for the formatter.py fix: a failed charge with
        # "amount": None used to raise TypeError deep inside
        # format_failed_charge(), crashing the whole run before its
        # snapshot could ever be saved.
        from . import run as run_mod

        charge = {
            "id": "ch_bad",
            "status": "failed",
            "amount": None,
            "failure_message": "card declined",
        }
        p1, p2, p3 = self._patched_run(charges=[charge])
        with p1, p2, p3:
            run_mod.run()  # must not raise
        self.assertEqual(len(self.db.pending_reviews()), 1)
        overview = {r["name"]: r for r in self.db.module_overview()}
        self.assertEqual(overview["micro_saas"]["state"], "ok")

    def test_a_failed_charges_fetch_preserves_run_at_for_the_next_run(self):
        # Regression test for the snapshot.py/run.py fix: previously
        # snap.save() always advanced run_at to now, even when the charges
        # fetch itself had failed -- silently skipping that revenue window
        # on every future run.
        from . import run as run_mod
        from . import snapshot as snap_mod
        from .stripe_client import StripeError

        # First, a clean run establishes a known run_at.
        p1, p2, p3 = self._patched_run(charges=[])
        with p1, p2, p3:
            run_mod.run()
        first_run_at = snap_mod.SnapshotStore().previous_run_at
        self.assertIsNotNone(first_run_at)

        # Second run: the charges fetch fails -- run_at must not advance.
        p1, p2, p3 = self._patched_run(charges_error=StripeError("boom"))
        with p1, p2, p3:
            run_mod.run()
        self.assertEqual(snap_mod.SnapshotStore().previous_run_at, first_run_at)

    def test_a_failed_first_ever_run_does_not_fabricate_a_run_at(self):
        # Regression test for a gap the fix above didn't originally cover:
        # snap.save() correctly preserves an *existing* previous_run_at when
        # the charges fetch fails, but on a brand new install nothing has
        # ever been reconciled yet, so previous_run_at is itself still None
        # at that point. A bare `run_at or now()` would treat that None the
        # same as "no override given" and advance run_at to now anyway --
        # silently discarding exactly the window this fix exists to
        # protect, the one time (the very first run) it matters most.
        from . import run as run_mod
        from . import snapshot as snap_mod
        from .stripe_client import StripeError

        p1, p2, p3 = self._patched_run(charges_error=StripeError("boom"))
        with p1, p2, p3:
            run_mod.run()  # must not raise

        self.assertIsNone(snap_mod.SnapshotStore().previous_run_at)

    def test_new_and_churned_subscriptions_are_tracked_across_runs(self):
        from . import run as run_mod
        from . import snapshot as snap_mod

        p1, p2, p3 = self._patched_run(subscriptions=[{"id": "sub_a", "items": {}}])
        with p1, p2, p3:
            run_mod.run()
        self.assertEqual(snap_mod.SnapshotStore().previous_ids, {"sub_a"})

        # sub_a churned, sub_b is new.
        p1, p2, p3 = self._patched_run(subscriptions=[{"id": "sub_b", "items": {}}])
        with p1, p2, p3:
            run_mod.run()
        self.assertEqual(snap_mod.SnapshotStore().previous_ids, {"sub_b"})


if __name__ == "__main__":
    unittest.main()
