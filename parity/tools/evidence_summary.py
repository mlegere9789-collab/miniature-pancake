#!/usr/bin/env python3
"""Summarise and verify test evidence (master plan section 7.5).

The kernel test runner writes one JSON line per check and per test to the file named by DINO8_TEST_JSONL
(dino8-kernel/tests/evidence_jsonl.h); the app section runner writes one JSON line per check to the file named by
DINO8_SECTION_JSONL (dino8-app/tests/run_sections.sh, evidence_filter.py). This tool reads those files, prints the counts per
suite and per test / section, and verifies them, so a Register row can be credited from named tests instead of inferred
from counts:

  * every line is a JSON object with a known kind (run, check, test, summary, job);
  * every run that started also finished: it has its summary line (a crash or a kill leaves none) and, for the app, its
    sections each have their test line;
  * the summary and the test lines equal what the check lines add up to;
  * with --log SUITE=FILE (repeatable) the counts equal the PRINTED counts: lines starting `ok` are the printed ok checks and
    lines starting `FAIL` (FAILED and FAIL:) the printed failures, in the files given for that suite. A kernel test that threw
    prints one FAILED line as well, so printed failures = failed checks + thrown tests.
  * with shard information (DINO8_TEST_SHARD=k/n in the run lines) every test's idx is congruent to k modulo n, and when all
    n shards are present every registration index appears exactly once.

usage: evidence_summary.py [--log SUITE=FILE]... [--tests] [--failed] [--json] [--quiet] PATH...
  PATH     a JSONL file, or a directory searched recursively for *.jsonl
  --tests  one line per test (kernel) / section (app): result, checks, failed, ms
  --failed every failed check and thrown or incomplete test, by name
  --json   print the summary as one JSON object instead of text
Exit status: 0 consistent (or no evidence at all), 1 a verification failed (the ERROR lines say which), 2 usage error.
Output lines that start with "evidence " are the ones CI logs carry for kvlite.sh results to pick out.
"""
import argparse
import json
import os
import sys
from collections import OrderedDict, defaultdict

KINDS = ("run", "check", "test", "summary", "job")


class Report:
    def __init__(self):
        self.errors = []      # verification failures
        self.warnings = []    # worth reading, do not fail the verdict

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)


def iter_files(paths, rep):
    seen = []
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for f in sorted(files):
                    if f.endswith(".jsonl"):
                        seen.append(os.path.join(root, f))
        elif os.path.isfile(p):
            seen.append(p)
        else:
            rep.error("%s: no such file or directory" % p)
    return sorted(dict.fromkeys(seen))


def load(files, rep):
    """Returns the records in file order; every record gets '_src' (file:line) and '_file'."""
    recs = []
    for fn in files:
        with open(fn, "rb") as fh:
            for ln, raw in enumerate(fh, 1):
                raw = raw.strip()
                if not raw:
                    continue
                try:
                    obj = json.loads(raw.decode("utf-8", "replace"))
                except ValueError as e:
                    rep.error("%s:%d: not JSON (%s)" % (fn, ln, e))
                    continue
                if not isinstance(obj, dict) or obj.get("kind") not in KINDS or "suite" not in obj:
                    rep.error("%s:%d: not an evidence line (needs kind %s and suite)" % (fn, ln, "/".join(KINDS)))
                    continue
                obj["_src"] = "%s:%d" % (fn, ln)
                obj["_file"] = fn
                recs.append(obj)
    return recs


def group_key(r):
    return (r["_file"], r["suite"], r.get("pid"), r.get("job"))


def printed_counts(specs, rep):
    """--log SUITE=FILE: lines starting 'ok' / 'FAIL' per suite."""
    out = {}
    for spec in specs:
        if "=" not in spec:
            rep.error("--log needs SUITE=FILE, got %r" % spec)
            continue
        suite, fn = spec.split("=", 1)
        try:
            with open(fn, "rb") as fh:
                data = fh.read()
        except OSError as e:
            rep.error("--log %s: %s" % (spec, e))
            continue
        c = out.setdefault(suite, {"ok": 0, "fail": 0, "files": []})
        c["files"].append(fn)
        for line in data.split(b"\n"):
            if line.startswith(b"ok"):
                c["ok"] += 1
            elif line.startswith(b"FAIL"):
                c["fail"] += 1
    return out


