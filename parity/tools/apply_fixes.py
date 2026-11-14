#!/usr/bin/env python3
"""Apply a fixes file to the Parity Register, deterministically.

    python3 parity/tools/apply_fixes.py FIXES.jsonl [FIXES2.jsonl ...]
        [--register DIR] [--dry-run]

Each line of a fixes file is {"id", "field", "old", "new", "reason"}:

  id      row id; it must exist exactly once in the Register
  field   top-level field ("statement", "factory", "target", "notes", ...) or a dotted path
          into nested objects ("source.ref", "source.doc")
  old     the value the field must have right now; if it differs the whole run is refused
  new     the replacement value
  reason  why (required, not stored in the row)

Fixes are applied in file order, so two fixes of one field chain (the second one's old is
the first one's new). The run is all or nothing: if any fix is refused (unknown id, missing
field, old value does not match) nothing is written and the exit status is 1. Rows are
rewritten one JSON object per line with the same serialisation the Register uses
(json.dumps, ensure_ascii False), so a fix changes only the lines it touches.
Exit status: 0 applied (or dry run clean), 1 refused, 2 usage or input problem.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regio  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_REGISTER = os.path.join(REPO_ROOT, "parity", "register")
_MISSING = object()


class FixError(Exception):
    pass


def load_fixes(path):
    fixes = []
    with open(path, "r", encoding="utf-8-sig") as handle:
        for number, text in enumerate(handle, 1):
            if not text.strip():
                continue
            try:
                fix = json.loads(text)
            except ValueError as exc:
                raise FixError("%s:%d: not valid JSON: %s" % (path, number, exc))
            if not isinstance(fix, dict):
                raise FixError("%s:%d: a fix must be a JSON object" % (path, number))
            for key in ("id", "field", "old", "new", "reason"):
                if key not in fix:
                    raise FixError("%s:%d: missing key %r" % (path, number, key))
            if not isinstance(fix["field"], str) or not fix["field"]:
                raise FixError("%s:%d: field must be a non-empty string" % (path, number))
            fix["_where"] = "%s:%d" % (path, number)
            fixes.append(fix)
    return fixes


def _get(row, parts):
    node = row
    for part in parts:
        if not isinstance(node, dict) or part not in node:
            return _MISSING
        node = node[part]
    return node


def _set(row, parts, value):
    node = row
    for part in parts[:-1]:
        node = node[part]
    node[parts[-1]] = value


def serialise(row):
    return json.dumps(row, ensure_ascii=False)


def apply_fixes(register, fixes):
    """Apply fixes to the rows below register. Returns (files_changed, fixes_applied).

    Raises FixError (and writes nothing) if any fix cannot be applied exactly.
    """
    records, problems = regio.read_register(register)
    bad = [p for p in problems if p["severity"] == "error"]
    if bad:
        raise FixError("register is not readable: %s:%s %s" % (bad[0]["file"], bad[0]["line"], bad[0]["message"]))
    by_id = {}
    for record in records:
        row_id = record["row"].get("id")
        if row_id in by_id:
            raise FixError("duplicate id %s in the register; refusing" % row_id)
        by_id[row_id] = record

    refusals = []
    touched = set()
    applied = 0
    for fix in fixes:
        record = by_id.get(fix["id"])
        if record is None:
            refusals.append("%s: unknown id %s" % (fix["_where"], fix["id"]))
            continue
        parts = fix["field"].split(".")
        current = _get(record["row"], parts)
        if current is _MISSING:
            refusals.append("%s: %s has no field %s" % (fix["_where"], fix["id"], fix["field"]))
            continue
        if not regio_equal(current, fix["old"]):
            hint = " (already applied?)" if regio_equal(current, fix["new"]) else ""
            refusals.append(
                "%s: %s.%s does not match old%s: current %s"
                % (fix["_where"], fix["id"], fix["field"], hint, _short(current))
            )
            continue
        _set(record["row"], parts, fix["new"])
        touched.add(record["file"])
        applied += 1
    if refusals:
        raise FixError("\n".join(refusals))

    for name in sorted(touched):
        rows = [r["row"] for r in records if r["file"] == name]
        text = "".join(serialise(r) + "\n" for r in rows)
        tmp = name + ".tmp"
        with open(tmp, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        os.replace(tmp, name)
    return len(touched), applied


def regio_equal(left, right):
    return json.dumps(left, sort_keys=True) == json.dumps(right, sort_keys=True)


def _short(value):
    text = json.dumps(value, ensure_ascii=False)
    return text if len(text) <= 80 else text[:77] + "..."


def run(argv=None, out=None, err=None):
    out = out or sys.stdout
    err = err or sys.stderr
    parser = argparse.ArgumentParser(description="Apply fixes to the Parity Register.")
    parser.add_argument("fixes", nargs="+", help="fixes JSONL file(s)")
    parser.add_argument("--register", default=DEFAULT_REGISTER)
    parser.add_argument("--dry-run", action="store_true", help="check only, write nothing")
    args = parser.parse_args(argv)
    if not os.path.isdir(args.register):
        print("apply_fixes: no such register directory: %s" % args.register, file=err)
        return 2
    try:
        fixes = []
        for path in args.fixes:
            fixes.extend(load_fixes(path))
    except (OSError, FixError) as exc:
        print("apply_fixes: %s" % exc, file=err)
        return 2
    register = args.register
    if args.dry_run:
        import shutil
        import tempfile

        with tempfile.TemporaryDirectory(prefix="apply-fixes-") as tmp:
            copy = os.path.join(tmp, "register")
            shutil.copytree(register, copy)
            register = copy
            return _go(register, fixes, out, err, dry=True)
    return _go(register, fixes, out, err, dry=False)


def _go(register, fixes, out, err, dry):
    try:
        files, applied = apply_fixes(register, fixes)
    except FixError as exc:
        print("apply_fixes: REFUSED, nothing written:\n%s" % exc, file=err)
        return 1
    out.write(
        "apply_fixes: %s %d fixes in %d files\n"
        % ("would apply" if dry else "applied", applied, files)
    )
    return 0


def main():
    sys.exit(run())


if __name__ == "__main__":
    main()
