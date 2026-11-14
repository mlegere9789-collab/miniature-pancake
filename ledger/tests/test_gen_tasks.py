"""Tests for ledger/tools/gen_tasks.py (the Wave 1 work queue generator)."""

from __future__ import annotations

import json
import os
import shutil
import sys
import unittest

import claims  # noqa: E402
import gen_tasks  # noqa: E402

from . import support
from .support import REPO_ROOT, RemoteCase, TempDirCase

REAL_CATALOG = os.path.join(REPO_ROOT, "parity", "tools", "catalog")


def make_row(n, factory="flow", domain="DRAW", program="rhino", corpus=None, alias_of=None, wave=None, ref=None, statement=None, target="Strong"):
    rid = "D8-%s-%s-%04d" % (program.upper()[:8], "".join(c for c in domain.upper() if c.isalnum())[:8] or "X", n)
    row = {
        "id": rid,
        "program": [program],
        "statement": statement or "Row number %d of %s" % (n, domain),
        "source": {"kind": "command", "ref": ref or "Zzq%dQqz" % n, "doc": "https://example.invalid/%d" % n},
        "tier_now": "None",
        "target": target,
        "acceptance": [{"test_id": "t.%s.%d" % (domain, n)}] if corpus is None else [{"test_id": "t.%s.%d" % (domain, n), "corpus": corpus}],
        "budget_ref": None,
        "links": {"pain_points": [], "wishes": []},
        "factory": factory,
        "domain": domain,
        "alias_of": alias_of,
        "status": "open",
        "evidence": None,
    }
    if wave is not None:
        row["wave"] = wave
    return row


