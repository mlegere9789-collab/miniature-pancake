#!/usr/bin/env python3
"""Normalise Parity Register files so that they pass `validate_register.py --strict`.

    python3 parity/tools/normalize_register.py [--register DIR] [--check] [--validate]
        [--quiet]

Two repairs, both lossless:

  1. Acceptance items may only carry test_id, corpus and tolerance (row schema). Any other
     key, in practice the free-text `sketch`, is moved into the row's `notes` as
     "Acceptance sketch: <text>" (appended after the existing notes, one space between).
     With more than one item the text is labelled with the item's test id:
     "Acceptance sketch (<test_id>): <text>". A missing or blank test_id is created as
     acc.<program>.<domain>.<nnnn> (nnnn from the row id; ".<k>" is added for the second and
     later items of a row, and a counter if the id would still collide).
  2. A row lives in parity/register/<program>/<domain>.jsonl: the file name is the row's
     domain (characters outside A-Za-z0-9._- become "_"). Rows are regrouped into those
     files (several source files with the same domain are merged, in sorted source-file
     order, then line order) and source files left without rows are removed. The directory
     stays when it is one of the row's programs, otherwise it becomes the first program.

Nothing else about a row changes: statements, ids, keys and key order are kept, and a row
that needs no repair keeps its original line byte for byte. The result depends only on the
input rows, and a second run changes nothing (idempotent). A line that is not a JSON
object stops the run: a row is never dropped.

Before the files are written the tool proves that the multiset of row ids is unchanged and
that ids are unique across all programs; it prints a before/after row count per program.
--check writes nothing and exits 1 when a repair is pending. --validate finally runs the
strict validator on the result and passes on its exit status.

Exit status: 0 done (or clean with --check), 1 repairs pending (--check) or validation
failed, 2 input problem.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter, OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REGISTER = os.path.join(os.path.dirname(HERE), "register")
ALLOWED_ACCEPTANCE_KEYS = ("test_id", "corpus", "tolerance")
SKETCH_LABEL = "Acceptance sketch"


class NormalizeError(Exception):
    pass


def _norm(text):
    return re.sub(r"[^a-z0-9]+", "-", str(text).lower()).strip("-")


def _slug(text):
    return re.sub(r"[^a-z0-9]+", "-", str(text).lower()).strip("-") or "x"


def _file_stem(domain):
    return re.sub(r"[^A-Za-z0-9._-]", "_", domain)


def _text(value):
    if isinstance(value, str):
        return value.strip()
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def _row_number(row):
    match = re.search(r"-([0-9]+)$", str(row.get("id", "")))
    return match.group(1) if match else "0000"


def _first_program(row):
    programs = row.get("program")
    if isinstance(programs, list) and programs and isinstance(programs[0], str):
        return programs[0]
    return None


def read_rows(register):
    """[(relative_path, line_number, raw_text, row)] over every *.jsonl, sorted."""
    found = []
    for dirpath, dirnames, filenames in os.walk(register):
        dirnames.sort()
        for name in sorted(filenames):
            if not name.endswith(".jsonl"):
                continue
            path = os.path.join(dirpath, name)
            relative = os.path.relpath(path, register).replace(os.sep, "/")
            with open(path, "r", encoding="utf-8-sig") as handle:
                lines = handle.read().split("\n")
            for number, text in enumerate(lines, 1):
                if text.strip() == "":
                    continue
                try:
                    row = json.loads(text)
                except ValueError as exc:
                    raise NormalizeError(
                        "%s:%d is not valid JSON (%s)" % (relative, number, exc)
                    )
                if not isinstance(row, dict):
                    raise NormalizeError(
                        "%s:%d is not a JSON object" % (relative, number)
                    )
                found.append((relative, number, text, row))
    return found


def normalize_row(row, taken_tests):
    """Return (row, changed). Moves extra acceptance keys into notes, fills test ids."""
    acceptance = row.get("acceptance")
    if not isinstance(acceptance, list):
        return row, False
    program = _first_program(row) or "x"
    domain = row.get("domain") if isinstance(row.get("domain"), str) else "x"
    items = []
    sketches = []
    changed = False
    for index, item in enumerate(acceptance):
        if not isinstance(item, dict):
            items.append(item)
            continue
        extras = [key for key in item if key not in ALLOWED_ACCEPTANCE_KEYS]
        test_id = item.get("test_id")
        needs_id = not isinstance(test_id, str) or test_id.strip() == ""
        if not extras and not needs_id:
            items.append(item)
            continue
        changed = True
        new = OrderedDict()
        if needs_id:
            base = "acc.%s.%s.%s" % (_slug(program), _slug(domain), _row_number(row))
            if index:
                base = "%s.%d" % (base, index + 1)
            test_id = base
            counter = 2
            while test_id in taken_tests:
                test_id = "%s-%d" % (base, counter)
                counter += 1
            taken_tests.add(test_id)
            new["test_id"] = test_id
        for key, value in item.items():
            if key in ALLOWED_ACCEPTANCE_KEYS:
                new[key] = value
        if needs_id:
            # test_id first, as written everywhere else
            new = OrderedDict(
                [("test_id", new["test_id"])]
                + [(k, v) for k, v in new.items() if k != "test_id"]
            )
        for key in extras:
            text = _text(item[key])
            if text == "":
                continue
            if key != "sketch":
                text = "%s: %s" % (key, text)
            sketches.append((new["test_id"] if "test_id" in new else test_id, text))
        items.append(new)
    if not changed:
        return row, False
    out = OrderedDict()
    for key, value in row.items():
        out[key] = items if key == "acceptance" else value
    if sketches:
        parts = []
        for test_id, text in sketches:
            if len(acceptance) > 1:
                parts.append("%s (%s): %s" % (SKETCH_LABEL, test_id, text))
            else:
                parts.append("%s: %s" % (SKETCH_LABEL, text))
        notes = out.get("notes")
        notes = notes if isinstance(notes, str) else ""
        for part in parts:
            if part not in notes:
                notes = (notes + " " + part) if notes.strip() else part
        out["notes"] = notes
    return out, True


def target_path(relative, row):
    """parity/register-relative path for a row: <program>/<domain>.jsonl."""
    domain = row.get("domain")
    program = _first_program(row)
    parts = relative.split("/")
    if not isinstance(domain, str) or domain.strip() == "" or program is None:
        return relative
    directory = parts[0] if len(parts) == 2 else None
    programs = row.get("program")
    if directory is None or _norm(directory) not in [_norm(p) for p in programs]:
        directory = program
    return "%s/%s.jsonl" % (directory, _file_stem(domain))


def plan(register):
    """Compute the normalised file contents. Returns (before_rows, groups, stats)."""
    source = read_rows(register)
    taken_tests = set()
    for _rel, _num, _raw, row in source:
        for item in row.get("acceptance") or []:
            if isinstance(item, dict) and isinstance(item.get("test_id"), str):
                taken_tests.add(item["test_id"])
    groups = OrderedDict()
    changed_rows = 0
    for relative, _number, raw, row in source:
        new_row, changed = normalize_row(row, taken_tests)
        text = json.dumps(new_row, ensure_ascii=False) if changed else raw
        changed_rows += 1 if changed else 0
        groups.setdefault(target_path(relative, new_row), []).append((text, new_row))
    return source, groups, changed_rows


def prove(source, groups):
    """No row lost, ids unique across programs. Raises NormalizeError otherwise."""
    before = Counter(str(r.get("id")) for _a, _b, _c, r in source)
    after = Counter(str(r.get("id")) for rows in groups.values() for _t, r in rows)
    if before != after:
        raise NormalizeError("row ids differ before and after normalising")
    duplicates = sorted(i for i, n in after.items() if n > 1)
    if duplicates:
        raise NormalizeError(
            "%d duplicate row ids across programs (first: %s)"
            % (len(duplicates), ", ".join(duplicates[:5]))
        )


def per_program(rows):
    counts = Counter()
    for row in rows:
        programs = row.get("program")
        key = "+".join(programs) if isinstance(programs, list) and programs else "?"
        counts[key] += 1
    return counts


def print_table(before, after, out):
    names = sorted(set(before) | set(after))
    width = max([len(n) for n in names] + [7])
    out.write("%-*s %8s %8s %6s\n" % (width, "program", "before", "after", "lost"))
    for name in names:
        out.write(
            "%-*s %8d %8d %6d\n"
            % (width, name, before[name], after[name], max(0, before[name] - after[name]))
        )
    out.write(
        "%-*s %8d %8d %6d\n"
        % (
            width,
            "total",
            sum(before.values()),
            sum(after.values()),
            max(0, sum(before.values()) - sum(after.values())),
        )
    )


def write_groups(register, groups):
    wanted = {}
    for relative, rows in groups.items():
        wanted[relative] = "".join(text + "\n" for text, _row in rows)
    existing = set()
    for dirpath, dirnames, filenames in os.walk(register):
        for name in filenames:
            if name.endswith(".jsonl"):
                existing.add(
                    os.path.relpath(os.path.join(dirpath, name), register).replace(
                        os.sep, "/"
                    )
                )
    for relative, content in sorted(wanted.items()):
        path = os.path.join(register, *relative.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        old = None
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as handle:
                old = handle.read()
        if old != content:
            with open(path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(content)
    for relative in sorted(existing - set(wanted)):
        os.remove(os.path.join(register, *relative.split("/")))


def disk_files(register):
    found = {}
    for dirpath, dirnames, filenames in os.walk(register):
        for name in filenames:
            if name.endswith(".jsonl"):
                path = os.path.join(dirpath, name)
                relative = os.path.relpath(path, register).replace(os.sep, "/")
                with open(path, "r", encoding="utf-8") as handle:
                    found[relative] = handle.read()
    return found


def pending(register, groups):
    """True when writing groups would change anything on disk."""
    wanted = {r: "".join(t + "\n" for t, _row in rows) for r, rows in groups.items()}
    return disk_files(register) != wanted


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--register", default=DEFAULT_REGISTER)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    register = os.path.abspath(args.register)
    if not os.path.isdir(register):
        sys.stderr.write("not a directory: %s\n" % register)
        return 2
    try:
        source, groups, changed_rows = plan(register)
        prove(source, groups)
    except NormalizeError as exc:
        sys.stderr.write("normalize_register: %s\n" % exc)
        return 2
    before = per_program(r for _a, _b, _c, r in source)
    after = per_program(row for rows in groups.values() for _t, row in rows)
    needs = pending(register, groups)
    if not args.quiet:
        print_table(before, after, sys.stdout)
        print(
            "rows repaired: %d; files: %d -> %d; %s"
            % (
                changed_rows,
                len({s[0] for s in source}),
                len(groups),
                "changes pending" if needs else "already normalised",
            )
        )
    if args.check:
        return 1 if needs else 0
    if needs:
        write_groups(register, groups)
    if args.validate:
        validator = os.path.join(HERE, "validate_register.py")
        result = subprocess.run(
            [sys.executable, validator, "--register", register, "--strict"]
        )
        return 1 if result.returncode else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