def analyse(recs, logs, rep):
    suites = OrderedDict()   # suite -> stats
    groups = OrderedDict()   # (file, suite, pid, job) -> dict
    for r in recs:
        k = group_key(r)
        g = groups.setdefault(k, {"run": None, "summary": None, "job": None, "checks": [], "tests": [], "src": r["_src"]})
        kind = r["kind"]
        if kind == "run":
            if g["run"] is not None:
                rep.error("%s: a second run line for the same process (%s)" % (r["_src"], g["run"]["_src"]))
            g["run"] = r
        elif kind == "summary":
            g["summary"] = r
        elif kind == "job":
            g["job"] = r
        elif kind == "check":
            g["checks"].append(r)
        elif kind == "test":
            g["tests"].append(r)

    per_test = OrderedDict()  # (suite, test) -> counters, across all groups
    for k, g in groups.items():
        _file, suite, pid, job = k
        st = suites.setdefault(suite, {"runs": 0, "tests": 0, "checks": 0, "ok": 0, "fail": 0, "throw": 0, "incomplete": 0,
                                       "ms": 0.0, "shas": set(), "files": set(), "no_check_tests": []})
        st["runs"] += 1
        st["files"].add(_file)
        label = "%s pid %s%s" % (suite, pid, "" if job is None else " job %s" % job)
        if g["run"] is None:
            rep.warn("%s (%s): no run line" % (label, g["src"]))
        else:
            if g["run"].get("sha"):
                st["shas"].add(g["run"]["sha"])
        # checks
        nck = nok = nfail = 0
        by_test = defaultdict(lambda: [0, 0])
        for c in g["checks"]:
            res = c.get("result")
            nck += 1
            if res == "ok":
                nok += 1
            elif res == "fail":
                nfail += 1
            else:
                rep.error("%s: check result %r is neither ok nor fail" % (c["_src"], res))
            t = c.get("test", "")
            by_test[t][0] += 1
            if res == "fail":
                by_test[t][1] += 1
        st["checks"] += nck
        st["ok"] += nok
        st["fail"] += nfail
        # tests
        nthrow = 0
        listed = set()
        for t in g["tests"]:
            name = t.get("test", "")
            res = t.get("result")
            if res not in ("ok", "fail", "throw"):
                rep.error("%s: test result %r is not ok, fail or throw" % (t["_src"], res))
            if name in listed:
                rep.error("%s: test %s recorded twice in one run" % (t["_src"], name))
            listed.add(name)
            n_c, n_f = by_test.get(name, [0, 0])
            if t.get("checks") != n_c or t.get("failed") != n_f:
                rep.error("%s: test %s says checks=%s failed=%s but its check lines are %d and %d" %
                          (t["_src"], name, t.get("checks"), t.get("failed"), n_c, n_f))
            if res == "ok" and n_f:
                rep.error("%s: test %s is ok but has %d failed check line(s)" % (t["_src"], name, n_f))
            if res == "throw":
                nthrow += 1
            if t.get("incomplete"):
                st["incomplete"] += 1
            if n_c == 0:
                st["no_check_tests"].append(name)
            st["tests"] += 1
            st["ms"] += float(t.get("ms") or 0)
            key = (suite, name)
            pt = per_test.setdefault(key, {"results": [], "checks": 0, "failed": 0, "ms": 0.0, "runs": 0, "error": None,
                                           "incomplete": False})
            pt["results"].append(res)
            pt["checks"] += n_c
            pt["failed"] += n_f
            pt["ms"] += float(t.get("ms") or 0)
            pt["runs"] += 1
            if t.get("error"):
                pt["error"] = t["error"]
            if t.get("incomplete"):
                pt["incomplete"] = True
        st["throw"] += nthrow
        orphan = [name for name in by_test if name not in listed]
        for name in orphan:
            what = "outside any test" if name == "" else "in %s, which has no test line" % name
            if suite == "app" and name == "harness":
                rep.warn("%s: %d check line(s) printed by the harness before the first section" % (label, by_test[name][0]))
            elif suite == "app" and name != "":
                rep.error("%s: %d check line(s) %s (the section never finished its test line)" % (label, by_test[name][0], what))
            elif name == "":
                rep.warn("%s: %d check line(s) %s" % (label, by_test[name][0], what))
            else:
                rep.error("%s: %d check line(s) %s" % (label, by_test[name][0], what))
        if suite != "app" and not g["tests"]:
            r0 = g["run"] or {}
            rep.warn("%s: the run executed no test at all (filter %r, shard %r): the selection matched nothing" %
                     (label, r0.get("filter", ""), r0.get("shard", "")))
            st["empty_runs"] = st.get("empty_runs", 0) + 1
        # completeness
        s = g["summary"]
        if s is None:
            rep.error("%s (%s): no summary line - the run did not finish (crash or kill)" % (label, g["src"]))
        else:
            for fld, have in (("checks", nck), ("ok", nok), ("failed", nfail)):
                if s.get(fld) != have:
                    rep.error("%s: summary %s=%s but the check lines add up to %d" % (s["_src"], fld, s.get(fld), have))
            if suite != "app":
                if s.get("tests") != len(g["tests"]):
                    rep.error("%s: summary tests=%s but there are %d test lines" % (s["_src"], s.get("tests"), len(g["tests"])))
                if s.get("thrown") != nthrow:
                    rep.error("%s: summary thrown=%s but %d test lines say throw" % (s["_src"], s.get("thrown"), nthrow))
            else:
                if s.get("sections") != len(g["tests"]):
                    rep.error("%s: summary sections=%s but there are %d section lines" % (s["_src"], s.get("sections"), len(g["tests"])))
                if s.get("incomplete"):
                    rep.error("%s: sections %s did not finish (no section_time line)" % (s["_src"], ", ".join(s["incomplete"])))
        if suite == "app" and g["run"] is not None:
            planned = g["run"].get("sections") or []
            started = [t.get("test") for t in g["tests"]]
            never = [x for x in planned if x not in started]
            if never:
                rep.error("%s: sections %s were selected but never produced a result (the job ended earlier)" % (label, ", ".join(never)))
            j = g["job"]
            if j is not None and j.get("exit") not in (0, 1):
                rep.warn("%s: the job exited with status %s (124 = timeout)" % (label, j.get("exit")))
    # printed counts
    verdicts = []
    for suite, c in logs.items():
        st = suites.get(suite)
        have_ok = st["ok"] if st else 0
        have_fail = (st["fail"] + st["throw"]) if st else 0
        good = c["ok"] == have_ok and c["fail"] == have_fail
        verdicts.append({"suite": suite, "printed_ok": c["ok"], "printed_fail": c["fail"], "json_ok": have_ok,
                         "json_fail": have_fail, "equal": good})
        if not good:
            rep.error("%s: printed ok=%d FAIL=%d but the JSONL has ok=%d fail+throw=%d" %
                      (suite, c["ok"], c["fail"], have_ok, have_fail))
    for suite in logs:
        if suite not in suites:
            rep.error("--log %s: no evidence lines for that suite" % suite)
    # shards
    shard_runs = defaultdict(dict)  # (suite, n) -> {k: [groups]}
    for k, g in groups.items():
        r = g["run"]
        if r is None or not r.get("shard"):
            continue
        try:
            kk, nn = [int(x) for x in str(r["shard"]).split("/")]
        except ValueError:
            rep.warn("%s: unreadable shard %r" % (r["_src"], r["shard"]))
            continue
        shard_runs[(k[1], nn)].setdefault(kk, []).append(g)
        for t in g["tests"]:
            if t.get("idx") is not None and t["idx"] % nn != kk:
                rep.error("%s: test %s has idx %s, not congruent to shard %d/%d" % (t["_src"], t.get("test"), t["idx"], kk, nn))
    shard_info = []
    for (suite, nn), by_k in shard_runs.items():
        missing = [x for x in range(nn) if x not in by_k]
        info = {"suite": suite, "of": nn, "present": sorted(by_k), "missing": missing}
        if not missing:
            idxs = defaultdict(list)
            for kk, gl in by_k.items():
                for g in gl:
                    for t in g["tests"]:
                        if t.get("idx") is not None:
                            idxs[t["idx"]].append(t.get("test"))
            dup = {i: v for i, v in idxs.items() if len(v) > 1}
            gaps = [i for i in range(max(idxs) + 1) if i not in idxs] if idxs else []
            info["tests"] = len(idxs)
            info["duplicate_idx"] = len(dup)
            info["gaps"] = len(gaps)
            if dup:
                rep.error("%s: %d registration index(es) ran in more than one shard, e.g. %s" % (suite, len(dup), next(iter(dup.values()))))
            if gaps:
                rep.error("%s: registration index(es) %s ran in no shard" % (suite, gaps[:10]))
        shard_info.append(info)
    return suites, groups, per_test, verdicts, shard_info


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", metavar="PATH")
    ap.add_argument("--log", action="append", default=[], metavar="SUITE=FILE")
    ap.add_argument("--tests", action="store_true")
    ap.add_argument("--failed", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)

    rep = Report()
    files = iter_files(a.paths, rep)
    recs = load(files, rep)
    logs = printed_counts(a.log, rep)
    suites, groups, per_test, verdicts, shard_info = analyse(recs, logs, rep)
    ok = not rep.errors

    if a.json:
        out = {"files": files, "suites": {}, "printed": verdicts, "shards": shard_info, "errors": rep.errors,
               "warnings": rep.warnings, "verified": ok}
        for suite, st in suites.items():
            out["suites"][suite] = {k: (sorted(v) if isinstance(v, set) else v) for k, v in st.items()}
            out["suites"][suite]["items"] = {name: pt for (s, name), pt in per_test.items() if s == suite}
        json.dump(out, sys.stdout, indent=1, sort_keys=True)
        sys.stdout.write("\n")
        return 0 if ok else 1

    p = print
    if not files or not recs:
        p("evidence: no evidence lines found in %s (the test runner of this branch does not write DINO8_TEST_JSONL / "
          "DINO8_SECTION_JSONL, or no test ran)" % ", ".join(a.paths))
    for suite, st in suites.items():
        unit = "sections" if suite == "app" else "tests"
        p("evidence %s: files=%d runs=%d %s=%d checks=%d ok=%d fail=%d%s%s%s%s" %
          (suite, len(st["files"]), st["runs"], unit, st["tests"], st["checks"], st["ok"], st["fail"],
           " throw=%d" % st["throw"] if suite != "app" else "",
           " incomplete=%d" % st["incomplete"] if st["incomplete"] else "",
           " EMPTY-RUNS=%d" % st["empty_runs"] if st.get("empty_runs") else "",
           " sha=%s" % ",".join(sorted(x[:12] for x in st["shas"])) if st["shas"] else ""))
        if st["no_check_tests"] and not a.quiet:
            names = st["no_check_tests"]
            p("evidence %s: %d %s ran without a single check: %s%s" %
              (suite, len(names), unit, ", ".join(names[:8]), " ..." if len(names) > 8 else ""))
    for v in verdicts:
        p("evidence printed %s: printed ok=%d FAIL=%d, JSONL ok=%d fail+throw=%d: %s" %
          (v["suite"], v["printed_ok"], v["printed_fail"], v["json_ok"], v["json_fail"], "EQUAL" if v["equal"] else "DIFFERENT"))
    for si in shard_info:
        if si["missing"]:
            p("evidence %s shards: %d of %d present (missing %s)" % (si["suite"], len(si["present"]), si["of"], si["missing"]))
        else:
            p("evidence %s shards: all %d present, %d registration indexes, %d duplicated, %d missing" %
              (si["suite"], si["of"], si.get("tests", 0), si.get("duplicate_idx", 0), si.get("gaps", 0)))
    if a.tests:
        for (suite, name), pt in per_test.items():
            res = pt["results"][0] if len(set(pt["results"])) == 1 else "mixed(%s)" % ",".join(pt["results"])
            p("evidence %s %s %s checks=%d failed=%d ms=%.0f%s%s" % (
                "section" if suite == "app" else "test", name, res, pt["checks"], pt["failed"], pt["ms"],
                " runs=%d" % pt["runs"] if pt["runs"] > 1 else "",
                " error=%s" % pt["error"][:160] if pt["error"] else ""))
    if a.failed:
        for r in recs:
            if r["kind"] == "check" and r.get("result") == "fail":
                p("evidence FAILED %s %s: %s" % (r["suite"], r.get("test") or "-", r.get("name", "")))
            elif r["kind"] == "test" and (r.get("result") == "throw" or r.get("incomplete")):
                p("evidence %s %s %s: %s" % ("THREW" if r.get("result") == "throw" else "INCOMPLETE", r["suite"], r.get("test"),
                                             r.get("error", "")))
    if not a.quiet:
        for w in rep.warnings:
            p("evidence warning: %s" % w)
    for e in rep.errors:
        p("evidence ERROR: %s" % e)
    checked = ["lines", "summaries"]
    if verdicts:
        checked.append("printed counts")
    if any(not si["missing"] for si in shard_info):
        checked.append("shard coverage")
    if not recs and not rep.errors:
        p("evidence verdict: NO EVIDENCE")
    elif ok:
        p("evidence verdict: CONSISTENT (%s agree; this says nothing about whether the tests passed, see fail= above)" % ", ".join(checked))
    else:
        p("evidence verdict: INCONSISTENT (%d problem(s) above)" % len(rep.errors))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
