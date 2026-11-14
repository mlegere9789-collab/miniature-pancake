"""The data in ledger/examples is the format specification by example. Each fact asserted here
can be read off that data by hand; the tools must agree with the hand count."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest

import regio  # noqa: E402

from . import support
from .support import TempDirCase

EXAMPLES = os.path.join(support.LEDGER_DIR, "examples")


def tool(*args):
    return subprocess.run([sys.executable] + list(args), capture_output=True, text=True)


class ExampleRegister(unittest.TestCase):
    def test_the_example_register_is_valid_with_both_engines(self):
        for engine in ("builtin", "jsonschema"):
            if engine == "jsonschema":
                try:
                    import jsonschema  # noqa: F401
                except ImportError:
                    continue
            result = tool(
                os.path.join(support.PARITY_TOOLS_DIR, "validate_register.py"),
                "--register",
                os.path.join(EXAMPLES, "register"),
                "--engine",
                engine,
                "--strict",
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("8 rows in 2 files, 0 errors, 0 warnings", result.stdout)

    def test_the_credited_row_carries_a_matching_seal(self):
        records, _ = regio.read_register(os.path.join(EXAMPLES, "register"))
        first = records[0]["row"]
        self.assertEqual(first["tier_now"], "Strong")
        self.assertEqual(first["evidence"]["spec_sha"], regio.spec_sha(first))


class ExamplePercent(TempDirCase):
    def compute(self):
        out = self.path("percent.json")
        credits = self.path("credits.jsonl")
        result = tool(
            os.path.join(support.PARITY_TOOLS_DIR, "percent.py"),
            "--register",
            os.path.join(EXAMPLES, "register"),
            "--evidence",
            os.path.join(EXAMPLES, "evidence"),
            "--legacy",
            os.path.join(support.LEDGER_DIR, "legacy.json"),
            "--out",
            out,
            "--credits-out",
            credits,
            "--now",
            "2026-11-18T10:30:00Z",
            "--strict",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with open(out) as handle:
            data = json.load(handle)
        with open(credits) as handle:
            lines = [json.loads(line) for line in handle]
        return data, lines

    def test_hand_count(self):
        data, credits = self.compute()
        # 8 rows, one alias: 7 rows count. Passing: MESH-0001 (both tests ok in the latest full run),
        # MESH-0005 (loop cut, Best through its speed test) and CURVES-0001 (a lite task run).
        self.assertEqual(
            (data["total"], data["passing"], data["best"], data["target_met"]),
            (7, 3, 1, 3),
        )
        self.assertEqual(data["register"]["aliases"], 1)
        self.assertEqual(
            [c["id"] for c in credits],
            ["D8-BLENDER-MESH-0001", "D8-BLENDER-MESH-0005", "D8-RHINO-CURVES-0001"],
        )
        self.assertEqual([c["tier"] for c in credits], ["Strong", "Best", "Strong"])
        # the three rows that do not pass, and why
        self.assertEqual(
            data["not_passing_by_reason"],
            {"conflicting": 1, "failing": 1, "no-tests": 1, "vanished": 1},
        )
        self.assertEqual(data["flaky_tests"]["ids"], ["mesh.extrude.region"])
        self.assertEqual(data["vanished_tests"]["ids"], ["mesh.inset.thickness"])
        self.assertEqual(data["failing_tests"]["ids"], ["mesh.bevel.segments"])

    def test_programs_factories_waves(self):
        data, _ = self.compute()
        self.assertEqual(
            {k: (v["passing"], v["total"]) for k, v in data["by_program"].items()},
            {"blender": (2, 5), "grasshopper": (1, 1), "rhino": (1, 2)},
        )
        self.assertEqual(
            {k: (v["passing"], v["total"]) for k, v in data["by_factory"].items()},
            {"kernel-geometry": (1, 2), "mesh-subd-sculpt": (2, 5)},
        )
        self.assertEqual(
            {k: (v["passing"], v["total"]) for k, v in data["by_wave"].items()},
            {"1": (2, 5), "2": (1, 2)},
        )

    def test_register_and_evidence_agree_except_for_rows_ci_has_not_written_yet(self):
        data, _ = self.compute()
        self.assertEqual(data["unproven_claims"]["count"], 0)
        self.assertEqual(data["register"]["rows_passing_but_not_yet_written"], 2)
        # seven counted rows (the alias is not counted): one written as Strong, six as None
        self.assertEqual(
            data["register"]["tiers_written_in_register"], {"None": 6, "Strong": 1}
        )

    def test_credit_times_come_from_the_runs(self):
        _, credits = self.compute()
        times = {c["id"]: c["at"] for c in credits}
        # MESH-0001 passed in both full runs: its streak began at the first one
        self.assertEqual(times["D8-BLENDER-MESH-0001"], "2026-11-18T08:00:00Z")
        self.assertEqual(times["D8-RHINO-CURVES-0001"], "2026-11-18T10:00:00Z")

    def test_legacy_numbers_sit_beside_the_register_number(self):
        data, _ = self.compute()
        self.assertEqual(
            (data["legacy"]["kernel_pct"], data["legacy"]["app_pct"]), (79.3, 86.5)
        )
        self.assertAlmostEqual(
            data["percent"], 42.857, places=3
        )  # 3 of 7, not blended with the legacy numbers
        self.assertIn(
            "legacy parity map Kernel 79.3% App 86.5%", data["legacy"]["dual"]
        )


class ExampleWatchdog(unittest.TestCase):
    def report(self):
        result = tool(
            os.path.join(support.TOOLS_DIR, "watchdog.py"),
            "--ledger",
            EXAMPLES,
            "--claims-file",
            os.path.join(EXAMPLES, "claims", "claims.json"),
            "--now",
            "2026-11-18T09:30:00Z",
            "--total",
            "7",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_hand_count(self):
        report = self.report()
        found = sorted(
            (i["type"], i.get("agent"), i["action"]) for i in report["incidents"]
        )
        self.assertEqual(
            found,
            [
                (
                    "claim_expired",
                    "delta",
                    "split_task",
                ),  # claimed 08:30, nothing since: 60 minutes; two relaunches used up
                ("late_first_output", "delta", "split_task"),
                (
                    "no_change",
                    "beta",
                    "relaunch",
                ),  # three identical heartbeats 09:10, 09:18, 09:26
                ("no_heartbeat", "delta", "split_task"),
                (
                    "no_heartbeat",
                    "gamma",
                    "relaunch",
                ),  # last heartbeat 09:05: 25 minutes
            ],
        )
        self.assertEqual(
            {a["agent"]: a["status"] for a in report["agents"]},
            {"alpha": "ok", "beta": "stalled", "delta": "stalled", "gamma": "stalled"},
        )

    def test_the_healthy_agent_is_never_named(self):
        for incident in self.report()["incidents"]:
            self.assertNotEqual(incident.get("agent"), "alpha")


if __name__ == "__main__":
    unittest.main()
