"""Tests for parity/tools/percent.py: the row pass rule, the anti-gaming rules and the outputs."""

from __future__ import annotations

import copy
import io
import json
import os
import subprocess
import sys
import unittest

from . import support
from .support import TempDirCase, make_row, write_jsonl, write_register, write_run

import ledgerlib  # noqa: E402
import percent  # noqa: E402
import regio  # noqa: E402

NOW = ledgerlib.parse_time("2026-11-20T12:00:00Z")


def ids_of(row):
    return [a["test_id"] for a in row["acceptance"]]


class PercentBase(TempDirCase):
    def setUp(self):
        super().setUp()
        self.register = self.path("register")
        self.evidence = self.path("evidence")
        os.makedirs(self.evidence)

    def compute(self, files, previous=None, legacy=None, pace=None, extra_checks=()):
        shutil_rmtree(self.register)
        write_register(self.register, files)
        records, _ = regio.read_register(self.register)
        evidence = percent.load_evidence(self.evidence, extra_checks)
        return percent.compute(records, evidence, NOW, previous, legacy, pace, "run-1", "abc1234")

    def verdict(self, row, files=None):
        result, credits = self.compute(files or {"blender/mesh.jsonl": [row]})
        evidence = percent.load_evidence(self.evidence)
        return percent.evaluate_row(row, percent.judge_tests(evidence)), result, credits


def shutil_rmtree(path):
    import shutil

    shutil.rmtree(path, ignore_errors=True)


class RowPassRule(PercentBase):
    def test_row_passes_only_when_every_acceptance_test_is_ok(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}, {"test_id": "t.b"}])
        write_run(self.evidence, "100", checks=[("t.a", "ok"), ("t.b", "ok")])
        verdict, result, credits = self.verdict(row)
        self.assertTrue(verdict["passing"])
        self.assertEqual((result["total"], result["passing"]), (1, 1))
        self.assertEqual([c["id"] for c in credits], [row["id"]])

    def test_one_missing_test_blocks_the_row(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}, {"test_id": "t.b"}])
        write_run(self.evidence, "100", checks=[("t.a", "ok")])
        verdict, result, credits = self.verdict(row)
        self.assertFalse(verdict["passing"])
        self.assertEqual(verdict["reasons"], ["missing:t.b"])
        self.assertEqual((result["passing"], credits), (0, []))

    def test_failed_and_skipped_tests_block_the_row(self):
        for bad, kind in (("fail", "failing"), ("FAIL", "failing"), ("skip", "skipped"), ("pass", "failing"), ("", "failing")):
            row = make_row(1, acceptance=[{"test_id": "t.a"}])
            shutil_rmtree(self.evidence)
            write_run(self.evidence, "100", checks=[("t.a", bad)])
            verdict, _, _ = self.verdict(row)
            self.assertFalse(verdict["passing"], bad)
            self.assertEqual(verdict["reasons"], ["%s:t.a" % kind], bad)

    def test_ok_is_case_insensitive_but_nothing_else_counts(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}])
        write_run(self.evidence, "100", checks=[("t.a", "OK")])
        self.assertTrue(self.verdict(row)[0]["passing"])

    def test_a_row_without_tests_never_passes(self):
        row = make_row(1, acceptance=[])
        write_run(self.evidence, "100", checks=[("t.a", "ok")])
        verdict, result, _ = self.verdict(row)
        self.assertFalse(verdict["passing"])
        self.assertEqual(verdict["reasons"], ["no-tests"])
        self.assertEqual(result["not_passing_by_reason"], {"no-tests": 1})

    def test_task_evidence_json_counts(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}, {"test_id": "t.b"}])
        write_run(self.evidence, "100", tasks={"T-1": {"run_id": "100", "passed": ["t.a", "t.b"]}})
        self.assertTrue(self.verdict(row)[0]["passing"])

    def test_checks_and_task_evidence_must_agree(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}])
        write_run(self.evidence, "100", tasks={"T-1": {"passed": ["t.a"]}}, checks=[("t.a", "fail")])
        verdict = self.verdict(row)[0]
        self.assertEqual(verdict["reasons"], ["conflicting:t.a"])

    def test_the_suite_is_not_part_of_the_test_id(self):
        row = make_row(1, acceptance=[{"test_id": "kernel/t.a"}])
        write_run(self.evidence, "100", checks=[{"suite": "kernel", "test": "t.a", "name": "x", "result": "ok", "ms": 1}])
        self.assertFalse(self.verdict(row)[0]["passing"])

    def test_alias_rows_are_not_counted(self):
        a = make_row(1)
        b = make_row(2, alias_of=a["id"], acceptance=[])
        write_run(self.evidence, "100", checks=[(ids_of(a)[0], "ok")])
        _, result, _ = self.verdict(a, {"blender/mesh.jsonl": [a, b]})
        self.assertEqual((result["total"], result["passing"]), (1, 1))
        self.assertEqual(result["register"]["aliases"], 1)

    def test_duplicate_ids_count_once(self):
        a = make_row(1)
        write_run(self.evidence, "100", checks=[(ids_of(a)[0], "ok")])
        _, result, _ = self.verdict(a, {"blender/mesh.jsonl": [a, copy.deepcopy(a)]})
        self.assertEqual(result["total"], 1)
        self.assertEqual(result["register"]["duplicate_ids_ignored"], 1)

    def test_empty_register_is_zero_percent_not_an_error(self):
        result, credits = self.compute({})
        self.assertEqual((result["total"], result["passing"], result["percent"], credits), (0, 0, 0.0, []))


