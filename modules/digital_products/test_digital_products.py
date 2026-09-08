"""Offline unit tests for Digital Product Creation.

No network required — all Anthropic API responses are faked. Run with:
    python -m unittest modules.digital_products.test_digital_products
or via the whole suite:
    python -m unittest discover -s modules -p "test_*.py"
"""

from __future__ import annotations

import unittest

from .briefs import load_briefs
from .config import Settings
from .copywriter import CopyParseError, build_prompt, parse_reply
from .formatter import format_parse_failure_description, format_review_description

SAMPLE_BRIEF = {
    "id": "budget-tracker",
    "name": "Notion Budget Tracker Template",
    "category": "Notion template",
    "features": ["monthly budget view", "recurring bill tracker"],
    "audience": "young professionals",
    "price_usd": 9.0,
}

VALID_REPLY = (
    '{"title": "Notion Budget Tracker", '
    '"description": "Track every dollar with ease. Built for young '
    'professionals who want clarity without spreadsheets.", '
    '"tags": ["notion template", "budget tracker", "finance"]}'
)


def make_settings(**overrides) -> Settings:
    base = dict(
        anthropic_api_key="sk-ant-test",
        model="claude-3-5-haiku-20241022",
        max_tokens=700,
        max_per_run=5,
    )
    base.update(overrides)
    return Settings(**base)


class TestSettings(unittest.TestCase):
    def test_configured_requires_api_key(self):
        self.assertTrue(make_settings().configured)
        self.assertFalse(make_settings(anthropic_api_key=None).configured)


class TestBriefs(unittest.TestCase):
    def test_load_missing_file_is_empty(self):
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(load_briefs(Path(d) / "nope.json"), [])

    def test_load_round_trips(self):
        import json
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "briefs.json"
            path.write_text(json.dumps([SAMPLE_BRIEF]), encoding="utf-8")
            self.assertEqual(load_briefs(path), [SAMPLE_BRIEF])

    def test_load_skips_malformed_entries(self):
        import json
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "briefs.json"
            path.write_text(
                json.dumps([SAMPLE_BRIEF, {"name": "missing id"}, "not a dict"]),
                encoding="utf-8",
            )
            self.assertEqual(load_briefs(path), [SAMPLE_BRIEF])

    def test_load_non_list_json_is_empty(self):
        import json
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "briefs.json"
            path.write_text(json.dumps({"not": "a list"}), encoding="utf-8")
            self.assertEqual(load_briefs(path), [])


class TestCopywriter(unittest.TestCase):
    def test_build_prompt_includes_brief_fields(self):
        prompt = build_prompt(SAMPLE_BRIEF)
        self.assertIn("Notion Budget Tracker Template", prompt)
        self.assertIn("young professionals", prompt)
        self.assertIn("monthly budget view, recurring bill tracker", prompt)

    def test_build_prompt_handles_missing_fields(self):
        prompt = build_prompt({"id": "x", "name": "Thing"})
        self.assertIn("Thing", prompt)
        self.assertIn("not specified", prompt)

    def test_parse_valid_reply(self):
        copy = parse_reply(VALID_REPLY)
        self.assertEqual(copy.title, "Notion Budget Tracker")
        self.assertIn("young professionals", copy.description)
        self.assertEqual(len(copy.tags), 3)

    def test_parse_strips_markdown_fence(self):
        fenced = f"```json\n{VALID_REPLY}\n```"
        copy = parse_reply(fenced)
        self.assertEqual(copy.title, "Notion Budget Tracker")

    def test_parse_rejects_non_json(self):
        with self.assertRaises(CopyParseError):
            parse_reply("Sure, here's a great title for your product!")

    def test_parse_rejects_missing_title(self):
        with self.assertRaises(CopyParseError):
            parse_reply('{"description": "x", "tags": []}')

    def test_parse_rejects_non_string_tags(self):
        with self.assertRaises(CopyParseError):
            parse_reply('{"title": "t", "description": "d", "tags": [1, 2]}')

    def test_parse_rejects_empty_title(self):
        with self.assertRaises(CopyParseError):
            parse_reply('{"title": "  ", "description": "d", "tags": []}')


