"""Shared reading helpers for the Parity Register (parity/register/**/*.jsonl).

The Register is the only definition of progress (plan 4.1). One row is one JSON object on
one line. This module only reads and hashes rows; the checks live in validate_register.py
and the counting lives in percent.py.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

TIERS = ("None", "Basic", "Partial", "Strong", "Best")
TIER_RANK = {name: rank for rank, name in enumerate(TIERS)}
CREDITED_TIERS = ("Strong", "Best")


def register_files(root):
    """Every *.jsonl file below root, in a stable (sorted) order."""
    found = []
    for dirpath, dirnames, filenames in os.walk(str(root)):
        dirnames.sort()
        for name in sorted(filenames):
            if name.endswith(".jsonl"):
                found.append(Path(dirpath) / name)
    return found


def read_register(root):
    """Read every row below root.

    Returns (records, problems). A record is {"file", "line", "row"} with the file path as
    given (root joined to the relative part). A problem is {"file", "line", "code",
    "message"} for a line that is not a JSON object; such a line yields no record.
    """
    records = []
    problems = []
    for path in register_files(root):
        with open(str(path), "r", encoding="utf-8-sig") as handle:
            lines = handle.read().split("\n")
        # A final empty element is just the trailing newline.
        if lines and lines[-1] == "":
            lines.pop()
        for number, text in enumerate(lines, 1):
            if text.strip() == "":
                problems.append(
                    {
                        "file": str(path),
                        "line": number,
                        "code": "blank-line",
                        "message": "blank line in a JSONL file",
                        "severity": "warning",
                    }
                )
                continue
            try:
                row = json.loads(text)
            except ValueError as exc:
                problems.append(
                    {
                        "file": str(path),
                        "line": number,
                        "code": "bad-json",
                        "message": "not valid JSON: %s" % exc,
                        "severity": "error",
                    }
                )
                continue
            if not isinstance(row, dict):
                problems.append(
                    {
                        "file": str(path),
                        "line": number,
                        "code": "not-object",
                        "message": "a row must be a JSON object, got %s"
                        % type(row).__name__,
                        "severity": "error",
                    }
                )
                continue
            records.append({"file": str(path), "line": number, "row": row})
    return records, problems


def canonical_json(value):
    """Stable text for hashing: sorted keys, no spaces, UTF-8 characters kept."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def spec_sha(row):
    """Seal of what a row has to prove: acceptance list, superiority tests and target.

    CI stores it in the row's evidence when the row is credited. If the acceptance list,
    a tolerance, a corpus, the superiority tests or the target change afterwards, the seal
    no longer matches and the credit is void until the row is verified again (plan 7.4:
    a weakened test reverts the row).
    """
    spec = {
        "acceptance": row.get("acceptance") or [],
        "superiority_tests": row.get("superiority_tests") or [],
        "target": row.get("target"),
    }
    return hashlib.sha256(canonical_json(spec).encode("utf-8")).hexdigest()


def tier_of(row):
    """The tier written in the row (the schema default is None)."""
    return row.get("tier_now") or "None"


def acceptance_ids(row):
    """The acceptance test ids of a row, in order, without duplicates and blanks."""
    seen = []
    for item in row.get("acceptance") or []:
        if isinstance(item, dict):
            test_id = item.get("test_id")
            if isinstance(test_id, str) and test_id and test_id not in seen:
                seen.append(test_id)
    return seen


def superiority_ids(row):
    """The superiority (Best tier) test ids of a row, without duplicates and blanks."""
    seen = []
    for test_id in row.get("superiority_tests") or []:
        if isinstance(test_id, str) and test_id and test_id not in seen:
            seen.append(test_id)
    return seen
