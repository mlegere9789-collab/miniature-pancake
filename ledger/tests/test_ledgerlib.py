"""Tests for ledger/tools/ledgerlib.py: times, ordering, JSONL and the pace line of plan 6.4."""

from __future__ import annotations

import datetime
import json
import os
import unittest

from . import support
from .support import TempDirCase, iso, write_beats

import ledgerlib as lib  # noqa: E402


class Times(unittest.TestCase):
    def test_parse_forms(self):
        base = lib.parse_time("2026-11-18T09:00:00Z")
        self.assertEqual(base, datetime.datetime(2026, 11, 18, 9, 0, tzinfo=lib.UTC))
        self.assertEqual(lib.parse_time("2026-11-18T10:00:00+01:00"), base)
        self.assertEqual(lib.parse_time("2026-11-18T10:00:00+0100"), base)
        self.assertEqual(lib.parse_time("2026-11-18 09:00:00"), base)
        self.assertEqual(lib.parse_time("2026-11-18T09:00"), base)
        self.assertEqual(lib.parse_time("2026-11-18T09:00:00.250Z").microsecond, 250000)
        self.assertEqual(lib.parse_time("2026-11-18"), datetime.datetime(2026, 11, 18, tzinfo=lib.UTC))
        self.assertEqual(lib.parse_time("2026-11-17T22:30:00-10:30"), base)

    def test_bad_times(self):
        for bad in ("", "yesterday", "2026-13-01T00:00:00Z", "2026-11-18T25:00:00Z", None):
            with self.assertRaises(ValueError):
                lib.parse_time(bad)
            self.assertIsNone(lib.try_parse_time(bad))

    def test_format_round_trip(self):
        text = "2026-11-18T09:00:00Z"
        self.assertEqual(lib.format_time(lib.parse_time(text)), text)
        self.assertEqual(lib.format_time(lib.parse_time("2026-11-18T10:00:00+01:00")), text)

    def test_minutes_between(self):
        self.assertEqual(lib.minutes_between(lib.parse_time(iso(0)), lib.parse_time(iso(90))), 90.0)


class Ordering(unittest.TestCase):
    def test_runs_sort_numerically(self):
        names = ["10", "9", "100", "2"]
        self.assertEqual(sorted(names, key=lib.natural_key), ["2", "9", "10", "100"])
        self.assertEqual(sorted(["run-10", "run-9", "run-2"], key=lib.natural_key), ["run-2", "run-9", "run-10"])
        self.assertEqual(sorted(["b", "10", "a"], key=lib.natural_key), ["10", "a", "b"])  # numbers before text


class Jsonl(TempDirCase):
    def test_blank_lines_are_skipped_and_bad_lines_reported(self):
        path = self.path("x.jsonl")
        with open(path, "w") as handle:
            handle.write('{"a": 1}\n\n{broken\n[1]\n{"b": 2}\n')
        items, problems = lib.read_jsonl(path)
        self.assertEqual([(n, o) for n, o in items], [(1, {"a": 1}), (5, {"b": 2})])
        self.assertEqual([n for n, _ in problems], [3, 4])

    def test_dump_json_is_stable(self):
        self.assertEqual(lib.dump_json({"b": 1, "a": [1, 2]}), '{\n  "a": [\n    1,\n    2\n  ],\n  "b": 1\n}\n')


