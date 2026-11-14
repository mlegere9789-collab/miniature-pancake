"""Helpers shared by the parity tool tests: row builders and temporary Register trees."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PARITY_DIR = os.path.dirname(HERE)
REPO_ROOT = os.path.dirname(PARITY_DIR)
TOOLS_DIR = os.path.join(PARITY_DIR, "tools")
LEDGER_TOOLS_DIR = os.path.join(REPO_ROOT, "ledger", "tools")
SCHEMA_PATH = os.path.join(PARITY_DIR, "schema", "row.schema.json")

for _path in (TOOLS_DIR, LEDGER_TOOLS_DIR):
    if _path not in sys.path:
        sys.path.insert(0, _path)


def make_row(n=1, program="blender", domain="mesh", **over):
    """A valid, uncredited Register row (id D8-<PROGRAM>-<DOMAIN>-<nnnn>)."""
    row = {
        "id": "D8-%s-%s-%04d" % (program.upper()[:8], domain.upper()[:8], n),
        "program": [program],
        "statement": "Bevel the selected edges with width, segments and profile (case %d)"
        % n,
        "source": {
            "kind": "operator",
            "ref": "bpy.ops.mesh.bevel",
            "doc": "https://docs.blender.org/api/current/bpy.ops.mesh.html",
        },
        "tier_now": "None",
        "target": "Strong",
        "acceptance": [
            {
                "test_id": "%s.%s.case%d" % (program, domain, n),
                "corpus": "golden/mesh/bevel_set_03",
                "tolerance": {"volume_rel": 1e-6},
            }
        ],
        "factory": "mesh-subd-sculpt",
        "domain": domain,
        "status": "open",
        "evidence": None,
    }
    row.update(over)
    return row


def write_jsonl(path, rows, raw_lines=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row) + "\n")
        for text in raw_lines or []:
            handle.write(text + "\n")


def write_register(root, files):
    """files: {"blender/mesh.jsonl": [row, ...]} below root."""
    for relative, rows in files.items():
        write_jsonl(os.path.join(root, *relative.split("/")), rows)


class TempDirCase(unittest.TestCase):
    """A test case with a fresh temporary directory in self.tmp."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="d8-ledger-test-")
        self.addCleanup(self._tmp.cleanup)
        self.tmp = self._tmp.name

    def path(self, *parts):
        return os.path.join(self.tmp, *parts)


def write_run(
    root, run, manifest=None, tasks=None, checks=None, checks_name="checks.jsonl"
):
    """Write one evidence run directory below root.

    manifest: dict for run.json (or None); tasks: {task id: dict with passed/failed/skipped};
    checks: list of (test, result) pairs or full dicts, written as one JSONL file.
    """
    directory = os.path.join(root, str(run))
    os.makedirs(directory, exist_ok=True)
    if manifest is not None:
        with open(os.path.join(directory, "run.json"), "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
    for task, body in (tasks or {}).items():
        body = dict(body)
        body.setdefault("task", task)
        with open(
            os.path.join(directory, task + ".json"), "w", encoding="utf-8"
        ) as handle:
            json.dump(body, handle)
    if checks is not None:
        lines = []
        for item in checks:
            if isinstance(item, dict):
                lines.append(item)
            else:
                test, result = item
                lines.append(
                    {
                        "suite": "s",
                        "test": test,
                        "name": test,
                        "result": result,
                        "ms": 1,
                    }
                )
        write_jsonl(os.path.join(directory, checks_name), lines)
    return directory
