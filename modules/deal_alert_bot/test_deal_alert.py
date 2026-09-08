"""Offline unit tests for the Deal-Alert Bot.

No network required — all API responses are faked. Run with:
    python -m unittest modules.deal_alert_bot.test_deal_alert
or via the whole suite:
    python -m unittest discover -s modules -p "test_*.py"
"""

from __future__ import annotations

import unittest

from .config import Settings
from .dedup import SeenStore
from .formatter import build_deal_url, format_discord_payload, format_plaintext

SAMPLE_DEAL = {
    "title": "Hollow Knight",
    "dealID": "ABC123",
    "storeID": "1",
    "salePrice": "7.49",
    "normalPrice": "14.99",
    "savings": "50.033",
    "thumb": "https://example.com/thumb.jpg",
}


def make_settings(**overrides) -> Settings:
    base = dict(
        webhook_url=None,
        dry_run=True,
        min_savings=50.0,
        max_price=None,
        require_historic_low=False,
        store_ids=["1"],
        page_size=60,
        max_posts_per_run=5,
        affiliate_template="",
    )
    base.update(overrides)
    return Settings(**base)


class TestQualifies(unittest.TestCase):
    def setUp(self):
        # Import here so a missing orchestrator path fails loudly in one place.
        from .run import qualifies

        self.qualifies = qualifies

    def test_meets_savings(self):
        self.assertTrue(self.qualifies(SAMPLE_DEAL, make_settings(min_savings=50)))

    def test_below_savings_threshold(self):
        self.assertFalse(self.qualifies(SAMPLE_DEAL, make_settings(min_savings=75)))

    def test_price_cap_excludes(self):
        self.assertFalse(self.qualifies(SAMPLE_DEAL, make_settings(max_price=5.0)))

    def test_price_cap_includes(self):
        self.assertTrue(self.qualifies(SAMPLE_DEAL, make_settings(max_price=10.0)))

    def test_historic_low_required_but_missing(self):
        s = make_settings(require_historic_low=True)
        self.assertFalse(self.qualifies(SAMPLE_DEAL, s, cheapest_ever=None))

    def test_historic_low_tie_qualifies(self):
        s = make_settings(require_historic_low=True)
        self.assertTrue(self.qualifies(SAMPLE_DEAL, s, cheapest_ever=7.49))

    def test_historic_low_not_beaten(self):
        s = make_settings(require_historic_low=True)
        self.assertFalse(self.qualifies(SAMPLE_DEAL, s, cheapest_ever=5.00))

    def test_garbage_deal_does_not_crash(self):
        self.assertFalse(self.qualifies({"savings": "oops"}, make_settings()))


class TestFormatter(unittest.TestCase):
    def test_plain_link_is_placeholder(self):
        url, is_aff = build_deal_url(SAMPLE_DEAL, affiliate_template="")
        self.assertFalse(is_aff)
        self.assertIn("dealID=ABC123", url)
        text = format_plaintext(SAMPLE_DEAL, url, is_aff)
        self.assertIn("Hollow Knight", text)
        self.assertIn("50% off", text)
        self.assertIn("placeholder", text)

    def test_affiliate_template_applied(self):
        tmpl = "https://partner.example/r?u={deal_url}&id=XYZ"
        url, is_aff = build_deal_url(SAMPLE_DEAL, affiliate_template=tmpl)
        self.assertTrue(is_aff)
        self.assertIn("partner.example", url)
        self.assertIn("id=XYZ", url)

    def test_malformed_template_falls_back_to_plain_link_extra_placeholder(self):
        # A stray {other} the operator forgot to also give a value for --
        # str.format raises KeyError. Every deal in every run hits this
        # same call, so an uncaught error here would crash the whole run
        # forever until the .env template is fixed.
        tmpl = "https://partner.example/r?u={deal_url}&id={other}"
        url, is_aff = build_deal_url(SAMPLE_DEAL, affiliate_template=tmpl)
        self.assertFalse(is_aff)
        self.assertIn("dealID=ABC123", url)

    def test_malformed_template_falls_back_to_plain_link_stray_brace(self):
        tmpl = "https://partner.example/r?u={deal_url}&note={"
        url, is_aff = build_deal_url(SAMPLE_DEAL, affiliate_template=tmpl)
        self.assertFalse(is_aff)
        self.assertIn("dealID=ABC123", url)

    def test_discord_payload_shape(self):
        url, is_aff = build_deal_url(SAMPLE_DEAL)
        payload = format_discord_payload(SAMPLE_DEAL, url, is_aff)
        self.assertIn("content", payload)
        self.assertEqual(len(payload["embeds"]), 1)
        embed = payload["embeds"][0]
        self.assertIn("Hollow Knight", embed["title"])
        self.assertEqual(embed["thumbnail"]["url"], SAMPLE_DEAL["thumb"])