class Aggregation(PercentBase):
    def test_per_program_factory_and_wave(self):
        rows = [
            make_row(1, program="blender", factory="mesh-subd-sculpt", wave=1),
            make_row(2, program="blender", factory="mesh-subd-sculpt", wave=1),
            make_row(3, program="rhino", domain="curves", factory="kernel-geometry", wave=2),
            make_row(4, program="autocad", domain="draw", factory="cad-2d"),
        ]
        shared = make_row(5, program="rhino", domain="curves", factory="kernel-geometry", wave=2)
        shared["program"] = ["rhino", "grasshopper"]
        rows.append(shared)
        write_run(self.evidence, "100", checks=[(ids_of(rows[0])[0], "ok"), (ids_of(rows[2])[0], "ok"), (ids_of(shared)[0], "ok")])
        files = {
            "blender/mesh.jsonl": rows[:2],
            "rhino/curves.jsonl": [rows[2], shared],
            "autocad/draw.jsonl": [rows[3]],
        }
        result, _ = self.compute(files)
        self.assertEqual((result["total"], result["passing"]), (5, 3))
        self.assertEqual(result["percent"], 60.0)
        self.assertEqual(result["by_program"]["blender"]["total"], 2)
        self.assertEqual(result["by_program"]["blender"]["passing"], 1)
        self.assertEqual(result["by_program"]["rhino"], {"total": 2, "passing": 2, "best": 0, "target_met": 2, "percent": 100.0, "target_met_percent": 100.0})
        self.assertEqual(result["by_program"]["grasshopper"]["total"], 1)  # a shared row counts in each program
        self.assertEqual(result["by_program"]["autocad"]["passing"], 0)
        self.assertEqual(result["by_factory"]["mesh-subd-sculpt"]["total"], 2)
        self.assertEqual(result["by_factory"]["kernel-geometry"]["passing"], 2)
        self.assertEqual(result["by_wave"]["1"]["total"], 2)
        self.assertEqual(result["by_wave"]["2"]["passing"], 2)
        self.assertEqual(result["by_wave"]["unassigned"]["total"], 1)
        self.assertEqual(sum(b["total"] for b in result["by_factory"].values()), 5)
        self.assertEqual(sum(b["total"] for b in result["by_wave"].values()), 5)


