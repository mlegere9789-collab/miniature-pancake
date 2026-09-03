"""Offline unit tests for the shared orchestrator foundation.

No network required. Database tests point `database.DB_PATH` at a fresh
temporary file per test (see `TempDatabaseTestCase`) so they never touch
the real `data/orchestrator.db`. Run with:

    python -m unittest orchestrator.test_orchestrator
or via the whole suite:
    python -m unittest discover -s orchestrator -t . -p "test_*.py"
"""

from __future__ import annotations

import io
import json
import os
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from contextlib import redirect_stdout
from datetime import datetime, timedelta, timezone
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch

from . import cli, dashboard
from . import database as db
from . import scheduler as sch
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


class JobsFileTestCase(unittest.TestCase):
    """Points scheduler.JOBS_PATH/JOBS_EXAMPLE_PATH at temp files."""

    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmpdir.cleanup)
        self._orig_jobs_path = sch.JOBS_PATH
        self._orig_jobs_example_path = sch.JOBS_EXAMPLE_PATH
        sch.JOBS_PATH = Path(self._tmpdir.name) / "jobs.json"
        sch.JOBS_EXAMPLE_PATH = Path(self._tmpdir.name) / "jobs.example.json"
        self.addCleanup(self._restore)

    def _restore(self):
        sch.JOBS_PATH = self._orig_jobs_path
        sch.JOBS_EXAMPLE_PATH = self._orig_jobs_example_path

    def _write_jobs(self, jobs: list, *, as_default: bool = True) -> None:
        path = sch.JOBS_EXAMPLE_PATH if as_default else sch.JOBS_PATH
        path.write_text(json.dumps({"jobs": jobs}), encoding="utf-8")


JOB_HOURLY = {
    "name": "hourly-job",
    "module": "deal_alert_bot",
    "command": "python -m modules.deal_alert_bot.run",
    "cadence": "hourly",
    "at": "",
    "enabled": True,
}
JOB_DAILY = {
    "name": "daily-job",
    "module": "micro_saas",
    "command": "python -m modules.micro_saas.run",
    "cadence": "daily",
    "at": "06:00",
    "enabled": False,
}
JOB_WEEKLY = {
    "name": "weekly-job",
    "module": "digital_products",
    "command": "python -m modules.digital_products.run",
    "cadence": "weekly",
    "at": "mon 08:00",
    "enabled": True,
}
JOB_EVERY = {
    "name": "every-job",
    "module": "ecommerce_dropshipping",
    "command": "python -m modules.ecommerce_dropshipping.run",
    "cadence": "every",
    "interval_hours": 3,
    "at": "",
    "enabled": True,
}


class TestLoadJobs(JobsFileTestCase):
    def test_falls_back_to_example_when_jobs_json_missing(self):
        self._write_jobs([JOB_HOURLY])
        jobs = sch.load_jobs()
        self.assertEqual([j["name"] for j in jobs], ["hourly-job"])

    def test_prefers_jobs_json_over_example(self):
        self._write_jobs([JOB_HOURLY], as_default=True)
        self._write_jobs([JOB_DAILY], as_default=False)
        jobs = sch.load_jobs()
        self.assertEqual([j["name"] for j in jobs], ["daily-job"])

    def test_enabled_only_filters(self):
        self._write_jobs([JOB_HOURLY, JOB_DAILY])
        jobs = sch.load_jobs(enabled_only=True)
        self.assertEqual([j["name"] for j in jobs], ["hourly-job"])

    def test_skips_entries_without_a_name(self):
        self._write_jobs([JOB_HOURLY, {"cadence": "daily"}, "not a dict"])
        jobs = sch.load_jobs()
        self.assertEqual([j["name"] for j in jobs], ["hourly-job"])


