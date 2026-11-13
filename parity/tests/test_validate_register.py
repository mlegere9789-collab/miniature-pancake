"""Tests for parity/tools/validate_register.py: one test per rule."""

from __future__ import annotations

import copy
import io
import json
import os
import shutil
import subprocess
import sys
import unittest

from . import support
from .support import TempDirCase, make_row, write_jsonl, write_register

import regio  # noqa: E402
import validate_register as vr  # noqa: E402

try:
    import jsonschema  # noqa: F401

    HAVE_JSONSCHEMA = True
except ImportError:  # the built-in validator is then the only engine
    HAVE_JSONSCHEMA = False

EVIDENCE = {"run_id": "43486663063", "commit": "0123abc", "task": "T-1"}


def credited(row, tier="Strong", evidence=None):
    """The row as the CI count step would leave it."""
    row = copy.deepcopy(row)
    row["tier_now"] = tier
    row["status"] = "verified"
    ev = dict(evidence or EVIDENCE)
    ev["spec_sha"] = regio.spec_sha(row)
    row["evidence"] = ev
    return row


def run_cli(*args):
    out = io.StringIO()
    code = vr.run(list(args), out=out)
    return code, out.getvalue()


def codes(issues, severity=None):
    return sorted(
        i["code"] for i in issues if severity is None or i["severity"] == severity
    )


class CheckBase(TempDirCase):
    def setUp(self):
        super().setUp()
        with open(support.SCHEMA_PATH, "r", encoding="utf-8") as handle:
            self.schema = json.load(handle)
        self.validate, _ = vr.make_validator(self.schema, "builtin")

    def check(self, files, inventory=None):
        root = self.path("register")
        shutil.rmtree(root, ignore_errors=True)  # every call starts from an empty Register
        write_register(root, files)
        records, problems = regio.read_register(root)
        issues = [dict(p) for p in problems]
        issues += vr.check_records(records, self.validate, root, inventory)
        return issues


class SchemaRules(CheckBase):
    def test_valid_rows_pass(self):
        issues = self.check({"blender/mesh.jsonl": [make_row(1), make_row(2)]})
        self.assertEqual(issues, [])

    def test_missing_required_field(self):
        row = make_row(1)
        del row["statement"]
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["schema"])
        self.assertIn("statement", issues[0]["message"])

    def test_bad_id_pattern(self):
        issues = self.check({"blender/mesh.jsonl": [make_row(1, id="D8-blender-mesh-1")]})
        self.assertEqual(codes(issues, "error"), ["schema"])

    def test_unknown_program_and_factory_and_tier(self):
        row = make_row(1, program="blender")
        row["program"] = ["maya"]
        row["factory"] = "chaos"
        row["tier_now"] = "Gold"
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["schema"] * 3)

    def test_additional_property_is_rejected(self):
        issues = self.check({"blender/mesh.jsonl": [make_row(1, stray="x")]})
        self.assertEqual(codes(issues, "error"), ["schema"])
        self.assertIn("stray", issues[0]["message"])

    def test_acceptance_item_typo_is_rejected(self):
        row = make_row(1, acceptance=[{"test": "mesh.bevel.1"}])
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertIn("schema", codes(issues, "error"))

    def test_empty_test_id_is_rejected(self):
        row = make_row(1, acceptance=[{"test_id": ""}])
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertIn("schema", codes(issues, "error"))

    def test_wave_must_be_a_planned_wave(self):
        for bad in (7, -1, "1", True, 1.5):
            issues = self.check({"blender/mesh.jsonl": [make_row(1, wave=bad)]})
            self.assertEqual(codes(issues, "error"), ["schema"], bad)
        issues = self.check({"blender/mesh.jsonl": [make_row(1, wave=2)]})
        self.assertEqual(issues, [])

    def test_evidence_object_needs_run_id_and_commit(self):
        row = make_row(1, evidence={"run_id": "1"})
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertIn("schema", codes(issues, "error"))

    def test_bad_json_and_non_object_lines(self):
        root = self.path("register")
        write_jsonl(
            os.path.join(root, "blender", "mesh.jsonl"),
            [make_row(1)],
            raw_lines=["{not json", "[1, 2]"],
        )
        records, problems = regio.read_register(root)
        self.assertEqual(len(records), 1)
        self.assertEqual(sorted(p["code"] for p in problems), ["bad-json", "not-object"])


