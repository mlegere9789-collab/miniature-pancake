#!/usr/bin/env python3
"""Self-test of evidence_summary.py: `python3 -m unittest parity/tools/test_evidence_summary.py` (or run the file).

Every case builds a small evidence file the way the runners write it, runs the validator as a child process and checks the verdict
and the exit status, including the cases it exists to catch: a run that died (no summary), counts that differ from the printed
counts, a result that was flipped by hand, a section that never finished, a shard set with a hole.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "evidence_summary.py")


def run_line(suite="kernel", pid=1, **kw):
    d = {"kind": "run", "suite": suite, "pid": pid}
    d.update(kw)
    return d


def check(test, name, result="ok", suite="kernel", pid=1, n=1, **kw):
    d = {"kind": "check", "suite": suite, "pid": pid, "test": test, "name": name, "result": result, "n": n}
    d.update(kw)
    return d


def test_line(test, checks, failed, result=None, suite="kernel", pid=1, **kw):
    d = {"kind": "test", "suite": suite, "pid": pid, "test": test, "name": test,
         "result": result or ("fail" if failed else "ok"), "checks": checks, "failed": failed, "ms": 1.0}
    d.update(kw)
    return d


def summary(checks, ok, failed, tests=1, thrown=0, suite="kernel", pid=1, **kw):
    d = {"kind": "summary", "suite": suite, "pid": pid, "tests": tests, "checks": checks, "ok": ok, "failed": failed,
         "thrown": thrown, "ms": 2.0}
    d.update(kw)
    return d


class Case(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write(self, name, lines, raw=None):
        p = os.path.join(self.tmp.name, name)
        with open(p, "w", encoding="utf-8") as fh:
            if raw is not None:
                fh.write(raw)
            else:
                for r in lines:
                    fh.write(json.dumps(r) + "\n")
        return p

    def validate(self, *args):
        r = subprocess.run([sys.executable, "-I", TOOL] + list(args), capture_output=True, text=True)
        return r.returncode, r.stdout

    def good_kernel(self):
        return [run_line(), check("T1", "a"), check("T1", "b", n=2), test_line("T1", 2, 0), summary(2, 2, 0)]

    def test_consistent_run_matches_printed_counts(self):
        p = self.write("k.jsonl", self.good_kernel())
        out = self.write("out.txt", [], raw="ok: a\nok: b\n")
        err = self.write("err.txt", [], raw="")
        rc, text = self.validate("--log", "kernel=" + out, "--log", "kernel=" + err, p)
        self.assertEqual(rc, 0, text)
        self.assertIn("EQUAL", text)
        self.assertIn("CONSISTENT", text)

    def test_printed_counts_that_differ_are_reported(self):
        p = self.write("k.jsonl", self.good_kernel())
        out = self.write("out.txt", [], raw="ok: a\nok: b\nok: c\n")
        rc, text = self.validate("--log", "kernel=" + out, p)
        self.assertEqual(rc, 1, text)
        self.assertIn("DIFFERENT", text)

    def test_a_run_without_summary_is_incomplete(self):
        p = self.write("k.jsonl", self.good_kernel()[:-1])
        rc, text = self.validate(p)
        self.assertEqual(rc, 1, text)
        self.assertIn("no summary line", text)

    def test_a_flipped_result_is_caught(self):
        lines = self.good_kernel()
        lines[2]["result"] = "fail"   # the check lines now disagree with the test line and the summary
        rc, text = self.validate(self.write("k.jsonl", lines))
        self.assertEqual(rc, 1, text)

    def test_a_thrown_test_counts_as_one_printed_failure(self):
        lines = [run_line(), check("T1", "a"), test_line("T1", 1, 0, result="throw", error="boom"),
                 summary(1, 1, 0, thrown=1)]
        p = self.write("k.jsonl", lines)
        out = self.write("out.txt", [], raw="ok: a\n")
        err = self.write("err.txt", [], raw="FAILED: T1 threw an uncaught exception instead of completing (boom)\n")
        rc, text = self.validate("--log", "kernel=" + out, "--log", "kernel=" + err, p)
        self.assertEqual(rc, 0, text)
        self.assertIn("throw=1", text)

    def test_malformed_line_is_an_error(self):
        p = self.write("k.jsonl", [], raw=json.dumps(self.good_kernel()[0]) + "\nthis is not json\n")
        rc, text = self.validate(p)
        self.assertEqual(rc, 1, text)
        self.assertIn("not JSON", text)

    def test_no_evidence_is_not_an_error(self):
        os.mkdir(os.path.join(self.tmp.name, "empty"))
        rc, text = self.validate(os.path.join(self.tmp.name, "empty"))
        self.assertEqual(rc, 0, text)
        self.assertIn("NO EVIDENCE", text)

    def test_run_that_matched_no_test_is_flagged(self):
        p = self.write("k.jsonl", [run_line(filter="Typo"), summary(0, 0, 0, tests=0)])
        rc, text = self.validate(p)
        self.assertEqual(rc, 0, text)
        self.assertIn("matched nothing", text)

    def test_shards_that_cover_every_index_once(self):
        lines = []
        for k in range(2):
            lines += [run_line(pid=10 + k, shard="%d/2" % k), check("T%d" % k, "x", pid=10 + k),
                      test_line("T%d" % k, 1, 0, pid=10 + k, idx=k), summary(1, 1, 0, pid=10 + k)]
        rc, text = self.validate(self.write("s.jsonl", lines))
        self.assertEqual(rc, 0, text)
        self.assertIn("all 2 present", text)

    def test_shard_with_a_foreign_index_is_an_error(self):
        lines = [run_line(shard="0/2"), check("T0", "x"), test_line("T0", 1, 0, idx=1), summary(1, 1, 0)]
        rc, text = self.validate(self.write("s.jsonl", lines))
        self.assertEqual(rc, 1, text)
        self.assertIn("not congruent", text)

    def test_app_section_that_never_finished_is_an_error(self):
        lines = [{"kind": "run", "suite": "app", "pid": 5, "job": 0, "sections": ["SME", "HGB"]},
                 check("SME", "SME_a", suite="app", pid=5, job=0, section="SME"),
                 {"kind": "test", "suite": "app", "pid": 5, "job": 0, "test": "SME", "section": "SME", "name": "SME",
                  "result": "fail", "checks": 1, "failed": 0, "incomplete": True},
                 {"kind": "summary", "suite": "app", "pid": 5, "job": 0, "sections": 1, "checks": 1, "ok": 1, "failed": 0,
                  "incomplete": ["SME"]}]
        rc, text = self.validate(self.write("a.jsonl", lines))
        self.assertEqual(rc, 1, text)
        self.assertIn("did not finish", text)
        self.assertIn("never produced a result", text)

    def test_app_section_with_printed_counts(self):
        lines = [{"kind": "run", "suite": "app", "pid": 5, "job": 0, "sections": ["SME"]},
                 check("SME", "SME_a", suite="app", pid=5, job=0, section="SME"),
                 check("SME", "SME_b", result="fail", suite="app", pid=5, job=0, section="SME", n=2),
                 {"kind": "test", "suite": "app", "pid": 5, "job": 0, "test": "SME", "section": "SME", "name": "SME",
                  "result": "fail", "checks": 2, "failed": 1, "ms": 1000},
                 {"kind": "summary", "suite": "app", "pid": 5, "job": 0, "sections": 1, "checks": 2, "ok": 1, "failed": 1,
                  "incomplete": []}]
        p = self.write("a.jsonl", lines)
        log = self.write("group.log", [], raw="ok   SME_a\nFAIL SME_b\n     expected: x\nsection_time SME 1s\n")
        rc, text = self.validate("--log", "app=" + log, p)
        self.assertEqual(rc, 0, text)
        self.assertIn("EQUAL", text)


if __name__ == "__main__":
    unittest.main()
