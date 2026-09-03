"""Offline unit tests for the shared orchestrator foundation.

No network required. Database tests point `database.DB_PATH` at a fresh
temporary file per test (see `TempDatabaseTestCase`) so they never touch
the real `data/orchestrator.db`. Run with:

    python -m unittest orchestrator.test_orchestrator
or via the whole suite:
    python -m unittest discover -s orchestrator -p "test_*.py"
"""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from . import database as db
from .config import Config, MissingCredentialError
from .logger import ModuleLogger, get_logger
from .paths import MODULES


class TempDatabaseTestCase(unittest.TestCase):
    """Points database.DB_PATH at a fresh temp file for the test's duration."""

    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmpdir.cleanup)
        self._orig_db_path = db.DB_PATH
        db.DB_PATH = Path(self._tmpdir.name) / "test.db"
        self.addCleanup(self._restore_db_path)
        db.init_db()

    def _restore_db_path(self):
        db.DB_PATH = self._orig_db_path


class TestInitDb(TempDatabaseTestCase):
    def test_seeds_all_modules(self):
        names = {row["name"] for row in db.module_overview()}
        self.assertEqual(names, set(MODULES))

    def test_seeds_idle_status(self):
        for row in db.module_overview():
            self.assertEqual(row["state"], "idle")
            self.assertEqual(row["detail"], "Not started yet")

    def test_idempotent(self):
        db.init_db()  # run again against the same (temp) database
        self.assertEqual(len(db.module_overview()), len(MODULES))


class TestRecordActivity(TempDatabaseTestCase):
    def test_record_and_read_back(self):
        db.record_activity("deal_alert_bot", "hello", level="info", event="test")
        activity = db.recent_activity(limit=5)
        self.assertEqual(len(activity), 1)
        self.assertEqual(activity[0]["message"], "hello")
        self.assertEqual(activity[0]["module"], "deal_alert_bot")
        self.assertEqual(activity[0]["event"], "test")

    def test_metadata_round_trips_as_json(self):
        db.record_activity("deal_alert_bot", "m", metadata={"a": 1})
        activity = db.recent_activity(limit=1)
        self.assertEqual(json.loads(activity[0]["metadata"]), {"a": 1})

    def test_no_metadata_is_null(self):
        db.record_activity("deal_alert_bot", "m")
        self.assertIsNone(db.recent_activity(limit=1)[0]["metadata"])

    def test_recent_activity_newest_first_and_limited(self):
        for i in range(5):
            db.record_activity("deal_alert_bot", f"msg{i}")
        activity = db.recent_activity(limit=3)
        self.assertEqual(len(activity), 3)
        self.assertEqual(activity[0]["message"], "msg4")


class TestSetStatus(TempDatabaseTestCase):
    def test_overwrites_seeded_status(self):
        db.set_status("deal_alert_bot", "running", "Scanning")
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertEqual(overview["deal_alert_bot"]["state"], "running")
        self.assertEqual(overview["deal_alert_bot"]["detail"], "Scanning")

    def test_default_detail_is_empty(self):
        db.set_status("deal_alert_bot", "ok")
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertEqual(overview["deal_alert_bot"]["detail"], "")

    def test_other_modules_unaffected(self):
        db.set_status("deal_alert_bot", "error", "boom")
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertEqual(overview["micro_saas"]["state"], "idle")


class TestEarnings(TempDatabaseTestCase):
    def test_totals_zero_with_no_earnings(self):
        self.assertEqual(db.totals()["total_earnings"], 0)

    def test_record_and_totals(self):
        db.record_earning("deal_alert_bot", 4.20, source="amazon")
        db.record_earning("deal_alert_bot", 1.80, source="amazon")
        self.assertAlmostEqual(db.totals()["total_earnings"], 6.00)

    def test_module_overview_sums_per_module(self):
        db.record_earning("deal_alert_bot", 10.0)
        db.record_earning("micro_saas", 5.0)
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertAlmostEqual(overview["deal_alert_bot"]["total_earnings"], 10.0)
        self.assertAlmostEqual(overview["micro_saas"]["total_earnings"], 5.0)
        self.assertEqual(overview["digital_products"]["total_earnings"], 0)


class TestReviewQueue(TempDatabaseTestCase):
    def test_add_and_pending(self):
        rid = db.add_review_item("deal_alert_bot", "Post this?", description="d")
        pending = db.pending_reviews()
        self.assertEqual(len(pending), 1)
        self.assertEqual(pending[0]["id"], rid)
        self.assertEqual(pending[0]["status"], "pending")

    def test_payload_round_trips_as_json(self):
        db.add_review_item("deal_alert_bot", "t", payload={"url": "x"})
        self.assertEqual(json.loads(db.pending_reviews()[0]["payload"]), {"url": "x"})

    def test_no_payload_is_null(self):
        db.add_review_item("deal_alert_bot", "t")
        self.assertIsNone(db.pending_reviews()[0]["payload"])

    def test_resolve_approved_removes_from_pending(self):
        rid = db.add_review_item("deal_alert_bot", "t")
        self.assertTrue(db.resolve_review_item(rid, "approved"))
        self.assertEqual(db.pending_reviews(), [])

    def test_resolve_unknown_id_returns_false(self):
        self.assertFalse(db.resolve_review_item(999999, "approved"))

    def test_resolve_already_resolved_returns_false(self):
        rid = db.add_review_item("deal_alert_bot", "t")
        db.resolve_review_item(rid, "approved")
        self.assertFalse(db.resolve_review_item(rid, "rejected"))

    def test_resolve_invalid_decision_raises(self):
        rid = db.add_review_item("deal_alert_bot", "t")
        with self.assertRaises(ValueError):
            db.resolve_review_item(rid, "maybe")

    def test_module_overview_pending_count(self):
        db.add_review_item("deal_alert_bot", "a")
        db.add_review_item("deal_alert_bot", "b")
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertEqual(overview["deal_alert_bot"]["pending_reviews"], 2)