class RunOrderAndAntiGaming(PercentBase):
    def one(self):
        return make_row(1, acceptance=[{"test_id": "t.a"}])

    def test_latest_run_wins_numerically(self):
        row = self.one()
        write_run(self.evidence, "9", checks=[("t.a", "fail")])
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        self.assertTrue(self.verdict(row)[0]["passing"])  # 10 is later than 9

    def test_a_later_failure_removes_the_credit(self):
        row = self.one()
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        write_run(self.evidence, "11", checks=[("t.a", "fail")])
        verdict = self.verdict(row)[0]
        self.assertFalse(verdict["passing"])
        self.assertEqual(verdict["reasons"], ["failing:t.a"])

    def test_flaky_same_run_blocks_crediting(self):
        row = self.one()
        write_run(self.evidence, "10", checks=[("t.a", "fail"), ("t.a", "ok")])
        verdict, result, _ = self.verdict(row)
        self.assertEqual(verdict["reasons"], ["conflicting:t.a"])
        self.assertEqual(result["flaky_tests"], {"count": 1, "ids": ["t.a"]})

    def test_flaky_rerun_of_the_same_commit_in_a_new_run_blocks_crediting(self):
        row = self.one()
        write_run(self.evidence, "10", manifest={"sha": "aaa1111"}, checks=[("t.a", "fail")])
        write_run(self.evidence, "11", manifest={"sha": "aaa1111"}, checks=[("t.a", "ok")])
        self.assertEqual(self.verdict(row)[0]["reasons"], ["conflicting:t.a"])

    def test_a_new_commit_that_passes_clears_the_flake(self):
        row = self.one()
        write_run(self.evidence, "10", manifest={"sha": "aaa1111"}, checks=[("t.a", "fail")])
        write_run(self.evidence, "11", manifest={"sha": "aaa1111"}, checks=[("t.a", "ok")])
        write_run(self.evidence, "12", manifest={"sha": "bbb2222"}, checks=[("t.a", "ok")])
        self.assertTrue(self.verdict(row)[0]["passing"])

    def test_deleted_test_vanishes_from_the_next_full_run(self):
        row = self.one()
        write_run(self.evidence, "10", manifest={"kind": "full"}, checks=[("t.a", "ok"), ("t.b", "ok")])
        write_run(self.evidence, "11", manifest={"kind": "full"}, checks=[("t.b", "ok")])
        verdict, result, _ = self.verdict(row)
        self.assertEqual(verdict["reasons"], ["vanished:t.a"])
        self.assertEqual(result["vanished_tests"], {"count": 1, "ids": ["t.a"]})

    def test_a_lite_run_does_not_void_tests_it_did_not_run(self):
        row = self.one()
        write_run(self.evidence, "10", manifest={"kind": "full"}, checks=[("t.a", "ok")])
        write_run(self.evidence, "11", manifest={"kind": "lite"}, checks=[("t.b", "ok")])
        self.assertTrue(self.verdict(row)[0]["passing"])

    def test_a_test_that_is_back_after_the_full_run_counts_again(self):
        row = self.one()
        write_run(self.evidence, "10", manifest={"kind": "full"}, checks=[("t.a", "ok")])
        write_run(self.evidence, "11", manifest={"kind": "full"}, checks=[("t.b", "ok")])
        write_run(self.evidence, "12", manifest={"kind": "lite"}, checks=[("t.a", "ok")])
        self.assertTrue(self.verdict(row)[0]["passing"])

    def test_no_full_run_means_no_vanish_rule(self):
        row = self.one()
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        write_run(self.evidence, "11", checks=[("t.b", "ok")])
        self.assertTrue(self.verdict(row)[0]["passing"])

    def test_adhoc_checks_are_the_newest_run(self):
        row = self.one()
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        loose = self.path("loose.jsonl")
        write_jsonl(loose, [{"test": "t.a", "result": "fail"}])
        result, _ = self.compute({"blender/mesh.jsonl": [row]}, extra_checks=[loose])
        self.assertEqual(result["passing"], 0)

    def test_weakening_the_acceptance_after_credit_voids_it(self):
        row = make_row(1, acceptance=[{"test_id": "t.a", "tolerance": {"volume_rel": 1e-6}}], tier_now="Strong", status="verified")
        row["evidence"] = {"run_id": "10", "commit": "abc1234", "spec_sha": regio.spec_sha(row)}
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        self.assertTrue(self.verdict(row)[0]["passing"])
        loosened = copy.deepcopy(row)
        loosened["acceptance"][0]["tolerance"]["volume_rel"] = 0.5
        verdict, result, _ = self.verdict(loosened)
        self.assertFalse(verdict["passing"])
        self.assertEqual(verdict["reasons"], ["spec-changed"])
        self.assertEqual(result["unproven_claims"]["count"], 1)  # still claims Strong
        shrunk = copy.deepcopy(row)
        shrunk["acceptance"] = [{"test_id": "t.other"}]
        self.assertFalse(self.verdict(shrunk)[0]["passing"])
        retargeted = copy.deepcopy(row)
        retargeted["target"] = "Best"
        self.assertFalse(self.verdict(retargeted)[0]["passing"])

    def test_register_claim_without_evidence_is_reported_not_credited(self):
        row = make_row(1, tier_now="Strong", status="verified", evidence={"run_id": "999", "commit": "abc1234"})
        result, credits = self.compute({"blender/mesh.jsonl": [row]})
        self.assertEqual((result["passing"], credits), (0, []))
        self.assertEqual(result["unproven_claims"]["count"], 1)
        self.assertEqual(result["unproven_claims"]["rows"][0]["claimed"], "Strong")

    def test_evidence_without_register_claim_is_pending_not_unproven(self):
        row = self.one()
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        result, _ = self.compute({"blender/mesh.jsonl": [row]})
        self.assertEqual(result["register"]["rows_passing_but_not_yet_written"], 1)
        self.assertEqual(result["unproven_claims"]["count"], 0)