class TestFormatter(unittest.TestCase):
    def test_review_description_includes_copy(self):
        from .copywriter import ListingCopy

        copy = ListingCopy(title="T", description="D", tags=["a", "b"])
        text = format_review_description(SAMPLE_BRIEF, copy)
        self.assertIn("T", text)
        self.assertIn("D", text)
        self.assertIn("a, b", text)
        self.assertIn("budget-tracker", text)

    def test_parse_failure_description_includes_raw_text(self):
        text = format_parse_failure_description(SAMPLE_BRIEF, "not json at all")
        self.assertIn("not json at all", text)
        self.assertIn("budget-tracker", text)


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
                self.assertFalse(store.is_seen("budget-tracker"))
                store.mark("budget-tracker")
                store.save()
                reloaded = dedup.SeenStore()
                self.assertTrue(reloaded.is_seen("budget-tracker"))
            finally:
                dedup.SEEN_FILE = orig

    def test_a_numeric_id_still_dedups_after_a_reload(self):
        # Nothing in briefs.py enforces a brief's own "id" be a string --
        # json.dumps() in save() silently turns a non-string dict key into
        # a string key on write, so without coercing in is_seen/mark too,
        # is_seen(1) after a reload would check `1 in {"1": ...}` (always
        # False) and this brief would be redrafted every single run.
        import tempfile
        from pathlib import Path

        from . import dedup

        with tempfile.TemporaryDirectory() as d:
            orig = dedup.SEEN_FILE
            dedup.SEEN_FILE = Path(d) / "seen.json"
            try:
                store = dedup.SeenStore()
                store.mark(1)
                store.save()
                reloaded = dedup.SeenStore()
                self.assertTrue(reloaded.is_seen(1))
            finally:
                dedup.SEEN_FILE = orig


class TestRunEndToEnd(unittest.TestCase):
    """Drives the real run() against a temp database/dedup file and a
    faked Claude client -- no module's own run() orchestration was
    exercised end-to-end anywhere in this project before this session.
    Locks in the numeric-id dedup fix as a permanent regression rather
    than only the one-off script it was verified with."""

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

    def _patched_run(self, briefs, reply=VALID_REPLY, **settings_overrides):
        from unittest.mock import patch

        from . import run as run_mod

        settings = make_settings(**settings_overrides)
        return (
            patch.object(run_mod.Settings, "load", return_value=settings),
            patch.object(run_mod, "load_briefs", return_value=briefs),
            patch.object(run_mod.anthropic_client, "complete", return_value=reply),
        )

    def test_a_fresh_brief_is_drafted_and_flagged_for_review(self):
        from . import run as run_mod

        p1, p2, p3 = self._patched_run([SAMPLE_BRIEF])
        with p1, p2, p3:
            drafted = run_mod.run()
        self.assertEqual(drafted, 1)
        self.assertEqual(len(self.db.pending_reviews()), 1)

        from . import dedup

        self.assertTrue(dedup.SeenStore().is_seen("budget-tracker"))

    def test_a_second_run_does_not_redraft_the_same_brief(self):
        from . import run as run_mod

        p1, p2, p3 = self._patched_run([SAMPLE_BRIEF])
        with p1, p2, p3:
            run_mod.run()
        p1, p2, p3 = self._patched_run([SAMPLE_BRIEF])
        with p1, p2, p3:
            second = run_mod.run()
        self.assertEqual(second, 0)
        self.assertEqual(len(self.db.pending_reviews()), 1)

    def test_a_numeric_brief_id_still_dedups_across_real_runs(self):
        # Regression test for the dedup.py fix: a brief with a JSON-number
        # id used to silently defeat dedup after a reload, redrafting (and
        # re-paying for) the same brief on every single run forever.
        from . import run as run_mod

        numeric_brief = {**SAMPLE_BRIEF, "id": 42}
        p1, p2, p3 = self._patched_run([numeric_brief])
        with p1, p2, p3:
            run_mod.run()
        p1, p2, p3 = self._patched_run([numeric_brief])
        with p1, p2, p3:
            second = run_mod.run()
        self.assertEqual(second, 0)
        self.assertEqual(len(self.db.pending_reviews()), 1)

    def test_a_malformed_reply_still_flags_for_review_with_raw_text(self):
        from . import run as run_mod

        p1, p2, p3 = self._patched_run([SAMPLE_BRIEF], reply="not json at all")
        with p1, p2, p3:
            drafted = run_mod.run()
        self.assertEqual(drafted, 1)
        reviews = self.db.pending_reviews()
        self.assertEqual(len(reviews), 1)
        self.assertIn("not json at all", reviews[0]["payload"])


if __name__ == "__main__":
    unittest.main()