class UniqueIds(CheckBase):
    def test_duplicate_id_in_one_file(self):
        issues = self.check({"blender/mesh.jsonl": [make_row(1), make_row(2, id=make_row(1)["id"])]})
        self.assertEqual(codes(issues, "error"), ["duplicate-id"])
        self.assertIn("mesh.jsonl:1", issues[0]["message"])

    def test_duplicate_id_across_files(self):
        issues = self.check(
            {
                "blender/mesh.jsonl": [make_row(1)],
                "blender/nodes.jsonl": [make_row(1, domain="nodes", id=make_row(1)["id"])],
            }
        )
        self.assertIn("duplicate-id", codes(issues, "error"))


class EvidenceGate(CheckBase):
    def test_strong_without_evidence_is_an_error(self):
        row = make_row(1, tier_now="Strong")
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["tier-without-evidence"])

    def test_best_without_evidence_is_an_error(self):
        row = make_row(1, tier_now="Best", target="Best", superiority_tests=["perf.1"])
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["tier-without-evidence"])

    def test_evidence_without_commit_does_not_count(self):
        row = make_row(1, tier_now="Strong", evidence={"run_id": "5"})
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["schema", "tier-without-evidence"])

    def test_credited_row_with_evidence_passes(self):
        issues = self.check({"blender/mesh.jsonl": [credited(make_row(1))]})
        self.assertEqual(issues, [])

    def test_sub_strong_tiers_need_no_evidence(self):
        rows = [make_row(1, tier_now="Basic"), make_row(2, tier_now="Partial")]
        self.assertEqual(self.check({"blender/mesh.jsonl": rows}), [])

    def test_verified_without_evidence(self):
        row = make_row(1, status="verified", tier_now="Strong")
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["tier-without-evidence", "verified-without-evidence"])

    def test_verified_below_target(self):
        row = credited(make_row(1, target="Best", superiority_tests=["perf.1"]), tier="Strong")
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["verified-below-target"])

    def test_verified_with_tier_none(self):
        row = make_row(1, status="verified", evidence=dict(EVIDENCE))
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["verified-below-target"])

    def test_credited_row_needs_a_test(self):
        row = credited(make_row(1, acceptance=[]))
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["no-acceptance"])

    def test_best_needs_a_superiority_test(self):
        row = credited(make_row(1, target="Best"), tier="Best")
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues, "error"), ["best-without-superiority-test"])
        row = credited(make_row(1, target="Best", superiority_tests=["perf.1"]), tier="Best")
        self.assertEqual(self.check({"blender/mesh.jsonl": [row]}), [])

    def test_changed_acceptance_voids_the_seal(self):
        row = credited(make_row(1))
        row["acceptance"][0]["tolerance"]["volume_rel"] = 1e-2  # loosened after the credit
        issues = self.check({"blender/mesh.jsonl": [row]})
        self.assertEqual(codes(issues), ["spec-changed"])
        self.assertEqual(issues[0]["severity"], "warning")


class Aliases(CheckBase):
    def test_alias_to_existing_row(self):
        rows = [make_row(1), make_row(2, alias_of=make_row(1)["id"], acceptance=[])]
        self.assertEqual(self.check({"blender/mesh.jsonl": rows}), [])

    def test_alias_target_missing(self):
        rows = [make_row(1, alias_of="D8-BLENDER-MESH-9999")]
        issues = self.check({"blender/mesh.jsonl": rows})
        self.assertEqual(codes(issues, "error"), ["alias-missing"])

    def test_alias_of_itself(self):
        rows = [make_row(1, alias_of=make_row(1)["id"])]
        issues = self.check({"blender/mesh.jsonl": rows})
        self.assertEqual(codes(issues, "error"), ["alias-self"])

    def test_alias_cycle_is_reported_once(self):
        a, b = make_row(1), make_row(2)
        a["alias_of"], b["alias_of"] = b["id"], a["id"]
        issues = self.check({"blender/mesh.jsonl": [a, b]})
        self.assertEqual(codes(issues, "error"), ["alias-cycle"])

    def test_alias_chain_is_allowed(self):
        a, b, c = make_row(1), make_row(2), make_row(3)
        b["alias_of"], c["alias_of"] = a["id"], b["id"]
        self.assertEqual(self.check({"blender/mesh.jsonl": [a, b, c]}), [])

    def test_alias_null_is_a_normal_row(self):
        self.assertEqual(self.check({"blender/mesh.jsonl": [make_row(1, alias_of=None)]}), [])


