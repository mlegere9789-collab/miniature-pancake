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


if __name__ == "__main__":
    unittest.main()