class TestModuleOverviewLastActivity(TempDatabaseTestCase):
    def test_reflects_most_recent_message(self):
        db.record_activity("deal_alert_bot", "first")
        db.record_activity("deal_alert_bot", "second")
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertEqual(overview["deal_alert_bot"]["last_activity"], "second")

    def test_none_when_no_activity(self):
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertIsNone(overview["deal_alert_bot"]["last_activity"])


class TestConfig(unittest.TestCase):
    def _write_env(self, contents: str) -> Path:
        tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(tmpdir.cleanup)
        path = Path(tmpdir.name) / ".env"
        path.write_text(contents, encoding="utf-8")
        return path

    def test_get_missing_key_returns_default(self):
        cfg = Config(self._write_env(""))
        self.assertIsNone(cfg.get("NOPE"))
        self.assertEqual(cfg.get("NOPE", "d"), "d")

    def test_parses_simple_key_value(self):
        cfg = Config(self._write_env("FOO=bar\n"))
        self.assertEqual(cfg.get("FOO"), "bar")

    def test_ignores_comments_and_blank_lines(self):
        cfg = Config(self._write_env("# comment\n\nFOO=bar\n"))
        self.assertEqual(cfg.get("FOO"), "bar")

    def test_strips_export_prefix(self):
        cfg = Config(self._write_env("export FOO=bar\n"))
        self.assertEqual(cfg.get("FOO"), "bar")

    def test_strips_matching_quotes(self):
        cfg = Config(self._write_env('FOO="bar baz"\n'))
        self.assertEqual(cfg.get("FOO"), "bar baz")

    def test_require_raises_when_missing(self):
        cfg = Config(self._write_env(""))
        with self.assertRaises(MissingCredentialError):
            cfg.require("NOPE")

    def test_require_returns_value_when_present(self):
        cfg = Config(self._write_env("FOO=bar\n"))
        self.assertEqual(cfg.require("FOO"), "bar")

    def test_empty_value_treated_as_missing_by_require(self):
        cfg = Config(self._write_env("FOO=\n"))
        with self.assertRaises(MissingCredentialError):
            cfg.require("FOO")

    def test_has(self):
        cfg = Config(self._write_env("FOO=bar\n"))
        self.assertTrue(cfg.has("FOO"))
        self.assertFalse(cfg.has("NOPE"))

    def test_missing_file_is_empty_not_an_error(self):
        cfg = Config(Path("/nonexistent/path/.env"))
        self.assertIsNone(cfg.get("FOO"))

    def test_env_var_overrides_file(self):
        cfg = Config(self._write_env("SHARED=filevalue\n"))
        with patch.dict(os.environ, {"SHARED": "envvalue"}):
            self.assertEqual(cfg.get("SHARED"), "envvalue")

    def test_as_dict_redacts_by_default(self):
        cfg = Config(self._write_env("FOO=supersecretvalue\n"))
        redacted = cfg.as_dict()
        self.assertNotEqual(redacted["FOO"], "supersecretvalue")
        self.assertTrue(redacted["FOO"].startswith("supe"))

    def test_as_dict_unredacted_returns_real_value(self):
        cfg = Config(self._write_env("FOO=supersecretvalue\n"))
        self.assertEqual(cfg.as_dict(redacted=False)["FOO"], "supersecretvalue")


class TestLogger(TempDatabaseTestCase):
    def test_unknown_module_raises(self):
        with self.assertRaises(ValueError):
            get_logger("not_a_real_module")

    def test_known_module_returns_logger(self):
        log = get_logger("deal_alert_bot")
        self.assertIsInstance(log, ModuleLogger)
        self.assertEqual(log.module, "deal_alert_bot")

    def test_activity_writes_to_db(self):
        get_logger("deal_alert_bot").activity("hi")
        self.assertEqual(db.recent_activity(limit=1)[0]["message"], "hi")

    def test_level_shortcuts_set_level(self):
        get_logger("deal_alert_bot").warning("careful")
        self.assertEqual(db.recent_activity(limit=1)[0]["level"], "warning")

    def test_status_writes_to_db(self):
        get_logger("deal_alert_bot").status("running", "doing stuff")
        overview = {r["name"]: r for r in db.module_overview()}
        self.assertEqual(overview["deal_alert_bot"]["state"], "running")

    def test_earning_writes_to_db(self):
        get_logger("deal_alert_bot").earning(5.0, source="test")
        self.assertAlmostEqual(db.totals()["total_earnings"], 5.0)

    def test_flag_for_review_writes_to_db(self):
        get_logger("deal_alert_bot").flag_for_review("Approve?", description="d")
        pending = db.pending_reviews()
        self.assertEqual(len(pending), 1)
        self.assertEqual(pending[0]["title"], "Approve?")


class TestPaths(unittest.TestCase):
    def test_ensure_data_dir_creates_nested_directory(self):
        from . import paths

        with tempfile.TemporaryDirectory() as d:
            target = Path(d) / "nested" / "data"
            orig = paths.DATA_DIR
            paths.DATA_DIR = target
            try:
                result = paths.ensure_data_dir()
                self.assertTrue(target.is_dir())
                self.assertEqual(result, target)
            finally:
                paths.DATA_DIR = orig

    def test_modules_has_five_entries(self):
        self.assertEqual(len(MODULES), 5)
        for name in MODULES:
            self.assertIsInstance(name, str)
            self.assertTrue(name)


if __name__ == "__main__":
    unittest.main()