class Inventory(CheckBase):
    def test_unknown_test_for_active_rows_only(self):
        active = make_row(1, status="in_progress")
        idle = make_row(2)  # open rows may have tests that do not exist yet
        issues = self.check({"blender/mesh.jsonl": [active, idle]}, inventory={"other.test"})
        self.assertEqual(codes(issues, "error"), ["unknown-test"])
        self.assertEqual(issues[0]["id"], active["id"])

    def test_known_test_passes(self):
        active = make_row(1, status="in_progress")
        wanted = {active["acceptance"][0]["test_id"]}
        self.assertEqual(self.check({"blender/mesh.jsonl": [active]}, inventory=wanted), [])

    def test_load_inventory_formats(self):
        path = self.path("inv.txt")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("# comment\nplain.test\n" + json.dumps({"suite": "k", "test": "json.test"}) + "\n")
        self.assertEqual(vr.load_inventory(path), {"plain.test", "json.test"})


class Warnings(CheckBase):
    def test_path_mismatch(self):
        issues = self.check({"rhino/mesh.jsonl": [make_row(1)]})
        self.assertEqual(codes(issues), ["path-mismatch"])
        issues = self.check({"blender/other.jsonl": [make_row(1)]})
        self.assertEqual(codes(issues), ["path-mismatch"])

    def test_multi_program_row_may_live_in_either_directory(self):
        row = make_row(1, program="rhino")
        row["program"] = ["rhino", "grasshopper"]
        self.assertEqual(self.check({"grasshopper/mesh.jsonl": [row]}), [])

    def test_path_layout(self):
        issues = self.check({"mesh.jsonl": [make_row(1)]})
        self.assertEqual(codes(issues), ["path-layout"])

    def test_duplicate_statement(self):
        rows = [make_row(1), make_row(2, statement=make_row(1)["statement"].upper() + ".")]
        issues = self.check({"blender/mesh.jsonl": rows})
        self.assertEqual(codes(issues), ["duplicate-statement"])

    def test_alias_rows_may_repeat_a_statement(self):
        rows = [make_row(1), make_row(2, statement=make_row(1)["statement"], alias_of=make_row(1)["id"])]
        self.assertEqual(self.check({"blender/mesh.jsonl": rows}), [])


