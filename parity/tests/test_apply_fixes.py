"""Tests for parity/tools/apply_fixes.py."""

from __future__ import annotations

import io
import json
import os
import unittest

import apply_fixes as af  # noqa: E402

from .support import TempDirCase, make_row, write_jsonl, write_register


def read_lines(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read().split("\n")


class ApplyFixesTest(TempDirCase):
    def setUp(self):
        super().setUp()
        self.root = self.path("register")
        self.r1 = make_row(1, program="sketchup", domain="edit", statement="Old statement one here")
        self.r2 = make_row(2, program="sketchup", domain="edit", statement="Old statement two here")
        write_register(self.root, {"sketchup/edit.jsonl": [self.r1, self.r2]})
        self.file = os.path.join(self.root, "sketchup", "edit.jsonl")

    def fixes(self, *items):
        path = self.path("fixes.jsonl")
        write_jsonl(path, list(items))
        return path

    def fix(self, row, field, old, new):
        return {"id": row["id"], "field": field, "old": old, "new": new, "reason": "test"}

    def run_tool(self, path, *extra):
        out, err = io.StringIO(), io.StringIO()
        code = af.run([path, "--register", self.root, *extra], out=out, err=err)
        return code, out.getvalue(), err.getvalue()

    def test_applies_and_touches_only_the_fixed_line(self):
        before = read_lines(self.file)
        path = self.fixes(self.fix(self.r1, "statement", self.r1["statement"], "New statement one."))
        code, out, _ = self.run_tool(path)
        self.assertEqual(code, 0, out)
        after = read_lines(self.file)
        self.assertEqual(json.loads(after[0])["statement"], "New statement one.")
        self.assertEqual(after[1], before[1])

    def test_refuses_when_old_does_not_match_and_writes_nothing(self):
        before = open(self.file, encoding="utf-8").read()
        path = self.fixes(
            self.fix(self.r1, "statement", self.r1["statement"], "New statement one."),
            self.fix(self.r2, "statement", "not the current text", "New two statement."),
        )
        code, _, err = self.run_tool(path)
        self.assertEqual(code, 1)
        self.assertIn("does not match old", err)
        self.assertEqual(open(self.file, encoding="utf-8").read(), before)

    def test_unknown_id_and_missing_field_are_refused(self):
        ghost = dict(self.r1, id="D8-NOPE-NOPE-0001")
        code, _, err = self.run_tool(self.fixes(self.fix(ghost, "statement", "a", "b")))
        self.assertEqual(code, 1)
        self.assertIn("unknown id", err)
        code, _, err = self.run_tool(self.fixes(self.fix(self.r1, "superiority", "a", "b")))
        self.assertEqual(code, 1)
        self.assertIn("no field", err)

    def test_dotted_field_and_chained_fixes(self):
        path = self.fixes(
            self.fix(self.r1, "source.ref", "bpy.ops.mesh.bevel", "ref two"),
            self.fix(self.r1, "source.ref", "ref two", "ref three"),
        )
        code, out, _ = self.run_tool(path)
        self.assertEqual(code, 0, out)
        self.assertEqual(json.loads(read_lines(self.file)[0])["source"]["ref"], "ref three")

    def test_second_run_is_refused_as_already_applied(self):
        path = self.fixes(self.fix(self.r1, "target", "Strong", "Best"))
        self.assertEqual(self.run_tool(path)[0], 0)
        code, _, err = self.run_tool(path)
        self.assertEqual(code, 1)
        self.assertIn("already applied", err)

    def test_dry_run_writes_nothing(self):
        before = open(self.file, encoding="utf-8").read()
        path = self.fixes(self.fix(self.r1, "target", "Strong", "Best"))
        code, out, _ = self.run_tool(path, "--dry-run")
        self.assertEqual(code, 0)
        self.assertIn("would apply 1", out)
        self.assertEqual(open(self.file, encoding="utf-8").read(), before)

    def test_deterministic_output(self):
        path = self.fixes(self.fix(self.r2, "factory", "mesh-subd-sculpt", "direct-modeling"))
        self.assertEqual(self.run_tool(path)[0], 0)
        first = open(self.file, encoding="utf-8").read()
        self.assertTrue(first.endswith("\n"))
        self.assertNotIn("\\u", first)


if __name__ == "__main__":
    unittest.main()
