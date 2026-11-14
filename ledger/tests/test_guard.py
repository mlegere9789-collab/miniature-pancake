"""Tests for ledger/tools/guard.py: only CI writes evidence, percent.json and credit fields."""

from __future__ import annotations

import copy
import io
import os
import subprocess
import sys
import unittest

from . import support
from .support import TempDirCase

import guard  # noqa: E402
import regio  # noqa: E402
from parity.tests.support import make_row, write_register  # noqa: E402

CI = "github-actions[bot]"
EVIDENCE = {"run_id": "7", "commit": "abc1234"}


class Paths(unittest.TestCase):
    def test_agents_cannot_write_evidence_or_percent(self):
        changed = ["ledger/evidence/43486663063/T-1.json", "ledger/percent.json", "ledger/credits.jsonl", "ledger/tasks/T-1.json", "parity/register/x.jsonl"]
        found = guard.check_paths(changed, "owner")
        self.assertEqual(len(found), 3)
        self.assertTrue(all("written by CI only" in line for line in found))

    def test_ci_may_write_them(self):
        self.assertEqual(guard.check_paths(["ledger/percent.json", "ledger/evidence/1/x.json"], CI), [])

    def test_other_ledger_files_are_free(self):
        paths = ["ledger/heartbeats/2026-11-18/a.jsonl", "ledger/tasks/T-1.json", "ledger/incidents/x.md", "ledger/usage/2026-11-18.json", "ledger/tools/x.py"]
        self.assertEqual(guard.check_paths(paths, "owner"), [])

    def test_gitkeep_placeholders_are_not_evidence(self):
        self.assertEqual(guard.check_paths(["ledger/evidence/.gitkeep"], "owner"), [])
        self.assertEqual(len(guard.check_paths(["ledger/evidence/.gitkeep.json"], "owner")), 1)

    def test_lookalike_names_are_not_protected(self):
        self.assertEqual(guard.check_paths(["ledger/evidence_notes.md", "ledger/percent.json.bak", "docs/ledger/percent.json"], "owner"), [])

    def test_allowed_actors_come_from_the_option_or_the_environment(self):
        self.assertEqual(guard.check_paths(["ledger/percent.json"], "ci-bot", "ci-bot, other"), [])
        old = os.environ.get("LEDGER_CI_ACTORS")
        os.environ["LEDGER_CI_ACTORS"] = "env-bot"
        try:
            self.assertEqual(guard.check_paths(["ledger/percent.json"], "env-bot"), [])
            self.assertEqual(len(guard.check_paths(["ledger/percent.json"], CI)), 1)  # the default is replaced, not extended
        finally:
            if old is None:
                del os.environ["LEDGER_CI_ACTORS"]
            else:
                os.environ["LEDGER_CI_ACTORS"] = old


class Rows(TempDirCase):
    def load(self, name, files):
        root = self.path(name)
        write_register(root, files)
        return guard._rows_by_id(root)

    def verdict(self, before_rows, after_rows, actor="owner"):
        base = self.load("base", {"blender/mesh.jsonl": before_rows})
        head = self.load("head_%d" % len(os.listdir(self.tmp)), {"blender/mesh.jsonl": after_rows})
        return guard.check_rows(base, head, actor)

    def test_new_rows_start_uncredited(self):
        self.assertEqual(self.verdict([], [make_row(1)]), [])
        self.assertEqual(self.verdict([], [make_row(1, status="in_progress")]), [])
        self.assertEqual(len(self.verdict([], [make_row(1, tier_now="Strong")])), 1)
        self.assertEqual(len(self.verdict([], [make_row(1, status="verified")])), 1)
        self.assertEqual(len(self.verdict([], [make_row(1, evidence=EVIDENCE)])), 1)

    def test_raising_a_tier_is_refused(self):
        row = make_row(1)
        credited = copy.deepcopy(row)
        credited["tier_now"] = "Strong"
        found = self.verdict([row], [credited])
        self.assertEqual(len(found), 1)
        self.assertIn("tier_now raised from None to Strong", found[0])

    def test_marking_verified_or_adding_evidence_is_refused(self):
        row = make_row(1)
        verified = dict(row, status="verified")
        self.assertIn("only CI may verify", self.verdict([row], [verified])[0])
        proofed = dict(row, evidence=EVIDENCE)
        self.assertIn("written by CI only", self.verdict([row], [proofed])[0])
        changed = dict(proofed, evidence=dict(EVIDENCE, run_id="8"))
        self.assertEqual(len(self.verdict([proofed], [changed])), 1)

    def test_lowering_a_credit_is_always_allowed(self):
        credited = make_row(1, tier_now="Strong", status="verified", evidence=EVIDENCE)
        reset = dict(credited, tier_now="None", status="open", evidence=None)
        self.assertEqual(self.verdict([credited], [reset]), [])
        self.assertEqual(self.verdict([credited], [dict(credited, tier_now="Basic", status="in_progress", evidence=None)]), [])

    def test_unchanged_credit_and_other_edits_are_fine(self):
        credited = make_row(1, tier_now="Strong", status="verified", evidence=EVIDENCE)
        edited = dict(credited, notes="reworded")
        self.assertEqual(self.verdict([credited], [edited]), [])

    def test_ci_may_credit(self):
        row = make_row(1)
        credited = dict(row, tier_now="Strong", status="verified", evidence=EVIDENCE)
        self.assertEqual(self.verdict([row], [credited], actor=CI), [])

    def test_a_missing_base_register_means_every_row_is_new(self):
        head = self.load("head", {"blender/mesh.jsonl": [make_row(1, tier_now="Best")]})
        self.assertEqual(len(guard.check_rows(guard._rows_by_id(self.path("no-such-dir")), head, "owner")), 1)


class CommandLine(TempDirCase):
    def tool(self, *args, stdin=""):
        return subprocess.run([sys.executable, os.path.join(support.TOOLS_DIR, "guard.py")] + list(args), input=stdin, capture_output=True, text=True)

    def test_paths_via_stdin(self):
        clean = self.tool("paths", "--actor", "owner", stdin="parity/tools/x.py\nledger/tasks/T-1.json\n")
        self.assertEqual(clean.returncode, 0, clean.stdout)
        dirty = self.tool("paths", "--actor", "owner", stdin="ledger/percent.json\n")
        self.assertEqual(dirty.returncode, 1)
        self.assertIn("guard: ledger/percent.json", dirty.stdout)
        self.assertEqual(self.tool("paths", "--actor", CI, stdin="ledger/percent.json\n").returncode, 0)

    def test_rows_via_directories(self):
        write_register(self.path("base"), {"blender/mesh.jsonl": [make_row(1)]})
        write_register(self.path("head"), {"blender/mesh.jsonl": [make_row(1, tier_now="Strong")]})
        result = self.tool("rows", "--actor", "owner", "--base", self.path("base"), "--head", self.path("head"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("only CI may credit", result.stdout)


if __name__ == "__main__":
    unittest.main()