class BestTier(PercentBase):
    def row(self, target="Best"):
        return make_row(1, target=target, acceptance=[{"test_id": "t.a"}], superiority_tests=["p.speed"])

    def test_best_needs_the_superiority_tests(self):
        write_run(self.evidence, "10", checks=[("t.a", "ok"), ("p.speed", "ok")])
        verdict = self.verdict(self.row())[0]
        self.assertEqual((verdict["tier"], verdict["target_met"]), ("Best", True))
        self.assertEqual(verdict["tests"], ["t.a", "p.speed"])

    def test_strong_without_superiority_is_passing_but_misses_a_best_target(self):
        write_run(self.evidence, "10", checks=[("t.a", "ok"), ("p.speed", "fail")])
        verdict, result, _ = self.verdict(self.row())
        self.assertEqual((verdict["passing"], verdict["tier"], verdict["target_met"]), (True, "Strong", False))
        self.assertEqual((result["passing"], result["best"], result["target_met"]), (1, 0, 0))

    def test_strong_target_is_met_at_strong(self):
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        verdict = self.verdict(self.row(target="Strong"))[0]
        self.assertEqual((verdict["tier"], verdict["target_met"]), ("Strong", True))


class CreditsAndReverts(PercentBase):
    def test_credit_time_comes_from_the_run_and_the_streak(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}])
        write_run(self.evidence, "10", manifest={"finished_at": "2026-11-18T09:00:00Z"}, checks=[("t.a", "ok")])
        write_run(self.evidence, "11", manifest={"finished_at": "2026-11-18T10:00:00Z"}, checks=[("t.a", "ok")])
        _, _, credits = self.verdict(row)
        self.assertEqual(credits[0]["at"], "2026-11-18T09:00:00Z")  # the streak began at run 10
        self.assertEqual(credits[0]["run"], "11")

    def test_a_failure_restarts_the_streak(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}])
        write_run(self.evidence, "10", manifest={"finished_at": "2026-11-18T09:00:00Z"}, checks=[("t.a", "ok")])
        write_run(self.evidence, "11", manifest={"finished_at": "2026-11-18T10:00:00Z", "sha": "x1"}, checks=[("t.a", "fail")])
        write_run(self.evidence, "12", manifest={"finished_at": "2026-11-18T11:00:00Z", "sha": "x2"}, checks=[("t.a", "ok")])
        _, _, credits = self.verdict(row)
        self.assertEqual(credits[0]["at"], "2026-11-18T11:00:00Z")

    def test_credit_without_run_times_uses_the_previous_time_then_now(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}])
        write_run(self.evidence, "10", checks=[("t.a", "ok")])
        files = {"blender/mesh.jsonl": [row]}
        _, credits = self.compute(files)
        self.assertEqual(credits[0]["at"], "2026-11-20T12:00:00Z")
        previous = {row["id"]: {"id": row["id"], "at": "2026-11-19T01:02:03Z"}}
        _, credits = self.compute(files, previous=previous)
        self.assertEqual(credits[0]["at"], "2026-11-19T01:02:03Z")

    def test_reverted_rows_are_listed_with_the_reason(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}])
        gone = make_row(2, acceptance=[{"test_id": "t.b"}])
        write_run(self.evidence, "10", checks=[("t.a", "fail")])
        previous = {
            row["id"]: {"id": row["id"], "at": "2026-11-19T01:02:03Z"},
            gone["id"]: {"id": gone["id"], "at": "2026-11-19T01:02:03Z"},
            "D8-BLENDER-MESH-9999": {"id": "D8-BLENDER-MESH-9999", "at": "2026-11-19T01:02:03Z"},
        }
        result, _ = self.compute({"blender/mesh.jsonl": [row, gone]}, previous=previous)
        reverted = {r["id"]: r["reasons"] for r in result["reverted"]["rows"]}
        self.assertEqual(reverted[row["id"]], ["failing:t.a"])
        self.assertEqual(reverted[gone["id"]], ["missing:t.b"])
        self.assertIn("row-removed", reverted["D8-BLENDER-MESH-9999"][0])