class TestSeenStore(unittest.TestCase):
    def test_mark_and_check(self):
        import tempfile
        from pathlib import Path

        from . import dedup

        with tempfile.TemporaryDirectory() as d:
            orig = dedup.SEEN_FILE
            dedup.SEEN_FILE = Path(d) / "seen.json"
            try:
                store = SeenStore()
                self.assertFalse(store.is_seen("ABC123"))
                store.mark("ABC123")
                store.save()
                reloaded = SeenStore()
                self.assertTrue(reloaded.is_seen("ABC123"))
            finally:
                dedup.SEEN_FILE = orig


class TestRunEndToEnd(unittest.TestCase):
    """Drives the real run() against a temp database/dedup file and a
    faked CheapShark client -- no module's own run() orchestration was
    exercised end-to-end anywhere in this project before this session,
    only the pure functions underneath it (qualifies(), build_deal_url(),
    dedup). Locks in the malformed-affiliate-template fix as a permanent
    regression rather than only the one-off script it was verified with."""

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

        from . import dedup

        self._orig_seen_file = dedup.SEEN_FILE
        dedup.SEEN_FILE = Path(self._tmpdir.name) / "seen.json"
        self.addCleanup(self._restore_seen_file)

    def _restore_db_path(self):
        self.db.DB_PATH = self._orig_db_path

    def _restore_seen_file(self):
        from . import dedup

        dedup.SEEN_FILE = self._orig_seen_file

    def _patched_run(self, deals, **settings_overrides):
        from unittest.mock import patch

        from . import run as run_mod

        settings = make_settings(**settings_overrides)
        return (
            patch.object(run_mod.Settings, "load", return_value=settings),
            patch.object(run_mod.cheapshark, "fetch_deals", return_value=deals),
        )

    def test_a_qualifying_deal_is_logged_in_dry_run_and_not_marked_seen(self):
        # Dry-run deliberately never marks as seen (see run.py's own
        # comment): flipping to live later should still post it once.
        from . import run as run_mod

        p1, p2 = self._patched_run([SAMPLE_DEAL])
        with p1, p2:
            posted = run_mod.run()
        self.assertEqual(posted, 1)

        from . import dedup

        self.assertFalse(dedup.SeenStore().is_seen("ABC123"))

    def test_malformed_affiliate_template_does_not_crash_the_run(self):
        # Regression test for the formatter.py fix: build_deal_url() is
        # called on every qualifying deal regardless of dry-run/live mode,
        # before the dry-run branch -- a bad template used to crash here
        # every single run, unconditionally.
        from . import run as run_mod

        bad_template = "https://partner.example/r?u={deal_url}&id={typo}"
        p1, p2 = self._patched_run([SAMPLE_DEAL], affiliate_template=bad_template)
        with p1, p2:
            posted = run_mod.run()  # must not raise
        self.assertEqual(posted, 1)
        overview = {r["name"]: r for r in self.db.module_overview()}
        self.assertIn(overview["deal_alert_bot"]["state"], ("ok", "warning"))

    def test_a_posted_live_deal_is_marked_seen_and_not_reposted(self):
        from unittest.mock import patch

        from . import run as run_mod

        p1, p2 = self._patched_run(
            [SAMPLE_DEAL], dry_run=False, webhook_url="http://example.invalid/hook"
        )
        with p1, p2, patch.object(run_mod, "post_webhook") as post_mock:
            first = run_mod.run()
        post_mock.assert_called_once()
        self.assertEqual(first, 1)

        p1, p2 = self._patched_run(
            [SAMPLE_DEAL], dry_run=False, webhook_url="http://example.invalid/hook"
        )
        with p1, p2, patch.object(run_mod, "post_webhook") as post_mock:
            second = run_mod.run()
        post_mock.assert_not_called()
        self.assertEqual(second, 0)


if __name__ == "__main__":
    unittest.main()
