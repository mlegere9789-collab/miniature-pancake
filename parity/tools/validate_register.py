#!/usr/bin/env python3
"""Validate the Parity Register (plan section 4 and Appendix C).

    python3 parity/tools/validate_register.py [--register DIR] [--schema FILE]
        [--inventory FILE] [--strict] [--github] [--json]

Checks, over every parity/register/**/*.jsonl row:

  schema               row against parity/schema/row.schema.json (jsonschema when it is
                       installed, otherwise a built-in validator for the keywords the
                       schema uses; the built-in one refuses a schema keyword it does not
                       know instead of ignoring it)
  duplicate-id         ids are unique across all files
  tier-without-evidence
                       tier_now Strong or Best needs an evidence object (run_id, commit)
  verified-without-evidence, verified-below-target, no-acceptance
                       status verified needs evidence, a tier that meets the target, and at
                       least one acceptance test; a credited tier needs a test too
  best-without-superiority-test
                       tier_now Best needs at least one superiority test id
  alias-missing, alias-self, alias-cycle
                       alias_of must name an existing row, not the row itself, and not loop
  unknown-test         with --inventory FILE: acceptance test ids of rows that are in
                       progress, verified or credited must exist in the inventory
  bad-json, not-object a line that is not a JSON object

Warnings (errors with --strict): spec-changed (the seal in evidence.spec_sha no longer
matches the row's acceptance list, so the credit is void), path-mismatch / path-layout
(row not in parity/register/<program>/<domain>.jsonl), duplicate-statement, blank-line.

Exit status: 0 clean, 1 at least one error, 2 usage or input problem.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regio  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_REGISTER = os.path.join(REPO_ROOT, "parity", "register")
DEFAULT_SCHEMA = os.path.join(REPO_ROOT, "parity", "schema", "row.schema.json")

ANNOTATION_KEYWORDS = {
    "$schema",
    "$id",
    "$comment",
    "title",
    "description",
    "default",
    "examples",
}
HANDLED_KEYWORDS = {
    "type",
    "enum",
    "required",
    "properties",
    "additionalProperties",
    "items",
    "pattern",
    "minItems",
    "minLength",
}


class UnsupportedSchema(Exception):
    """The built-in validator met a schema keyword it does not implement."""


# --------------------------------------------------------------------------------------
# Built-in JSON Schema validator (draft-07 subset)
# --------------------------------------------------------------------------------------


def _type_ok(value, name):
    if name == "string":
        return isinstance(value, str)
    if name == "integer":
        if isinstance(value, bool):
            return False
        return isinstance(value, int) or (
            isinstance(value, float) and value.is_integer()
        )
    if name == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if name == "boolean":
        return isinstance(value, bool)
    if name == "array":
        return isinstance(value, list)
    if name == "object":
        return isinstance(value, dict)
    if name == "null":
        return value is None
    raise UnsupportedSchema("unknown type %r" % (name,))


def _json_equal(left, right):
    """JSON equality: true is not 1 and false is not 0."""
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(
            _json_equal(a, b) for a, b in zip(left, right)
        )
    if isinstance(left, dict) and isinstance(right, dict):
        return set(left) == set(right) and all(
            _json_equal(left[k], right[k]) for k in left
        )
    return left == right


def _short(value):
    text = json.dumps(value, ensure_ascii=False, sort_keys=True)
    return text if len(text) <= 60 else text[:57] + "..."


def mini_validate(instance, schema, path="$"):
    """Validate instance; return a sorted list of (path, message)."""
    errors = []
    for key in schema:
        if key not in HANDLED_KEYWORDS and key not in ANNOTATION_KEYWORDS:
            raise UnsupportedSchema(
                "schema keyword %r is not supported by the built-in validator; "
                "install jsonschema (pip install jsonschema)" % key
            )
    declared = schema.get("type")
    if declared is not None:
        names = declared if isinstance(declared, list) else [declared]
        if not any(_type_ok(instance, name) for name in names):
            errors.append(
                (path, "%s is not of type %s" % (_short(instance), ", ".join(names)))
            )
            return sorted(errors)
    if "enum" in schema and not any(_json_equal(instance, o) for o in schema["enum"]):
        errors.append(
            (path, "%s is not one of %s" % (_short(instance), _short(schema["enum"])))
        )
    if isinstance(instance, str):
        if "minLength" in schema and len(instance) < schema["minLength"]:
            errors.append(
                (
                    path,
                    "%s is too short (minLength %d)"
                    % (_short(instance), schema["minLength"]),
                )
            )
        if "pattern" in schema and re.search(schema["pattern"], instance) is None:
            errors.append(
                (path, "%s does not match %r" % (_short(instance), schema["pattern"]))
            )
    if isinstance(instance, list):
        if "minItems" in schema and len(instance) < schema["minItems"]:
            errors.append(
                (
                    path,
                    "%s is too short (minItems %d)"
                    % (_short(instance), schema["minItems"]),
                )
            )
        if "items" in schema:
            item_schema = schema["items"]
            if not isinstance(item_schema, dict):
                raise UnsupportedSchema("only a schema object is supported for 'items'")
            for index, item in enumerate(instance):
                errors.extend(
                    mini_validate(item, item_schema, "%s[%d]" % (path, index))
                )
    if isinstance(instance, dict):
        properties = schema.get("properties", {})
        for name in schema.get("required", []):
            if name not in instance:
                errors.append((path, "%r is a required property" % name))
        for name, value in instance.items():
            if name in properties:
                errors.extend(
                    mini_validate(value, properties[name], "%s.%s" % (path, name))
                )
            elif "additionalProperties" in schema:
                extra = schema["additionalProperties"]
                if extra is False:
                    errors.append(
                        (path, "additional property %r is not allowed" % name)
                    )
                elif isinstance(extra, dict):
                    errors.extend(mini_validate(value, extra, "%s.%s" % (path, name)))
    return sorted(errors)


def _jsonschema_path(error):
    text = "$"
    for part in error.absolute_path:
        text += "[%d]" % part if isinstance(part, int) else ".%s" % part
    return text


def make_validator(schema, prefer="auto"):
    """Return (function row -> sorted [(path, message)], engine name).

    prefer is "auto" (jsonschema when importable), "jsonschema" or "builtin".
    """
    if prefer in ("auto", "jsonschema"):
        try:
            import jsonschema  # type: ignore

            cls = jsonschema.Draft7Validator
            cls.check_schema(schema)
            validator = cls(schema)

            def run(row):
                found = [
                    (_jsonschema_path(e), e.message) for e in validator.iter_errors(row)
                ]
                return sorted(found)

            return run, "jsonschema"
        except ImportError:
            if prefer == "jsonschema":
                raise
    # The built-in validator checks the schema keywords on first use of each node; walk the
    # schema once now so an unsupported keyword is reported before any row is read.
    _check_supported(schema)
    return (lambda row: mini_validate(row, schema)), "builtin"


def _check_supported(schema):
    if isinstance(schema, dict):
        for key, value in schema.items():
            if key not in HANDLED_KEYWORDS and key not in ANNOTATION_KEYWORDS:
                raise UnsupportedSchema(
                    "schema keyword %r is not supported by the built-in validator; "
                    "install jsonschema (pip install jsonschema)" % key
                )
            if key == "properties":
                for sub in value.values():
                    _check_supported(sub)
            elif key in ("items", "additionalProperties"):
                _check_supported(value)


# --------------------------------------------------------------------------------------
# Register checks
# --------------------------------------------------------------------------------------


def _issue(severity, code, record, message, row_id=None):
    return {
        "severity": severity,
        "code": code,
        "file": record["file"] if record else None,
        "line": record["line"] if record else None,
        "id": row_id,
        "message": message,
    }


def _norm(text):
    return re.sub(r"[^a-z0-9]+", "-", str(text).lower()).strip("-")


def _norm_statement(text):
    return re.sub(r"[^a-z0-9]+", " ", str(text).lower()).strip()


def load_inventory(path):
    """Test ids known to exist: a text file (one id per line, # comments) or JSONL lines."""
    ids = set()
    with open(path, "r", encoding="utf-8") as handle:
        for text in handle:
            text = text.strip()
            if not text or text.startswith("#"):
                continue
            if text.startswith("{"):
                try:
                    obj = json.loads(text)
                except ValueError:
                    continue
                for key in ("test", "id", "test_id"):
                    if isinstance(obj.get(key), str):
                        ids.add(obj[key])
                        break
            else:
                ids.add(text)
    return ids


def _has_evidence(row):
    evidence = row.get("evidence")
    return (
        isinstance(evidence, dict)
        and isinstance(evidence.get("run_id"), str)
        and evidence["run_id"] != ""
        and isinstance(evidence.get("commit"), str)
        and evidence["commit"] != ""
    )


def check_records(records, validate, register_root=None, inventory=None):
    """Run every check; return a list of issues (dicts with severity, code, file, line...)."""
    issues = []
    first_by_id = {}

    for record in records:
        row = record["row"]
        row_id = row.get("id") if isinstance(row.get("id"), str) else None
        for where, message in validate(row):
            issues.append(
                _issue("error", "schema", record, "%s: %s" % (where, message), row_id)
            )
        if row_id is not None:
            if row_id in first_by_id:
                other = first_by_id[row_id]
                issues.append(
                    _issue(
                        "error",
                        "duplicate-id",
                        record,
                        "id already defined at %s:%d" % (other["file"], other["line"]),
                        row_id,
                    )
                )
            else:
                first_by_id[row_id] = record

    alias_target = {}
    seen_statements = {}
    layout = {}
    for record in records:
        row = record["row"]
        row_id = row.get("id") if isinstance(row.get("id"), str) else None
        tier = row.get("tier_now") or "None"
        status = row.get("status")
        target = row.get("target")
        if not isinstance(tier, str) or tier not in regio.TIER_RANK:
            continue  # the schema check already reported the bad value
        credited = tier in regio.CREDITED_TIERS
        has_ev = _has_evidence(row)
        tests = regio.acceptance_ids(row)

        if credited and not has_ev:
            issues.append(
                _issue(
                    "error",
                    "tier-without-evidence",
                    record,
                    "tier_now is %s but the row has no evidence (run_id and commit): only CI "
                    "may credit a row, an agent's own statement counts as nothing"
                    % tier,
                    row_id,
                )
            )
        if credited and not tests:
            issues.append(
                _issue(
                    "error",
                    "no-acceptance",
                    record,
                    "tier_now is %s but the row names no acceptance test" % tier,
                    row_id,
                )
            )
        if tier == "Best" and not regio.superiority_ids(row):
            issues.append(
                _issue(
                    "error",
                    "best-without-superiority-test",
                    record,
                    "tier_now is Best but the row has no superiority_tests",
                    row_id,
                )
            )
        if status == "verified":
            if not has_ev:
                issues.append(
                    _issue(
                        "error",
                        "verified-without-evidence",
                        record,
                        "status is verified but the row has no evidence (run_id and commit)",
                        row_id,
                    )
                )
            if (
                target in regio.TIER_RANK
                and regio.TIER_RANK[tier] < regio.TIER_RANK[target]
            ):
                issues.append(
                    _issue(
                        "error",
                        "verified-below-target",
                        record,
                        "status is verified but tier_now %s is below target %s"
                        % (tier, target),
                        row_id,
                    )
                )
            if not tests and not credited:
                issues.append(
                    _issue(
                        "error",
                        "no-acceptance",
                        record,
                        "status is verified but the row names no acceptance test",
                        row_id,
                    )
                )

        evidence = row.get("evidence")
        if isinstance(evidence, dict) and isinstance(evidence.get("spec_sha"), str):
            if evidence["spec_sha"] != regio.spec_sha(row):
                issues.append(
                    _issue(
                        "warning",
                        "spec-changed",
                        record,
                        "acceptance list, superiority tests or target changed after the row "
                        "was credited (evidence.spec_sha no longer matches): the credit is void "
                        "until CI verifies the row again",
                        row_id,
                    )
                )

        alias = row.get("alias_of")
        if isinstance(alias, str) and row_id is not None:
            alias_target[row_id] = alias
            if alias == row_id:
                issues.append(
                    _issue(
                        "error",
                        "alias-self",
                        record,
                        "alias_of names the row itself",
                        row_id,
                    )
                )
            elif alias not in first_by_id:
                issues.append(
                    _issue(
                        "error",
                        "alias-missing",
                        record,
                        "alias_of %s does not exist in the Register" % alias,
                        row_id,
                    )
                )

        if inventory is not None and (
            status in ("in_progress", "verified") or credited
        ):
            wanted = list(tests)
            if tier == "Best":
                wanted += regio.superiority_ids(row)
            for test_id in wanted:
                if test_id not in inventory:
                    issues.append(
                        _issue(
                            "error",
                            "unknown-test",
                            record,
                            "test id %s is not in the test inventory" % test_id,
                            row_id,
                        )
                    )

        statement = row.get("statement")
        programs = row.get("program")
        if (
            isinstance(statement, str)
            and isinstance(programs, list)
            and not isinstance(alias, str)
        ):
            key = (tuple(sorted(str(p) for p in programs)), _norm_statement(statement))
            if key in seen_statements and row_id is not None:
                issues.append(
                    _issue(
                        "warning",
                        "duplicate-statement",
                        record,
                        "same statement as %s (merge into an alias row?)"
                        % seen_statements[key],
                        row_id,
                    )
                )
            elif row_id is not None:
                seen_statements[key] = row_id

        if register_root is not None:
            slot = layout.setdefault(
                record["file"],
                {"record": record, "rows": 0, "bad_program": [], "bad_domain": []},
            )
            slot["rows"] += 1
            relative = os.path.relpath(record["file"], str(register_root)).replace(
                os.sep, "/"
            )
            parts = relative.split("/")
            if len(parts) == 2:
                program_list = (
                    [_norm(p) for p in programs] if isinstance(programs, list) else []
                )
                if program_list and _norm(parts[0]) not in program_list:
                    slot["bad_program"].append(row_id)
                domain = row.get("domain")
                stem = _norm(parts[1][: -len(".jsonl")])
                # a file name may carry a prefix or suffix around the domain (b_shader_nodes)
                if isinstance(domain, str) and _norm(domain) not in stem:
                    slot["bad_domain"].append(domain)

    # file layout: one warning per file, not one per row
    for name, slot in sorted(layout.items()):
        relative = os.path.relpath(name, str(register_root)).replace(os.sep, "/")
        parts = relative.split("/")
        first = slot["record"]
        if len(parts) != 2:
            issues.append(
                _issue(
                    "warning",
                    "path-layout",
                    first,
                    "expected parity/register/<program>/<domain>.jsonl, found %s"
                    % relative,
                )
            )
            continue
        if slot["bad_program"]:
            issues.append(
                _issue(
                    "warning",
                    "path-mismatch",
                    first,
                    "directory %s is not one of the programs of %d of %d rows (first: %s)"
                    % (
                        parts[0],
                        len(slot["bad_program"]),
                        slot["rows"],
                        slot["bad_program"][0],
                    ),
                )
            )
        if slot["bad_domain"]:
            issues.append(
                _issue(
                    "warning",
                    "path-mismatch",
                    first,
                    "file %s does not match the domain of %d of %d rows (domains: %s)"
                    % (
                        parts[1],
                        len(slot["bad_domain"]),
                        slot["rows"],
                        ", ".join(sorted(set(slot["bad_domain"]))[:5]),
                    ),
                )
            )

    # alias loops: follow each chain once
    reported = set()
    for start in sorted(alias_target):
        chain = []
        node = start
        while node in alias_target and node not in chain:
            chain.append(node)
            node = alias_target[node]
        if node in chain and node != alias_target.get(node):  # a loop, not a self alias
            loop = chain[chain.index(node) :]
            marker = frozenset(loop)
            if marker not in reported:
                reported.add(marker)
                record = first_by_id.get(loop[0])
                issues.append(
                    _issue(
                        "error",
                        "alias-cycle",
                        record,
                        "alias_of loops: %s" % " -> ".join(loop + [loop[0]]),
                        loop[0],
                    )
                )
    return issues


def _display_path(path):
    """A path relative to the current directory when it lies below it (for annotations)."""
    relative = os.path.relpath(path)
    return path if relative.startswith("..") else relative


def run(argv=None, out=None):
    parser = argparse.ArgumentParser(description="Validate the Parity Register.")
    parser.add_argument(
        "--register", help="register directory (default parity/register)"
    )
    parser.add_argument(
        "--schema", help="row schema (default parity/schema/row.schema.json)"
    )
    parser.add_argument(
        "--inventory", help="file of test ids that exist (one per line, or JSONL)"
    )
    parser.add_argument("--strict", action="store_true", help="warnings are errors")
    parser.add_argument(
        "--github", action="store_true", help="print GitHub Actions annotations"
    )
    parser.add_argument("--json", action="store_true", help="print one JSON report")
    parser.add_argument(
        "--engine", choices=("auto", "jsonschema", "builtin"), default="auto"
    )
    args = parser.parse_args(argv)
    out = out or sys.stdout

    explicit_register = args.register is not None
    register = _display_path(args.register or DEFAULT_REGISTER)
    schema_path = args.schema or DEFAULT_SCHEMA
    try:
        with open(schema_path, "r", encoding="utf-8") as handle:
            schema = json.load(handle)
        validate, engine = make_validator(schema, args.engine)
    except (OSError, ValueError, UnsupportedSchema, ImportError) as exc:
        print(
            "validate_register: cannot load the schema %s: %s" % (schema_path, exc),
            file=sys.stderr,
        )
        return 2

    inventory = None
    if args.inventory:
        try:
            inventory = load_inventory(args.inventory)
        except OSError as exc:
            print(
                "validate_register: cannot read the inventory: %s" % exc,
                file=sys.stderr,
            )
            return 2

    if not os.path.isdir(register):
        if explicit_register:
            print(
                "validate_register: no such register directory: %s" % register,
                file=sys.stderr,
            )
            return 2
        records, problems = [], []
    else:
        records, problems = regio.read_register(register)

    issues = [
        {
            "severity": p["severity"],
            "code": p["code"],
            "file": p["file"],
            "line": p["line"],
            "id": None,
            "message": p["message"],
        }
        for p in problems
    ]
    issues += check_records(
        records, validate, register if os.path.isdir(register) else None, inventory
    )
    if args.strict:
        for issue in issues:
            if issue["severity"] == "warning":
                issue["severity"] = "error"

    errors = [i for i in issues if i["severity"] == "error"]
    warnings = [i for i in issues if i["severity"] == "warning"]
    files = len(regio.register_files(register)) if os.path.isdir(register) else 0

    if args.json:
        json.dump(
            {
                "rows": len(records),
                "files": files,
                "engine": engine,
                "errors": errors,
                "warnings": warnings,
            },
            out,
            indent=2,
            sort_keys=True,
        )
        out.write("\n")
    else:
        for issue in errors + warnings:
            where = (
                "%s:%s" % (issue["file"], issue["line"])
                if issue["file"]
                else "register"
            )
            if args.github:
                level = "error" if issue["severity"] == "error" else "warning"
                out.write(
                    "::%s file=%s,line=%s,title=%s::%s %s\n"
                    % (
                        level,
                        issue["file"],
                        issue["line"],
                        issue["code"],
                        issue["id"] or "",
                        issue["message"],
                    )
                )
            else:
                out.write(
                    "%s: %s [%s] %s%s\n"
                    % (
                        where,
                        issue["severity"].upper(),
                        issue["code"],
                        (issue["id"] + ": ") if issue["id"] else "",
                        issue["message"],
                    )
                )
        out.write(
            "validate_register: %d rows in %d files, %d errors, %d warnings (engine: %s)\n"
            % (len(records), files, len(errors), len(warnings), engine)
        )
    return 1 if errors else 0


def main():
    sys.exit(run())


if __name__ == "__main__":
    main()