class TestParseAt(unittest.TestCase):
    def test_hourly_ignores_at(self):
        self.assertEqual(sch._parse_at("hourly", ""), (0, 0, None))

    def test_daily_default_midnight(self):
        self.assertEqual(sch._parse_at("daily", ""), (0, 0, None))

    def test_daily_explicit_time(self):
        self.assertEqual(sch._parse_at("daily", "06:30"), (6, 30, None))

    def test_weekly_valid(self):
        self.assertEqual(sch._parse_at("weekly", "mon 08:00"), (8, 0, 0))
        self.assertEqual(sch._parse_at("weekly", "sun 23:59"), (23, 59, 6))

    def test_weekly_missing_day_raises(self):
        with self.assertRaises(ValueError):
            sch._parse_at("weekly", "08:00")

    def test_weekly_bad_day_raises(self):
        with self.assertRaises(ValueError):
            sch._parse_at("weekly", "someday 08:00")

    def test_unknown_cadence_raises(self):
        with self.assertRaises(ValueError):
            sch._parse_at("monthly", "")


class TestIntervalHours(unittest.TestCase):
    def test_default_is_one(self):
        self.assertEqual(sch._interval_hours({}), 1)

    def test_valid_range(self):
        self.assertEqual(sch._interval_hours({"interval_hours": 23}), 23)

    def test_too_low_raises(self):
        with self.assertRaises(ValueError):
            sch._interval_hours({"interval_hours": 0})

    def test_too_high_raises(self):
        with self.assertRaises(ValueError):
            sch._interval_hours({"interval_hours": 24})


class TestToCronLine(unittest.TestCase):
    def test_hourly(self):
        line = sch.to_cron_line(JOB_HOURLY)
        self.assertTrue(line.startswith("0 * * * * cd "))
        self.assertIn("data/logs/hourly-job.log", line)
        self.assertIn(JOB_HOURLY["command"], line)
        self.assertIn("# hourly-job", line)

    def test_daily(self):
        line = sch.to_cron_line(JOB_DAILY)
        self.assertTrue(line.startswith("0 6 * * * cd "))

    def test_weekly_monday(self):
        line = sch.to_cron_line(JOB_WEEKLY)
        # mon -> cron dow 1
        self.assertTrue(line.startswith("0 8 * * 1 cd "))

    def test_every_n_hours(self):
        line = sch.to_cron_line(JOB_EVERY)
        self.assertTrue(line.startswith("0 */3 * * * cd "))


class TestIsDue(unittest.TestCase):
    NOW = datetime(2026, 9, 2, 12, 0, 0, tzinfo=timezone.utc)  # a Wednesday

    def test_hourly_never_run_is_due(self):
        self.assertTrue(sch._is_due(JOB_HOURLY, self.NOW, None))

    def test_hourly_run_recently_not_due(self):
        last = self.NOW - timedelta(minutes=10)
        self.assertFalse(sch._is_due(JOB_HOURLY, self.NOW, last))

    def test_hourly_run_over_an_hour_ago_is_due(self):
        last = self.NOW - timedelta(minutes=60)
        self.assertTrue(sch._is_due(JOB_HOURLY, self.NOW, last))

    def test_every_respects_interval(self):
        last = self.NOW - timedelta(hours=2)
        self.assertFalse(sch._is_due(JOB_EVERY, self.NOW, last))  # needs 3h
        last = self.NOW - timedelta(hours=3)
        self.assertTrue(sch._is_due(JOB_EVERY, self.NOW, last))

    def test_daily_before_scheduled_time_not_due(self):
        job = {**JOB_DAILY, "enabled": True}
        early = self.NOW.replace(hour=5, minute=0)
        self.assertFalse(sch._is_due(job, early, None))

    def test_daily_after_scheduled_time_never_run_is_due(self):
        job = {**JOB_DAILY, "enabled": True}
        late = self.NOW.replace(hour=7, minute=0)
        self.assertTrue(sch._is_due(job, late, None))

    def test_daily_already_run_today_not_due(self):
        job = {**JOB_DAILY, "enabled": True}
        late = self.NOW.replace(hour=7, minute=0)
        last = self.NOW.replace(hour=6, minute=1)
        self.assertFalse(sch._is_due(job, late, last))

    def test_daily_run_yesterday_is_due_today(self):
        job = {**JOB_DAILY, "enabled": True}
        late = self.NOW.replace(hour=7, minute=0)
        last = late - timedelta(days=1)
        self.assertTrue(sch._is_due(job, late, last))

    def test_weekly_wrong_day_not_due(self):
        # self.NOW is a Wednesday (weekday()==2); job wants Monday (dow 0)
        self.assertFalse(sch._is_due(JOB_WEEKLY, self.NOW, None))

    def test_weekly_right_day_after_time_is_due(self):
        monday = datetime(2026, 8, 31, 9, 0, tzinfo=timezone.utc)  # a Monday
        self.assertTrue(sch._is_due(JOB_WEEKLY, monday, None))

    def test_weekly_right_day_before_time_not_due(self):
        monday_early = datetime(2026, 8, 31, 7, 0, tzinfo=timezone.utc)
        self.assertFalse(sch._is_due(JOB_WEEKLY, monday_early, None))

    def test_weekly_already_run_this_week_not_due(self):
        monday = datetime(2026, 8, 31, 9, 0, tzinfo=timezone.utc)
        last_run = monday.replace(hour=8, minute=1)
        self.assertFalse(sch._is_due(JOB_WEEKLY, monday, last_run))