class LegacyAndTrajectory(PercentBase):
    def test_legacy_placeholder_without_a_file(self):
        result, _ = self.compute({"blender/mesh.jsonl": [make_row(1)]}, legacy=percent.load_legacy(None))
        legacy = result["legacy"]
        self.assertEqual((legacy["status"], legacy["kernel_pct"], legacy["app_pct"]), ("placeholder", None, None))
        self.assertIn("n/a", legacy["dual"])
        self.assertEqual(legacy["register_percent"], 0.0)

    def test_legacy_numbers_are_carried_not_credited(self):
        path = self.path("legacy.json")
        with open(path, "w") as handle:
            json.dump({"kernel_pct": 79.3, "app_pct": 86.5, "as_of_commit": "53a680b4"}, handle)
        result, _ = self.compute({"blender/mesh.jsonl": [make_row(1)]}, legacy=percent.load_legacy(path))
        legacy = result["legacy"]
        self.assertEqual(legacy["status"], "frozen")
        self.assertIn("Kernel 79.3% App 86.5%", legacy["dual"])
        self.assertIn("53a680b4", legacy["dual"])
        self.assertEqual(result["percent"], 0.0)  # the legacy map gives no Register credit

    def test_trajectory_block(self):
        rows = [make_row(i) for i in range(1, 11)]
        write_run(self.evidence, "10", checks=[(ids_of(r)[0], "ok") for r in rows[:4]])
        pace = ledgerlib.load_pace(None)
        result, _ = self.compute({"blender/mesh.jsonl": rows}, pace=pace)
        trajectory = result["trajectory"]
        self.assertEqual(trajectory["passing"], 4)
        self.assertEqual(trajectory["period"]["to"], "2026-11-24")
        # 20 Nov 12:00 is 2.5 of the 7 days of the first period: 10% of 10 rows * 2.5/7
        self.assertAlmostEqual(trajectory["required_cumulative_rows"], 10 * 0.10 * 2.5 / 7, places=1)
        self.assertEqual(trajectory["status"], "ahead")


class Malformed(PercentBase):
    def test_bad_lines_and_files_are_reported_and_never_credit(self):
        row = make_row(1, acceptance=[{"test_id": "t.a"}])
        directory = write_run(self.evidence, "10", checks=[("t.a", "ok")])
        with open(os.path.join(directory, "bad.jsonl"), "w") as handle:
            handle.write("{broken\n")
            handle.write(json.dumps({"test": "t.z"}) + "\n")  # no result
            handle.write("[1]\n")
        with open(os.path.join(directory, "T-9.json"), "w") as handle:
            handle.write("{broken")
        with open(os.path.join(directory, "T-8.json"), "w") as handle:
            json.dump({"task": "T-8", "run_id": "77", "passed": ["t.q"]}, handle)  # wrong run
        with open(os.path.join(directory, "T-7.json"), "w") as handle:
            json.dump({"task": "T-7", "passed": "t.q"}, handle)  # not a list
        with open(os.path.join(directory, "T-6.json"), "w") as handle:
            json.dump({"task": "T-6"}, handle)  # no lists
        with open(os.path.join(directory, "T-5.json"), "w") as handle:
            json.dump({"task": "other", "passed": ["t.q"]}, handle)  # task != file name
        result, _ = self.compute({"blender/mesh.jsonl": [row]})
        self.assertEqual(result["evidence"]["malformed_count"], 8)
        self.assertEqual(result["passing"], 1)  # the good line still counts
        self.assertEqual(result["evidence"]["tests"], 1)

    def test_bad_manifest_values(self):
        write_run(self.evidence, "10", manifest={"kind": "huge", "finished_at": "yesterday"}, checks=[("t.a", "ok")])
        evidence = percent.load_evidence(self.evidence)
        self.assertEqual(len(evidence.malformed), 2)