class Workspace(TempDirCase):
    """A temporary Register, catalog and output directory."""

    def setUp(self):
        super().setUp()
        self.register = self.path("register")
        self.out = self.path("tasks")
        self.catalog = self.path("catalog")
        self.waves = self.path("waves.json")
        os.makedirs(self.catalog)
        shutil.copy(os.path.join(REAL_CATALOG, "map_rows.py"), self.catalog)
        self.set_catalog([], [])
        with open(self.waves, "w") as handle:
            json.dump({"version": 1, "factory": {"flow": 1, "bim-arch": 2, "kernel-geometry": 1}}, handle)

    def set_catalog(self, commands, kernel):
        with open(os.path.join(self.catalog, "catalog.jsonl"), "w") as handle:
            for name in commands:
                handle.write(json.dumps({"name": name, "aliases": [], "status": "implemented"}) + "\n")
        with open(os.path.join(self.catalog, "kernel_api.jsonl"), "w") as handle:
            for symbol in kernel:
                handle.write(json.dumps({"symbol": symbol, "header": "a.h", "line": 1, "kind": "function", "name": symbol.split("::")[-1], "scope": "", "internal": False}) + "\n")

    def write_register(self, rows_by_file):
        shutil.rmtree(self.register, ignore_errors=True)
        for rel, rows in rows_by_file.items():
            path = os.path.join(self.register, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as handle:
                for row in rows:
                    handle.write(json.dumps(row) + "\n")

    def generate(self, **kw):
        kw.setdefault("hints_on", False)
        return gen_tasks.generate(self.register, self.out, self.waves, self.catalog, **kw)

    def run_all(self, **kw):
        result = self.generate(**kw)
        gen_tasks.write_output(result, self.out)
        return result

    def read(self, name):
        with open(os.path.join(self.out, name), encoding="utf-8") as handle:
            return handle.read()

    def read_all(self):
        return {n: self.read(n) for n in sorted(os.listdir(self.out))}

    def tasks(self):
        found = {}
        for name in sorted(os.listdir(self.out)):
            if name.startswith("T-") and name.endswith(".json"):
                with open(os.path.join(self.out, name)) as handle:
                    found[name[:-5]] = json.load(handle)
        return found


def standard_register():
    rows = []
    rows += [make_row(i, "flow", "DRAW") for i in range(1, 96)]  # 95 rows -> 3 tasks
    rows += [make_row(i, "flow", "TINY", "autocad") for i in range(1, 13)]  # 12 + 15 pooled
    rows += [make_row(i, "flow", "SMALL", "autocad") for i in range(1, 16)]
    rows += [make_row(i, "bim-arch", "WALL", "archicad") for i in range(1, 26)]  # 25 -> one task
    return {"rhino/draw.jsonl": [r for r in rows if r["domain"] == "DRAW"], "autocad/misc.jsonl": [r for r in rows if r["domain"] in ("TINY", "SMALL")], "archicad/wall.jsonl": [r for r in rows if r["domain"] == "WALL"]}


class Grouping(Workspace):
    def test_sizes_factories_and_coverage(self):
        self.write_register(standard_register())
        self.run_all()
        tasks = self.tasks()
        seen = []
        for task in tasks.values():
            self.assertGreaterEqual(len(task["rows"]), 20, task["id"])
            self.assertLessEqual(len(task["rows"]), 40, task["id"])
            seen += task["rows"]
        self.assertEqual(len(seen), len(set(seen)))
        self.assertEqual(len(seen), 95 + 12 + 15 + 25)
        by_factory = {t["factory"] for t in tasks.values()}
        self.assertEqual(by_factory, {"flow", "bim-arch"})
        # DRAW: 3 tasks of 31 or 32; the two small domains pool into one misc task of 27
        flow = sorted(len(t["rows"]) for t in tasks.values() if t["factory"] == "flow")
        self.assertEqual(flow, [27, 31, 32, 32])
        misc = [t for t in tasks.values() if t["pooled_domains"]]
        self.assertEqual(len(misc), 1)
        self.assertEqual(misc[0]["domains"], ["SMALL", "TINY"])

    def test_domains_are_grouped_case_insensitively_across_programs(self):
        rows = {
            "rhino/a.jsonl": [make_row(i, "flow", "Mesh", "rhino") for i in range(1, 13)],
            "blender/a.jsonl": [make_row(i, "flow", "MESH", "blender") for i in range(1, 13)],
        }
        self.write_register(rows)
        self.run_all()
        tasks = self.tasks()
        self.assertEqual(len(tasks), 1)
        self.assertEqual(len(next(iter(tasks.values()))["rows"]), 24)

    def test_stub_residual_is_folded_into_a_neighbouring_task(self):
        rows = [make_row(i, "flow", "BIG") for i in range(1, 41)] + [make_row(i, "flow", "SMALL") for i in range(1, 7)]
        self.write_register({"rhino/a.jsonl": rows})
        self.run_all()
        sizes = sorted(len(t["rows"]) for t in self.tasks().values())
        self.assertEqual(sizes, [23, 23])  # 46 rows, no stub of 6

    def test_a_factory_smaller_than_the_window_is_one_small_task(self):
        self.write_register({"rhino/a.jsonl": [make_row(i, "flow", "A") for i in range(1, 6)]})
        self.run_all()
        tasks = self.tasks()
        self.assertEqual(len(tasks), 1)
        self.assertEqual(self.generate()["index"]["task_size"]["outside_window"], 1)

    def test_aliases_stay_with_their_canonical_row(self):
        canon = make_row(1, "flow", "DRAW")
        rows = [canon] + [make_row(i, "flow", "DRAW") for i in range(2, 80)]
        # aliases placed far from the canonical row in file order, one in another domain
        rows.append(make_row(80, "flow", "DRAW", alias_of=canon["id"]))
        other = make_row(81, "flow", "OTHER", alias_of=canon["id"])
        rows.append(other)
        chain = make_row(82, "flow", "DRAW", alias_of=other["id"])  # alias of an alias
        rows.append(chain)
        self.write_register({"rhino/a.jsonl": rows})
        self.run_all()
        for task in self.tasks().values():
            if canon["id"] in task["rows"]:
                self.assertEqual(task["rows"][0], canon["id"])
                for alias in (rows[-3]["id"], other["id"], chain["id"]):
                    self.assertIn(alias, task["rows"])
                    self.assertIn(alias, task["alias_rows"])
                break
        else:
            self.fail("canonical row in no task")
        self.assertEqual(gen_tasks.validate_queue(self.out, self.register), [])

    def test_rows_sharing_a_corpus_are_not_cut_apart_when_a_boundary_fits(self):
        rows = []
        for i in range(1, 31):
            rows.append(make_row(i, "flow", "D", corpus="corpus A" if i % 2 else "corpus B"))
        for i in range(31, 51):
            rows.append(make_row(i, "flow", "D", corpus="corpus C"))
        self.write_register({"rhino/a.jsonl": rows})
        self.run_all()
        tasks = self.tasks()
        self.assertEqual(len(tasks), 2)
        corpora = sorted(tuple(t["corpora"]) for t in tasks.values())
        self.assertEqual(corpora, [("corpus A", "corpus B"), ("corpus C",)])

    def test_wave_from_the_map_and_the_row_wins(self):
        rows = [make_row(i, "flow", "A") for i in range(1, 22)] + [make_row(22, "flow", "A", wave=3)]
        self.write_register({"rhino/a.jsonl": rows})
        self.run_all()
        task = next(iter(self.tasks().values()))
        self.assertEqual(task["wave"], 3)  # a task completes with its last row
        self.assertEqual(self.generate()["index"]["by_wave"]["3"]["tasks"], 1)


class Output(Workspace):
    def test_task_files_follow_the_ledger_format(self):
        self.write_register(standard_register())
        self.run_all()
        for tid, task in self.tasks().items():
            self.assertTrue(claims.valid_name(tid))
            self.assertEqual(task["status"], "open")
            self.assertEqual(task["retries"], 0)
            self.assertEqual(task["branch"], "task/" + tid)
            self.assertEqual(task["deps"], [])
            self.assertIn(task["points"], (1, 2, 3, 5, 8))
            for forbidden in ("evidence", "tier_now", "verified"):
                self.assertNotIn(forbidden, task)
        self.assertEqual(gen_tasks.validate_queue(self.out, self.register), [])

    def test_output_is_idempotent_and_deterministic(self):
        self.write_register(standard_register())
        first = self.run_all()
        snapshot = self.read_all()
        second = self.generate()
        self.assertEqual(second["files"], first["files"])
        change = gen_tasks.write_output(second, self.out)
        self.assertEqual(change["written"], [])
        self.assertEqual(change["removed"], [])
        self.assertEqual(gen_tasks.check_output(second, self.out), [])
        after = self.read_all()
        self.assertEqual(snapshot, after)

    def test_check_reports_a_stale_directory(self):
        self.write_register(standard_register())
        self.run_all()
        victim = sorted(self.tasks())[0]
        os.remove(os.path.join(self.out, victim + ".json"))
        problems = gen_tasks.check_output(self.generate(), self.out)
        self.assertEqual(problems, ["missing: %s.json" % victim])

    def test_the_register_is_not_touched(self):
        self.write_register(standard_register())
        before = gen_tasks.register_digest(self.register)
        self.run_all()
        self.assertEqual(gen_tasks.register_digest(self.register), before)

    def test_changed_register_prunes_open_tasks_and_keeps_started_ones(self):
        self.write_register(standard_register())
        self.run_all()
        tasks = self.tasks()
        started = sorted(t for t, d in tasks.items() if d["factory"] == "flow")[0]
        data = tasks[started]
        data["status"] = "claimed"
        with open(os.path.join(self.out, started + ".json"), "w") as handle:
            json.dump(data, handle)
        before_text = self.read(started + ".json")
        # drop some rows of the register that are in other tasks, add new ones
        reg = standard_register()
        reg["archicad/wall.jsonl"] = reg["archicad/wall.jsonl"][:22]
        self.write_register(reg)
        result = self.run_all()
        self.assertEqual(self.read(started + ".json"), before_text)
        self.assertEqual(result["frozen"], [started])
        now = self.tasks()
        owners = {}
        for tid, task in now.items():
            for rid in task["rows"]:
                self.assertNotIn(rid, owners)
                owners[rid] = tid
        for rid in data["rows"]:
            self.assertEqual(owners[rid], started)
        self.assertEqual(gen_tasks.validate_queue(self.out, self.register), [])
        # a frozen task is never listed as open work
        self.assertEqual(result["index"]["by_status"].get("claimed"), 1)

    def test_index_counts(self):
        self.write_register(standard_register())
        result = self.run_all()
        index = result["index"]
        self.assertEqual(index["total"]["rows"], 147)
        self.assertEqual(index["by_factory"]["flow"]["rows"], 122)
        self.assertEqual(index["by_factory"]["bim-arch"]["rows"], 25)
        self.assertEqual(index["by_wave"]["1"]["rows"], 122)
        self.assertEqual(index["by_wave"]["2"]["rows"], 25)
        self.assertEqual(sum(t["rows"] for t in index["tasks"]), 147)
        with open(os.path.join(self.out, "index.json")) as handle:
            self.assertEqual(json.load(handle), index)
        report = self.read("REPORT.md")
        self.assertIn("| Tasks | %d |" % index["total"]["tasks"], report)

    def test_validate_detects_a_lost_row_and_a_mixed_factory(self):
        self.write_register(standard_register())
        self.run_all()
        tasks = self.tasks()
        flow = sorted(t for t, d in tasks.items() if d["factory"] == "flow")[0]
        bim = sorted(t for t, d in tasks.items() if d["factory"] == "bim-arch")[0]
        path = os.path.join(self.out, flow + ".json")
        data = tasks[flow]
        lost = data["rows"].pop()
        data["counts"]["rows"] -= 1
        with open(path, "w") as handle:
            json.dump(data, handle)
        problems = gen_tasks.validate_queue(self.out, self.register)
        self.assertTrue(any("are in no task" in p for p in problems), problems)
        data["rows"].append(tasks[bim]["rows"][0])
        data["counts"]["rows"] += 1
        with open(path, "w") as handle:
            json.dump(data, handle)
        problems = gen_tasks.validate_queue(self.out, self.register)
        self.assertTrue(any("mixes factories" in p for p in problems), problems)
        self.assertTrue(any("is also in" in p for p in problems), problems)
        self.assertNotEqual(lost, None)


class Hints(Workspace):
    def test_hints_are_names_and_counts_only(self):
        self.set_catalog(["Circle", "Polyline"], ["d8::geom::ArcLength"])
        rows = []
        for i in range(1, 11):
            rows.append(make_row(i, "flow", "DRAW", ref="Circle", statement="Draw a circle by center and radius."))
        for i in range(11, 21):
            rows.append(make_row(i, "flow", "DRAW", ref="ArcLength", statement="Report the length of an arc."))
        for i in range(21, 41):
            rows.append(make_row(i, "flow", "DRAW"))
        self.write_register({"rhino/a.jsonl": rows})
        result = self.run_all(hints_on=True)
        self.assertEqual(len(result["tasks"]), 1)
        task = result["tasks"][0]
        self.assertEqual(task["counts"]["with_hint"], 20)
        self.assertEqual(task["counts"]["without_hint"], 20)
        self.assertEqual(len(task["rows_without_hint"]), 20)
        self.assertIn("NOT EVIDENCE", task["hints"]["label"])
        self.assertEqual([c["name"] for c in task["hints"]["commands"]], ["Circle"])
        self.assertEqual([k["symbol"] for k in task["hints"]["kernel"]], ["d8::geom::ArcLength"])
        self.assertEqual(task["hints"]["commands"][0]["rows"], 10)
        self.assertEqual(task["row_hints"][rows[0]["id"]]["commands"], ["Circle"])
        for forbidden in ("evidence", "tier_now"):
            self.assertNotIn(forbidden, task)
        total = result["index"]["total"]
        self.assertEqual((total["with_hint"], total["without_hint"]), (20, 20))

    def test_no_hints_mode_marks_every_row_without_hint(self):
        self.write_register({"rhino/a.jsonl": [make_row(i, "flow", "A") for i in range(1, 31)]})
        result = self.run_all(hints_on=False)
        self.assertEqual(result["index"]["total"]["with_hint"], 0)
        self.assertFalse(result["index"]["settings"]["hints"])


class ClaimIntegration(RemoteCase):
    def test_a_generated_task_can_be_claimed_once(self):
        register = self.path("register")
        out = self.path("tasks")
        os.makedirs(os.path.join(register, "rhino"))
        with open(os.path.join(register, "rhino", "a.jsonl"), "w") as handle:
            for i in range(1, 31):
                handle.write(json.dumps(make_row(i, "flow", "DRAW")) + "\n")
        catalog = self.path("catalog")
        os.makedirs(catalog)
        result = gen_tasks.generate(register, out, os.path.join(self.tmp, "none.json"), catalog, hints_on=False)
        gen_tasks.write_output(result, out)
        task = [t for t in os.listdir(out) if t.startswith("T-")][0][:-5]
        first = self.tool("--now", support.iso(0), "claim", task, "--agent", "alpha", "--tasks", out)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.tool("--now", support.iso(1), "claim", task, "--agent", "beta", "--tasks", out)
        self.assertEqual(second.returncode, 3)
        with open(os.path.join(out, task + ".json")) as handle:
            data = json.load(handle)
        data["status"] = "merged"
        with open(os.path.join(out, task + ".json"), "w") as handle:
            json.dump(data, handle)
        third = self.tool("--now", support.iso(2), "claim", task, "--agent", "gamma", "--tasks", out)
        self.assertEqual(third.returncode, 4)


class CommittedQueue(unittest.TestCase):
    """The queue committed under ledger/tasks must agree with the committed Register."""

    def test_committed_queue_validates(self):
        out = os.path.join(REPO_ROOT, "ledger", "tasks")
        register = os.path.join(REPO_ROOT, "parity", "register")
        if not os.path.isfile(os.path.join(out, "index.json")):
            self.skipTest("no generated queue in this checkout")
        self.assertEqual(gen_tasks.validate_queue(out, register), [])

    def test_real_register_groups_into_the_window_without_hints(self):
        register = os.path.join(REPO_ROOT, "parity", "register")
        if not os.path.isdir(register):
            self.skipTest("no Register")
        result = gen_tasks.generate(register, os.path.join(REPO_ROOT, "ledger", "nonexistent-tasks"), os.path.join(REPO_ROOT, "ledger", "waves.json"), REAL_CATALOG, hints_on=False)
        total = sum(len(t["rows"]) for t in result["tasks"])
        self.assertEqual(total, len(gen_tasks.load_rows(register)))
        self.assertEqual(result["index"]["task_size"]["outside_window"], 0)


if __name__ == "__main__":
    unittest.main()