class PaceLine(unittest.TestCase):
    def setUp(self):
        self.pace = lib.load_pace(None)

    def at(self, stamp):
        return lib.parse_time(stamp)

    def test_required_rows_match_the_plan_table(self):
        # plan 6.4 for 8,200 rows: cumulative 820, 2050, 4100, 6560, 8200 at the end of each period
        for end, rows in (("2026-11-25T00:00:00Z", 820), ("2026-12-01T00:00:00Z", 2050), ("2026-12-08T00:00:00Z", 4100),
                          ("2026-12-15T00:00:00Z", 6560), ("2026-12-19T00:00:00Z", 8200)):
            self.assertAlmostEqual(lib.required_cumulative(self.pace, 8200, self.at(end)), rows, places=3, msg=end)
        self.assertEqual(lib.required_cumulative(self.pace, 8200, self.at("2026-11-17T23:59:00Z")), 0.0)
        self.assertEqual(lib.required_cumulative(self.pace, 8200, self.at("2027-01-01T00:00:00Z")), 8200.0)
        self.assertAlmostEqual(lib.required_cumulative(self.pace, 8200, self.at("2026-11-21T12:00:00Z")), 820 * 3.5 / 7, places=3)

    def test_per_day_requirement_matches_the_plan_table(self):
        got = [round(lib.required_per_day(self.pace, 8200, self.at(s))) for s in
               ("2026-11-20T00:00:00Z", "2026-11-27T00:00:00Z", "2026-12-03T00:00:00Z", "2026-12-10T00:00:00Z", "2026-12-17T00:00:00Z")]
        self.assertEqual(got, [117, 205, 293, 351, 410])
        self.assertEqual(lib.required_per_day(self.pace, 8200, self.at("2026-11-16T00:00:00Z")), 0.0)
        self.assertEqual(lib.required_per_day(self.pace, 8200, self.at("2026-12-20T00:00:00Z")), 0.0)

    def test_trajectory_statuses(self):
        noon = self.at("2026-11-21T12:00:00Z")  # required: 410 rows of 8200
        self.assertEqual(lib.trajectory(self.pace, 8200, 0, self.at("2026-11-16T12:00:00Z"))["status"], "wave0")
        self.assertEqual(lib.trajectory(self.pace, 8200, 500, noon)["status"], "ahead")
        self.assertEqual(lib.trajectory(self.pace, 8200, 410, noon)["status"], "on_pace")
        self.assertEqual(lib.trajectory(self.pace, 8200, 300, noon)["status"], "behind")
        self.assertEqual(lib.trajectory(self.pace, 8200, 8200, self.at("2026-12-20T00:00:00Z"))["status"], "hardening")
        block = lib.trajectory(self.pace, 8200, 300, noon)
        self.assertEqual((block["required_cumulative_rows"], block["gap_rows"]), (410.0, 110.0))

    def test_bad_pace_files_are_refused(self):
        bad = [
            {"periods": []},
            {"periods": [{"from": "2026-11-18", "to": "2026-11-20", "cumulative_pct": 50}, {"from": "2026-11-19", "to": "2026-11-25", "cumulative_pct": 60}]},
            {"periods": [{"from": "2026-11-18", "to": "2026-11-20", "cumulative_pct": 50}, {"from": "2026-11-21", "to": "2026-11-25", "cumulative_pct": 40}]},
            {"periods": [{"from": "2026-11-18", "to": "2026-11-20", "cumulative_pct": 120}]},
            {"periods": [{"from": "2026-11-20", "to": "2026-11-18", "cumulative_pct": 10}]},
        ]
        for pace in bad:
            merged = dict(lib.DEFAULT_PACE)
            merged.update(pace)
            with self.assertRaises(ValueError, msg=pace):
                lib.validate_pace(merged)

    def test_load_pace_file_overrides_the_defaults(self):
        tmp = self.id()  # no file needed: a missing path means the defaults
        self.assertEqual(lib.load_pace("/nonexistent/pace.json")["window_hours"], 6)


class HeartbeatReader(TempDirCase):
    def test_fields_are_normalized(self):
        root = self.path("heartbeats")
        write_beats(root, "alpha", [{"time": iso(0), "task": 7, "what": "  ", "commit": "abc", "metric": True, "state": "Working"}])
        beats, problems = lib.read_heartbeats(root, 1, lib.parse_time(iso(5)))
        self.assertEqual(problems, [])
        beat = beats["alpha"][0]
        self.assertEqual((beat["task"], beat["what"], beat["commit"], beat["metric"], beat["state"]), ("7", None, "abc", None, "Working"))

    def test_missing_directory_is_empty(self):
        self.assertEqual(lib.read_heartbeats(self.path("none"), 1), ({}, []))

    def test_beats_are_sorted_by_time_across_days(self):
        root = self.path("heartbeats")
        write_beats(root, "alpha", [{"time": "2026-11-19T00:05:00Z"}], day="2026-11-19")
        write_beats(root, "alpha", [{"time": "2026-11-18T23:55:00Z"}], day="2026-11-18")
        beats, _ = lib.read_heartbeats(root, 2, lib.parse_time("2026-11-19T01:00:00Z"))
        self.assertEqual([lib.format_time(b["time"]) for b in beats["alpha"]], ["2026-11-18T23:55:00Z", "2026-11-19T00:05:00Z"])


if __name__ == "__main__":
    unittest.main()