class TestCrontabCommands(JobsFileTestCase):
    def test_install_writes_marker_block_for_enabled_jobs_only(self):
        self._write_jobs([JOB_HOURLY, JOB_DAILY])  # only hourly-job is enabled
        with patch.object(sch, "_read_crontab", return_value="# existing line\n"):
            with patch.object(sch, "_write_crontab") as write_mock:
                self.assertEqual(sch.cmd_install(), 0)
        written = write_mock.call_args[0][0]
        self.assertIn("# existing line", written)
        self.assertIn(sch.MARKER_BEGIN, written)
        self.assertIn(sch.MARKER_END, written)
        self.assertIn("hourly-job", written)
        self.assertNotIn("daily-job", written)

    def test_install_replaces_previous_block_not_duplicates(self):
        self._write_jobs([JOB_HOURLY])
        old_block = "\n".join(
            [sch.MARKER_BEGIN, "0 * * * * old-stale-line", sch.MARKER_END]
        )
        with patch.object(sch, "_read_crontab", return_value=old_block):
            with patch.object(sch, "_write_crontab") as write_mock:
                sch.cmd_install()
        written = write_mock.call_args[0][0]
        self.assertNotIn("old-stale-line", written)
        self.assertEqual(written.count(sch.MARKER_BEGIN), 1)

    def test_install_with_no_enabled_jobs_returns_error_and_does_not_write(self):
        self._write_jobs([JOB_DAILY])  # disabled
        with patch.object(sch, "_write_crontab") as write_mock:
            self.assertEqual(sch.cmd_install(), 1)
        write_mock.assert_not_called()

    def test_uninstall_removes_marker_block(self):
        block = "\n".join(
            ["# keep me", sch.MARKER_BEGIN, "0 * * * * job-line", sch.MARKER_END]
        )
        with patch.object(sch, "_read_crontab", return_value=block):
            with patch.object(sch, "_write_crontab") as write_mock:
                self.assertEqual(sch.cmd_uninstall(), 0)
        written = write_mock.call_args[0][0]
        self.assertIn("# keep me", written)
        self.assertNotIn("job-line", written)
        self.assertNotIn(sch.MARKER_BEGIN, written)


