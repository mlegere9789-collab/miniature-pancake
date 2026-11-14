#!/usr/bin/env python3
"""Evidence stream filter for run_sections.sh (master plan section 7.5).

run_sections.sh pipes the output of every job through this filter when DINO8_SECTION_JSONL names a file. The filter

  * copies its input to its output unchanged, line by line, except for the section markers the assembled script prints
    only in this mode (`@@d8ev-section NAME`, see run_sections.sh): those are consumed here, so the log a job leaves is
    byte for byte what it is without evidence;
  * appends ONE JSON line per check to the file, the moment the line goes by. A check is any line the CI counters count:
    starting with `ok` (result ok) or `FAIL` (result fail), whoever printed it (check(), every section helper, and the app's own
    `ok   expect_...` lines that smoke.sh passes through);
  * appends one `test` line per section when the section's own `section_time NAME Ns` line goes by.

Line formats (suite "app"; `test` and `section` carry the same section name, `name` is the check label):

  {"kind":"run","suite":"app","pid":P,"job":J,"sections":["SME","HGB"],"sha":"..."}
  {"kind":"check","suite":"app","pid":P,"job":J,"test":"SME","section":"SME","name":"SME_close: Check valid","result":"ok","n":3}
  {"kind":"test","suite":"app","pid":P,"job":J,"test":"SME","section":"SME","name":"SME","result":"ok","checks":9,"failed":0,"ms":1000}
  {"kind":"summary","suite":"app","pid":P,"job":J,"sections":1,"checks":9,"ok":9,"failed":0,"incomplete":[]}

`strict` is false (and only then present) for a line that starts with ok / FAIL but is not the usual `ok   <label>` or
`FAIL <label>` / `FAIL: <label>` form. A section that never printed its `section_time` line (the script exited or was killed
inside it) gets a `test` line with result "fail" and "incomplete":true. Evidence is best effort: if the file cannot be
written the filter keeps copying and says so once on stderr, it never stops a run.

usage: evidence_filter.py --jsonl FILE [--pid N] [--job N] [--sections "A B C"] [--sha SHA]
"""
import argparse
import json
import os
import re
import sys

MARK = b"@@d8ev-section "
SECTION_TIME = re.compile(rb"^section_time (\S+) (\d+)s\s*$")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jsonl", required=True)
    ap.add_argument("--pid", type=int, default=os.getpid())
    ap.add_argument("--job", type=int, default=0)
    ap.add_argument("--sections", default="")
    ap.add_argument("--sha", default="")
    a = ap.parse_args()

    out = sys.stdout.buffer
    ev = None
    try:
        ev = open(a.jsonl, "ab", buffering=0)
    except OSError as e:
        sys.stderr.write("evidence_filter: cannot open %s for appending (%s); no evidence will be written\n" % (a.jsonl, e))

    def emit(obj):
        nonlocal ev
        if ev is None:
            return
        base = {"kind": obj.pop("kind"), "suite": "app", "pid": a.pid, "job": a.job}
        base.update(obj)
        try:
            ev.write((json.dumps(base, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
        except OSError as e:
            sys.stderr.write("evidence_filter: write to %s failed (%s); evidence stops here\n" % (a.jsonl, e))
            ev = None

    run = {"kind": "run", "sections": a.sections.split()}
    sha = a.sha or os.environ.get("DINO8_EVIDENCE_SHA") or os.environ.get("GITHUB_SHA") or ""
    if sha:
        run["sha"] = sha
    emit(run)

    cur = None      # name of the section being run; None before the first marker (the harness part of the script)
    n = ok = failed = 0   # of the current section
    t_ok = t_failed = t_checks = 0
    done = 0
    incomplete = []

    def close_incomplete():
        nonlocal cur
        if cur is not None:
            incomplete.append(cur)
            emit({"kind": "test", "test": cur, "section": cur, "name": cur, "result": "fail", "checks": n, "failed": failed,
                  "incomplete": True})
            cur = None

    stdin = sys.stdin.buffer
    while True:
        line = stdin.readline()
        if not line:
            break
        if line.startswith(MARK):
            close_incomplete()
            cur = line[len(MARK):].strip().decode("utf-8", "replace")
            n = ok = failed = 0
            continue                      # the marker never reaches the log
        try:
            out.write(line)
            out.flush()
        except OSError:
            pass                          # a closed log must not stop the run's own bookkeeping
        isok = line.startswith(b"ok")
        if isok or line.startswith(b"FAIL"):
            text = line.rstrip(b"\r\n")
            if isok:
                m = re.match(rb"ok {3}(.*)$", text, re.S)
                strict = m is not None
                if m is None:
                    m = re.match(rb"ok[: ]+(.*)$", text, re.S)       # `ok: label`, `ok label`; `okay ...` keeps its whole line
            else:
                m = re.match(rb"FAIL(?::| )(?: )?(.*)$", text, re.S)
                strict = m is not None
                if m is None:
                    m = re.match(rb"FAIL[: ]+(.*)$", text, re.S)
            name = (m.group(1) if m else text).decode("utf-8", "replace")
            section = cur if cur is not None else "harness"
            n += 1
            t_checks += 1
            if isok:
                ok += 1
                t_ok += 1
            else:
                failed += 1
                t_failed += 1
            rec = {"kind": "check", "test": section, "section": section, "name": name, "result": "ok" if isok else "fail", "n": n}
            if not strict:
                rec["strict"] = False
            emit(rec)
            continue
        m = SECTION_TIME.match(line)
        if m and cur is not None and m.group(1).decode("utf-8", "replace") == cur:
            emit({"kind": "test", "test": cur, "section": cur, "name": cur, "result": "fail" if failed else "ok", "checks": n,
                  "failed": failed, "ms": int(m.group(2)) * 1000})
            done += 1
            cur = None

    close_incomplete()
    emit({"kind": "summary", "sections": done + len(incomplete), "checks": t_checks, "ok": t_ok, "failed": t_failed,
          "incomplete": incomplete})
    if ev is not None:
        ev.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(0)
