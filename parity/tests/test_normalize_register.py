"""Tests for parity/tools/normalize_register.py: lossless, idempotent, strict-clean output."""

from __future__ import annotations

import os

import normalize_register  # noqa: E402
import regio  # noqa: E402

from .support import TempDirCase, make_row, write_register


def sketch_row(n, program="revit", domain="revit.view", **over):
    row = make_row(n, program=program, domain=domain, **over)
    row["id"] = "D8-%s-%s-%04d" % (program.upper()[:8], "VIEW", n)
    row["acceptance"] = [{"test_id": "acc.%s.%d" % (program, n), "sketch": "Sketch %d." % n}]
    return row


class NormalizeTests(TempDirCase):
    def run_tool(self, *extra):
        return normalize_register.main(["--register", self.root, "--quiet"] + list(extra))

    def setUp(self):
        super().setUp()
        self.root = self.path("register")

    def rows(self):
        records, problems = regio.read_register(self.root)
        self.assertEqual(problems, [])
        return records

    def test_sketch_moves_into_notes_and_nothing_is_lost(self):
        first = sketch_row(1, notes="Existing note.")
        second = sketch_row(2)
        write_register(self.root, {"revit/view.jsonl": [first, second]})
        self.assertEqual(self.run_tool(), 0)
        by_id = {r["row"]["id"]: r["row"] for r in self.rows()}
        one = by_id[first["id"]]
        self.assertEqual(one["notes"], "Existing note. Acceptance sketch: Sketch 1.")
        self.assertEqual(one["acceptance"], [{"test_id": "acc.revit.1"}])
        self.assertEqual(by_id[second["id"]]["notes"], "Acceptance sketch: Sketch 2.")
        self.assertEqual(one["statement"], first["statement"])

    def test_file_name_becomes_domain_and_files_merge(self):
        a = make_row(1, domain="shared")
        b = make_row(2, domain="shared")
        write_register(self.root, {"blender/a.jsonl": [a], "blender/b_shared.jsonl": [b]})
        self.assertEqual(self.run_tool(), 0)
        records = self.rows()
        self.assertEqual({os.path.basename(r["file"]) for r in records}, {"shared.jsonl"})
        self.assertEqual([r["row"]["id"] for r in records], [a["id"], b["id"]])

    def test_missing_test_id_is_created_and_multi_item_sketches_are_labelled(self):
        row = make_row(7, program="autocad", domain="draw")
        row["acceptance"] = [{"sketch": "first"}, {"sketch": "second", "corpus": "c"}]
        write_register(self.root, {"autocad/draw.jsonl": [row]})
        self.assertEqual(self.run_tool(), 0)
        out = self.rows()[0]["row"]
        ids = [a["test_id"] for a in out["acceptance"]]
        self.assertEqual(ids, ["acc.autocad.draw.0007", "acc.autocad.draw.0007.2"])
        self.assertEqual(out["acceptance"][1]["corpus"], "c")
        self.assertIn("Acceptance sketch (acc.autocad.draw.0007): first", out["notes"])
        self.assertIn("Acceptance sketch (acc.autocad.draw.0007.2): second", out["notes"])

    def test_idempotent_and_check_mode(self):
        write_register(
            self.root,
            {"revit/view.jsonl": [sketch_row(1)], "blender/x.jsonl": [make_row(2, domain="mesh")]},
        )
        self.assertEqual(self.run_tool("--check"), 1)
        self.assertEqual(self.run_tool(), 0)
        snapshot = {
            os.path.relpath(r["file"], self.root): r["row"] for r in self.rows()
        }
        self.assertEqual(self.run_tool("--check"), 0)
        self.assertEqual(self.run_tool(), 0)
        again = {os.path.relpath(r["file"], self.root): r["row"] for r in self.rows()}
        self.assertEqual(snapshot, again)

    def test_bad_json_stops_the_run(self):
        write_register(self.root, {"blender/mesh.jsonl": [make_row(1)]})
        with open(os.path.join(self.root, "blender", "mesh.jsonl"), "a") as handle:
            handle.write("{not json\n")
        self.assertEqual(self.run_tool(), 2)

    def test_duplicate_ids_stop_the_run(self):
        row = make_row(1)
        write_register(self.root, {"blender/mesh.jsonl": [row, dict(row)]})
        self.assertEqual(self.run_tool(), 2)

    def test_result_passes_the_strict_validator(self):
        write_register(
            self.root,
            {"revit/view.jsonl": [sketch_row(1), sketch_row(2)]},
        )
        self.assertEqual(self.run_tool("--validate"), 0)