class BuiltinValidator(unittest.TestCase):
    def test_unsupported_keyword_is_refused_not_ignored(self):
        with self.assertRaises(vr.UnsupportedSchema):
            vr.make_validator({"type": "object", "oneOf": []}, "builtin")
        with self.assertRaises(vr.UnsupportedSchema):
            vr.make_validator({"type": "object", "properties": {"a": {"minimum": 1}}}, "builtin")

    def test_row_schema_uses_only_supported_keywords(self):
        with open(support.SCHEMA_PATH, "r", encoding="utf-8") as handle:
            vr.make_validator(json.load(handle), "builtin")

    def test_bool_is_not_a_number(self):
        schema = {"type": "object", "properties": {"n": {"enum": [0, 1]}, "i": {"type": "integer"}}}
        check, _ = vr.make_validator(schema, "builtin")
        self.assertEqual(check({"n": 1, "i": 3}), [])
        self.assertTrue(check({"n": True}))
        self.assertTrue(check({"i": True}))

    @unittest.skipUnless(HAVE_JSONSCHEMA, "jsonschema is not installed")
    def test_builtin_agrees_with_jsonschema(self):
        with open(support.SCHEMA_PATH, "r", encoding="utf-8") as handle:
            schema = json.load(handle)
        builtin, _ = vr.make_validator(schema, "builtin")
        canonical, engine = vr.make_validator(schema, "jsonschema")
        self.assertEqual(engine, "jsonschema")

        good = make_row(1)
        cases = [good, credited(good), credited(make_row(2, target="Best", superiority_tests=["p"]), tier="Best")]
        bad = []

        def broken(**over):
            row = make_row(1)
            row.update(over)
            return row

        bad.append(broken(id="d8-x"))
        bad.append(broken(program=[]))
        bad.append(broken(program=["maya"]))
        bad.append(broken(program="blender"))
        bad.append(broken(statement="short"))
        bad.append(broken(source={"kind": "magic", "ref": "x", "doc": "y"}))
        bad.append(broken(source={"kind": "command", "ref": "x"}))
        bad.append(broken(tier_now="Gold"))
        bad.append(broken(target="Basic"))
        bad.append(broken(acceptance=[{"test_id": 5}]))
        bad.append(broken(acceptance=[{"test_id": "a", "extra": 1}]))
        bad.append(broken(acceptance="a"))
        bad.append(broken(budget_ref=5))
        bad.append(broken(links={"pain_points": ["a"]}))
        bad.append(broken(links={"wishes": [1.5]}))
        bad.append(broken(factory="x"))
        bad.append(broken(status="done"))
        bad.append(broken(evidence="x"))
        bad.append(broken(evidence={"run_id": "1", "commit": "NOTHEX!"}))
        bad.append(broken(evidence={"run_id": "1", "commit": "abc1234", "spec_sha": "short"}))
        bad.append(broken(wave=5))
        bad.append(broken(wave=True))
        bad.append(broken(superiority_tests=[""]))
        bad.append(broken(superiority_tests="x"))
        bad.append(broken(alias_of=5))
        bad.append(broken(notes=[]))
        bad.append(broken(stray=1))
        missing = make_row(1)
        del missing["domain"]
        bad.append(missing)
        for row in cases:
            self.assertEqual(builtin(row), [], row)
            self.assertEqual(canonical(row), [], row)
        for row in bad:
            self.assertTrue(builtin(row), row)
            self.assertTrue(canonical(row), row)


class CommandLine(TempDirCase):
    def run_tool(self, *args):
        return subprocess.run(
            [sys.executable, os.path.join(support.TOOLS_DIR, "validate_register.py")] + list(args),
            capture_output=True,
            text=True,
        )

    def test_exit_zero_on_a_clean_register(self):
        root = self.path("register")
        write_register(root, {"blender/mesh.jsonl": [make_row(1)]})
        result = self.run_tool("--register", root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("1 rows", result.stdout)

    def test_exit_one_on_an_error_and_names_the_file_line(self):
        root = self.path("register")
        write_register(root, {"blender/mesh.jsonl": [make_row(1), make_row(2, tier_now="Strong")]})
        result = self.run_tool("--register", root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("mesh.jsonl:2", result.stdout)
        self.assertIn("tier-without-evidence", result.stdout)

    def test_strict_turns_warnings_into_errors(self):
        root = self.path("register")
        write_register(root, {"rhino/mesh.jsonl": [make_row(1)]})
        self.assertEqual(self.run_tool("--register", root).returncode, 0)
        self.assertEqual(self.run_tool("--register", root, "--strict").returncode, 1)

    def test_missing_explicit_register_is_exit_two(self):
        result = self.run_tool("--register", self.path("nope"))
        self.assertEqual(result.returncode, 2)

    def test_json_report(self):
        root = self.path("register")
        write_register(root, {"blender/mesh.jsonl": [make_row(1, tier_now="Strong")]})
        result = self.run_tool("--register", root, "--json")
        report = json.loads(result.stdout)
        self.assertEqual(report["rows"], 1)
        self.assertEqual(report["errors"][0]["code"], "tier-without-evidence")

    def test_github_annotations(self):
        root = self.path("register")
        write_register(root, {"blender/mesh.jsonl": [make_row(1, tier_now="Strong")]})
        result = self.run_tool("--register", root, "--github")
        self.assertIn("::error file=", result.stdout)

    def test_builtin_engine_flag_works_without_jsonschema(self):
        root = self.path("register")
        write_register(root, {"blender/mesh.jsonl": [make_row(1)]})
        result = self.run_tool("--register", root, "--engine", "builtin")
        self.assertEqual(result.returncode, 0)
        self.assertIn("engine: builtin", result.stdout)


if __name__ == "__main__":
    unittest.main()