class TestSchedulerState(unittest.TestCase):
    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as d:
            orig = sch.STATE_FILE
            sch.STATE_FILE = Path(d) / "state.json"
            try:
                self.assertEqual(sch._load_state(), {})
                sch._save_state({"job-a": "2026-01-01T00:00:00+00:00"})
                self.assertEqual(
                    sch._load_state(), {"job-a": "2026-01-01T00:00:00+00:00"}
                )
            finally:
                sch.STATE_FILE = orig

    def test_malformed_state_file_is_empty(self):
        with tempfile.TemporaryDirectory() as d:
            orig = sch.STATE_FILE
            sch.STATE_FILE = Path(d) / "state.json"
            sch.STATE_FILE.write_text("not json", encoding="utf-8")
            try:
                self.assertEqual(sch._load_state(), {})
            finally:
                sch.STATE_FILE = orig


class TestCli(unittest.TestCase):
    def test_no_args_prints_docstring(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = cli.main([])
        self.assertEqual(code, 0)
        self.assertIn("python -m orchestrator init", buf.getvalue())

    def test_unknown_command(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = cli.main(["bogus"])
        self.assertEqual(code, 1)
        self.assertIn("Unknown command: bogus", buf.getvalue())

    def test_demo_dispatches_to_seed(self):
        with patch("orchestrator.demo.seed") as seed_mock:
            code = cli.main(["demo"])
        self.assertEqual(code, 0)
        seed_mock.assert_called_once()

    def test_dashboard_dispatches_with_rest_args(self):
        with patch("orchestrator.dashboard.main", return_value=0) as dash_mock:
            code = cli.main(["dashboard", "--port", "9999"])
        self.assertEqual(code, 0)
        dash_mock.assert_called_once_with(["--port", "9999"])

    def test_scheduler_dispatches_with_rest_args(self):
        with patch("orchestrator.scheduler.main", return_value=0) as sched_mock:
            code = cli.main(["scheduler", "list"])
        self.assertEqual(code, 0)
        sched_mock.assert_called_once_with(["list"])


class TestCmdInit(TempDatabaseTestCase):
    def test_prints_db_path_and_modules(self):
        # cli.py imports its own DB_PATH from paths at load time, separate
        # from database.DB_PATH (already patched by TempDatabaseTestCase) -
        # both need patching so the printed path matches where init_db()
        # actually writes.
        with patch.object(cli, "DB_PATH", db.DB_PATH):
            buf = io.StringIO()
            with redirect_stdout(buf):
                code = cli.main(["init"])
        self.assertEqual(code, 0)
        out = buf.getvalue()
        self.assertIn(str(db.DB_PATH), out)
        for name in MODULES:
            self.assertIn(name, out)


class TestCmdDoctor(unittest.TestCase):
    def test_reports_missing_when_absent(self):
        with tempfile.TemporaryDirectory() as d:
            fake_env = Path(d) / ".env"
            fake_db = Path(d) / "orchestrator.db"
            fake_jobs = Path(d) / "jobs.json"
            with (
                patch.object(cli, "ENV_PATH", fake_env),
                patch.object(cli, "DB_PATH", fake_db),
                patch.object(cli, "JOBS_PATH", fake_jobs),
                patch.object(cli.config, "has", return_value=False),
            ):
                buf = io.StringIO()
                with redirect_stdout(buf):
                    code = cli.main(["doctor"])
        self.assertEqual(code, 0)
        out = buf.getvalue()
        self.assertIn("MISSING", out)
        self.assertIn("not created yet", out)
        self.assertIn("using jobs.example.json defaults", out)
        self.assertIn("unset", out)

    def test_reports_found_when_present(self):
        with tempfile.TemporaryDirectory() as d:
            fake_env = Path(d) / ".env"
            fake_env.write_text("", encoding="utf-8")
            fake_db = Path(d) / "orchestrator.db"
            fake_db.write_text("", encoding="utf-8")
            fake_jobs = Path(d) / "jobs.json"
            fake_jobs.write_text("{}", encoding="utf-8")
            with (
                patch.object(cli, "ENV_PATH", fake_env),
                patch.object(cli, "DB_PATH", fake_db),
                patch.object(cli, "JOBS_PATH", fake_jobs),
                patch.object(cli.config, "has", return_value=True),
            ):
                buf = io.StringIO()
                with redirect_stdout(buf):
                    code = cli.main(["doctor"])
        self.assertEqual(code, 0)
        out = buf.getvalue()
        self.assertIn("found", out)
        self.assertNotIn("MISSING", out)
        self.assertIn("set ", out)


class TestDashboardHelpers(unittest.TestCase):
    def test_fmt_money(self):
        self.assertEqual(dashboard._fmt_money(1234.5), "$1,234.50")
        self.assertEqual(dashboard._fmt_money(0), "$0.00")

    def test_esc_escapes_html(self):
        self.assertEqual(dashboard._esc("<script>"), "&lt;script&gt;")

    def test_esc_handles_none(self):
        self.assertEqual(dashboard._esc(None), "")


class TestRenderPage(TempDatabaseTestCase):
    def test_includes_seeded_data(self):
        db.record_earning("deal_alert_bot", 4.20, source="amazon")
        db.set_status("deal_alert_bot", "ok", "Scanned 214 listings")
        db.add_review_item("micro_saas", "Refund on charge ch_1")
        page = dashboard.render_page()
        self.assertIn("Deal-Alert Bot", page)
        self.assertIn("$4.20", page)
        self.assertIn("Scanned 214 listings", page)
        self.assertIn("Refund on charge ch_1", page)
        self.assertIn("awaiting review", page)

    def test_escapes_malicious_content(self):
        db.add_review_item(
            "deal_alert_bot",
            "<script>alert(1)</script>",
            description="<img src=x onerror=alert(2)>",
        )
        page = dashboard.render_page()
        self.assertNotIn("<script>alert(1)</script>", page)
        self.assertIn("&lt;script&gt;", page)
        self.assertNotIn("<img src=x", page)

    def test_empty_states_render(self):
        page = dashboard.render_page()
        self.assertIn("Nothing awaiting review", page)
        self.assertIn("No activity logged yet", page)


class TestDashboardHandler(TempDatabaseTestCase):
    def setUp(self):
        super().setUp()
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), dashboard.Handler)
        self.port = self.server.server_address[1]
        self._thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self._thread.start()
        self.addCleanup(self.server.shutdown)
        self.addCleanup(self.server.server_close)

    def _get(self, path: str):
        return urllib.request.urlopen(f"http://127.0.0.1:{self.port}{path}", timeout=5)

    def test_homepage_returns_200_html(self):
        resp = self._get("/")
        self.assertEqual(resp.status, 200)
        self.assertIn("text/html", resp.headers["Content-Type"])
        self.assertIn(b"Income Orchestrator", resp.read())

    def test_api_overview_returns_json(self):
        resp = self._get("/api/overview")
        self.assertEqual(resp.status, 200)
        data = json.loads(resp.read())
        self.assertEqual(len(data["modules"]), len(MODULES))
        self.assertIn("total_earnings", data["totals"])

    def test_unknown_path_is_404(self):
        try:
            self._get("/nope")
            self.fail("expected an HTTPError")
        except urllib.error.HTTPError as exc:
            self.assertEqual(exc.code, 404)

    def test_review_post_resolves_item_and_redirects(self):
        rid = db.add_review_item("deal_alert_bot", "Approve?")
        body = f"id={rid}&decision=approved".encode("ascii")
        req = urllib.request.Request(
            f"http://127.0.0.1:{self.port}/review", data=body, method="POST"
        )
        # Don't follow the redirect automatically; just confirm the 303 + resolution.
        opener = urllib.request.build_opener(urllib.request.HTTPRedirectHandler)
        opener.open(req, timeout=5)
        self.assertEqual(db.pending_reviews(), [])


if __name__ == "__main__":
    unittest.main()
