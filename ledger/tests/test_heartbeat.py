"""Tests for ledger/tools/heartbeat.py: the lines it writes are the lines the watchdog reads."""

from __future__ import annotations

import io
import json
import os
import subprocess
import sys
import unittest

from . import support
from .support import TempDirCase, iso

import heartbeat  # noqa: E402
import ledgerlib  # noqa: E402


class Heartbeat(TempDirCase):
    def write(self, *args):
        out = io.StringIO()
        code = heartbeat.run(["--ledger", self.path("ledger")] + list(args), out=out)
        return code, out.getvalue()

    def test_appends_a_line_the_reader_understands(self):
        code, text = self.write("--agent", "alpha", "--what", "generated 10 rows", "--task", "T-1", "--commit", "abc1234",
                                "--evidence", "validate: 0 errors", "--metric", "10", "--now", iso(5))
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(text)["commit"], "abc1234")
        beats, problems = ledgerlib.read_heartbeats(self.path("ledger", "heartbeats"), 1, ledgerlib.parse_time(iso(6)))
        self.assertEqual(problems, [])
        beat = beats["alpha"][0]
        self.assertEqual((beat["task"], beat["what"], beat["commit"], beat["evidence"], beat["metric"]), ("T-1", "generated 10 rows", "abc1234", "validate: 0 errors", 10.0))
        self.assertEqual(ledgerlib.format_time(beat["time"]), iso(5))

    def test_lines_are_appended_not_replaced_and_land_in_the_days_folder(self):
        self.write("--agent", "alpha", "--what", "one", "--commit", "c1", "--now", iso(0))
        self.write("--agent", "alpha", "--what", "two", "--commit", "c2", "--now", iso(10))
        path = self.path("ledger", "heartbeats", iso(0)[:10], "alpha.jsonl")
        with open(path) as handle:
            self.assertEqual([json.loads(line)["what"] for line in handle], ["one", "two"])

    def test_state_and_waiting_on(self):
        self.write("--agent", "alpha", "--what", "waiting", "--state", "waiting", "--waiting-on", "ci-4711", "--commit", "c1", "--now", iso(0))
        beats, _ = ledgerlib.read_heartbeats(self.path("ledger", "heartbeats"), 1, ledgerlib.parse_time(iso(1)))
        self.assertEqual((beats["alpha"][0]["state"], beats["alpha"][0]["waiting_on"]), ("waiting", "ci-4711"))

    def test_refuses_bad_names_and_empty_messages(self):
        self.assertEqual(self.write("--agent", "bad name", "--what", "x")[0], 2)
        self.assertEqual(self.write("--agent", "alpha", "--what", "  ")[0], 2)
        self.assertEqual(self.write("--agent", "alpha", "--what", "x", "--now", "tomorrow")[0], 2)
        self.assertFalse(os.path.exists(self.path("ledger", "heartbeats")))

    def test_commit_defaults_to_head_of_the_current_directory(self):
        repo = self.path("repo")
        subprocess.run(["git", "init", "-q", repo], check=True)
        subprocess.run(["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@x", "commit", "--allow-empty", "-q", "-m", "x"], check=True)
        head = subprocess.run(["git", "-C", repo, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
        result = subprocess.run(
            [sys.executable, os.path.join(support.TOOLS_DIR, "heartbeat.py"), "--ledger", self.path("ledger"), "--agent", "alpha", "--what", "x", "--now", iso(0)],
            cwd=repo, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["commit"], head)


if __name__ == "__main__":
    unittest.main()