class CommandLine(TempDirCase):
    def setUp(self):
        super().setUp()
        self.register = self.path("register")
        self.evidence = self.path("evidence")
        self.out = self.path("percent.json")
        self.credits = self.path("credits.jsonl")
        self.rows = [make_row(1), make_row(2)]
        write_register(self.register, {"blender/mesh.jsonl": self.rows})
        write_run(self.evidence, "100", manifest={"finished_at": "2026-11-18T09:00:00Z"}, checks=[(ids_of(self.rows[0])[0], "ok")])

    def tool(self, *extra):
        return subprocess.run(
            [sys.executable, os.path.join(support.TOOLS_DIR, "percent.py"), "--register", self.register, "--evidence", self.evidence,
             "--out", self.out, "--credits-out", self.credits, "--legacy", self.path("none.json"), "--now", "2026-11-20T12:00:00Z"] + list(extra),
            capture_output=True, text=True,
        )

    def test_writes_percent_and_credits(self):
        result = self.tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.out) as handle:
            data = json.load(handle)
        self.assertEqual((data["total"], data["passing"], data["percent"]), (2, 1, 50.0))
        self.assertEqual(data["headline"], "1 of 2 Register rows passing (50.00%)")
        with open(self.credits) as handle:
            lines = [json.loads(x) for x in handle]
        self.assertEqual([l["id"] for l in lines], [self.rows[0]["id"]])
        self.assertEqual(lines[0]["at"], "2026-11-18T09:00:00Z")

    def test_output_is_deterministic(self):
        self.tool()
        with open(self.out) as handle:
            first = handle.read()
        self.tool("--now", "2026-11-20T12:00:00Z")
        with open(self.out) as handle:
            self.assertEqual(handle.read(), first)

    def test_check_passes_on_a_computed_file_and_fails_on_a_typed_one(self):
        self.tool()
        self.assertEqual(self.tool("--check").returncode, 0)
        with open(self.out) as handle:
            data = json.load(handle)
        data["passing"], data["percent"] = 2, 100.0  # typed by hand
        with open(self.out, "w") as handle:
            json.dump(data, handle)
        result = self.tool("--check")
        self.assertEqual(result.returncode, 1)
        self.assertIn("written by CI only", result.stderr)

    def test_check_ignores_the_clock(self):
        self.tool()
        self.assertEqual(self.tool("--check", "--now", "2026-12-25T00:00:00Z").returncode, 0)

    def test_check_fails_when_the_evidence_moved_on(self):
        self.tool()
        write_run(self.evidence, "101", checks=[(ids_of(self.rows[1])[0], "ok")])
        self.assertEqual(self.tool("--check").returncode, 1)

    def test_strict_fails_on_an_unproven_claim_or_bad_evidence(self):
        self.assertEqual(self.tool("--strict").returncode, 0)
        liar = make_row(3, tier_now="Strong", status="verified", evidence={"run_id": "5", "commit": "abc1234"})
        write_register(self.register, {"blender/other.jsonl": [liar]})
        result = self.tool("--strict")
        self.assertEqual(result.returncode, 1)
        self.assertIn("1 unproven claims", result.stderr)

    def test_missing_explicit_directories_are_exit_two(self):
        self.assertEqual(self.tool("--register", self.path("nope")).returncode, 2)
        self.assertEqual(self.tool("--evidence", self.path("nope")).returncode, 2)

    def test_stdout_mode(self):
        result = self.tool("--out", "-")
        data = json.loads(result.stdout)
        self.assertEqual(data["passing"], 1)

    def test_reverted_rows_use_the_previous_credits_file(self):
        self.tool()
        write_run(self.evidence, "101", manifest={"sha": "z9"}, checks=[(ids_of(self.rows[0])[0], "fail")])
        self.tool()
        with open(self.out) as handle:
            data = json.load(handle)
        self.assertEqual(data["reverted"]["count"], 1)
        self.assertEqual(data["reverted"]["rows"][0]["id"], self.rows[0]["id"])


if __name__ == "__main__":
    unittest.main()
