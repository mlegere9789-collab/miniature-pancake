#!/usr/bin/env python3
"""Wave 1 work queue generator: Register rows -> claimable task files (plan 6.3, 7.2, 7.3).

Reads  parity/register/<program>/*.jsonl  and writes, under ledger/tasks/ :

  <task-id>.json   one claimable task (the format claims.py and watchdog.py read: id, status,
                   retries, rows[], points, deps[], branch, handoff note, plus the extra
                   fields below)
  index.json       counts per factory and per wave, and one line per task
  REPORT.md        the human summary (tasks, rows per factory, how many rows have hints)

HOW ROWS ARE GROUPED

  * One task holds rows of ONE factory. Rows are grouped by factory x domain (the domain
    compared case-insensitively, across programs) and each group is cut into tasks of
    MIN_ROWS to MAX_ROWS rows (20 to 40 by default), in balanced chunks.
  * A row with `alias_of` always travels with its canonical row (the root of the alias
    chain), listed right after it; the pair is never split, even across domains.
  * Rows that share an acceptance corpus are ordered next to each other and a cut is moved
    to a corpus boundary whenever one lies in the allowed size window.
  * Domains with fewer than MIN_ROWS rows are pooled per factory into `misc` tasks (cut at
    domain boundaries when possible), so that no task is a handful of rows.

HINTS ARE NOT EVIDENCE

  The hint lists (existing Dino 8 commands and kernel symbols) come from the fuzzy name match
  of parity/tools/catalog/map_rows.py. They only say where to look first. A row becomes
  verified only through a named test with CI evidence; this tool never touches a row, never
  sets a tier, a status or evidence.

DETERMINISTIC AND IDEMPOTENT

  The output depends only on the Register, the catalog files, ledger/waves.json and the task
  files already on disk. Running it twice writes nothing the second time. A task file whose
  status is no longer `open` (claimed, in progress, merged...) is frozen: it is kept as it is,
  its rows are left out of the regeneration, and its id is never reused. Open task files that
  the new run no longer produces are removed (--no-prune keeps them).

USAGE

  gen_tasks.py [--register parity/register] [--out ledger/tasks] [--waves ledger/waves.json]
               [--catalog parity/tools/catalog] [--min-rows 20] [--max-rows 40]
               [--min-score 0.6] [--check] [--validate] [--no-prune] [--no-hints]

  --check     write nothing; exit 1 when the files on disk differ from what would be written
  --validate  write nothing; run the consistency checks on the files on disk (below)

  Exit 0 success, 1 a check or validation failed, 2 usage or input problem.

VALIDATION (the ledger's own checks, applied to the queue)

  every Register row is in exactly one task; no task mixes factories; every alias is in the
  same task as its canonical row; task ids pass claims.valid_name; every task file is accepted
  by claims._check_task_file and read by watchdog.read_task_files; statuses are known; points
  are one of 1, 2, 3, 5, 8; the index agrees with the files.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEDGER_DIR = os.path.dirname(HERE)
REPO_ROOT = os.path.dirname(LEDGER_DIR)
PARITY_TOOLS = os.path.join(REPO_ROOT, "parity", "tools")
for _path in (HERE, PARITY_TOOLS):
    if _path not in sys.path:
        sys.path.insert(0, _path)

import claims  # noqa: E402
import ledgerlib  # noqa: E402
import regio  # noqa: E402

DEFAULT_REGISTER = os.path.join(REPO_ROOT, "parity", "register")
DEFAULT_OUT = os.path.join(LEDGER_DIR, "tasks")
DEFAULT_WAVES = os.path.join(LEDGER_DIR, "waves.json")
DEFAULT_CATALOG = os.path.join(REPO_ROOT, "parity", "tools", "catalog")

MIN_ROWS = 20
MAX_ROWS = 40
MIN_SCORE = 0.6
TOP_PER_ROW = 3  # hints kept per kind for each row
TOP_PER_TASK = 20  # hints kept per kind for a task
POINT_STEPS = (1, 2, 3, 5, 8)
ROWS_PER_POINT_UNIT = 30.0
KNOWN_STATUS = (
    "open",
    "claimed",
    "in_progress",
    "verify",
    "review",
    "merged",
    "counted",
    "cancelled",
)
HINT_LABEL = (
    "HINTS, NOT EVIDENCE: fuzzy name match against what Dino 8 ships "
    "(parity/tools/catalog). A row is verified only by its named tests with CI evidence."
)
NO_WAVE = "unassigned"


# --------------------------------------------------------------------------------------
# reading the Register
# --------------------------------------------------------------------------------------


def load_rows(register_dir):
    """All Register rows in file order: [(order, row, relative_file)]. Raises on a bad line."""
    records, problems = regio.read_register(register_dir)
    bad = [p for p in problems if p.get("severity") == "error"]
    if bad:
        raise ValueError(
            "%d unreadable Register line(s), first: %s:%s %s"
            % (len(bad), bad[0]["file"], bad[0]["line"], bad[0]["message"])
        )
    out = []
    seen = set()
    for number, rec in enumerate(records):
        row = rec["row"]
        rid = row.get("id")
        if not isinstance(rid, str):
            raise ValueError("%s:%s: row without an id" % (rec["file"], rec["line"]))
        if rid in seen:
            raise ValueError("duplicate row id %s" % rid)
        seen.add(rid)
        rel = os.path.relpath(rec["file"], register_dir).replace(os.sep, "/")
        out.append((number, row, rel))
    return out


def register_digest(register_dir):
    """SHA-256 over the relative names and bytes of every Register file, in sorted order."""
    digest = hashlib.sha256()
    for path in regio.register_files(register_dir):
        rel = os.path.relpath(str(path), register_dir).replace(os.sep, "/")
        digest.update(rel.encode("utf-8") + b"\0")
        with open(str(path), "rb") as handle:
            digest.update(handle.read())
        digest.update(b"\0")
    return digest.hexdigest()


def load_waves(path):
    if not path or not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data.get("factory", {})


def wave_of(row, factory_waves):
    """Same rule as percent.py: the row's own wave, else the wave of its factory."""
    wave = row.get("wave")
    if isinstance(wave, int) and not isinstance(wave, bool):
        return wave
    return factory_waves.get(row.get("factory"))


def first_corpus(row):
    for item in row.get("acceptance") or []:
        corpus = item.get("corpus") if isinstance(item, dict) else None
        if isinstance(corpus, str) and corpus.strip():
            return corpus.strip()
    return None


def slug(text, limit=28):
    text = re.sub(r"[^a-z0-9]+", "-", str(text).lower()).strip("-")
    return (text[:limit].strip("-")) or "x"


# --------------------------------------------------------------------------------------
# hints (names only; map_rows.py scoring)
# --------------------------------------------------------------------------------------


def load_map_rows(catalog_dir):
    path = os.path.join(catalog_dir, "map_rows.py")
    spec = importlib.util.spec_from_file_location("d8_map_rows", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _jsonl(path):
    with open(path, "r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def build_hint_index(catalog_dir):
    """(map_rows module, Index) built exactly as map_rows.main() builds it."""
    mod = load_map_rows(catalog_dir)
    idx = mod.Index()
    for c in _jsonl(os.path.join(catalog_dir, "catalog.jsonl")):
        extra = {
            "status": c.get("status"),
            "impl_file": c.get("impl_file"),
            "impl_line": c.get("impl_line"),
        }
        idx.add("command", c["name"], c["name"], extra)
        for alias in c.get("aliases", []):
            if len(alias) >= 4:
                idx.add("command", c["name"], alias, extra)
    for k in _jsonl(os.path.join(catalog_dir, "kernel_api.jsonl")):
        if k["kind"] in ("constructor", "destructor", "operator") or k.get("internal"):
            continue
        parts = k["symbol"].split("::")
        text = (
            " ".join(parts[-2:])
            if len(parts) >= 2 and k["kind"] in ("method", "static_method")
            else parts[-1]
        )
        idx.add(
            "kernel",
            k["symbol"],
            text,
            {"header": k["header"], "line": k["line"], "sym_kind": k["kind"]},
        )
    return mod, idx


def row_hints(mod, idx, row, min_score=MIN_SCORE, top=TOP_PER_ROW):
    """{"commands": [...], "kernel": [...]} for one row, best first, deterministic."""
    best = {}
    for query, weight, _src in mod.queries_for(row):
        for score, kind, name, extra in idx.match(query, weight, min_score):
            key = (kind, name)
            if key not in best or score > best[key][0]:
                best[key] = (score, extra)
    commands, kernel = [], []
    for (kind, name), (score, extra) in best.items():
        if kind == "command":
            commands.append(
                {
                    "name": name,
                    "score": round(score, 3),
                    "status": extra.get("status"),
                }
            )
        else:
            kernel.append(
                {
                    "symbol": name,
                    "score": round(score, 3),
                    "header": "%s:%s" % (extra.get("header"), extra.get("line")),
                }
            )
    commands.sort(key=lambda d: (-d["score"], d["name"]))
    kernel.sort(key=lambda d: (-d["score"], d["symbol"]))
    return {"commands": commands[:top], "kernel": kernel[:top]}


def has_existing(hint):
    """A hint that points at something that exists: a registered command or a kernel symbol."""
    if hint["kernel"]:
        return True
    return any(c.get("status") in ("implemented", "partial") for c in hint["commands"])


# --------------------------------------------------------------------------------------
# grouping
# --------------------------------------------------------------------------------------


def alias_roots(rows_by_id):
    """id -> id of the root of its alias chain (itself when not an alias, or unresolvable)."""
    roots = {}
    for rid, (_, row, _) in rows_by_id.items():
        seen = [rid]
        cur = rid
        while True:
            target = rows_by_id[cur][1].get("alias_of")
            if not target or target not in rows_by_id or target in seen:
                break
            # an alias never leaves its factory (a task has one factory)
            if rows_by_id[target][1].get("factory") != row.get("factory"):
                break
            seen.append(target)
            cur = target
        roots[rid] = cur
    return roots


def build_units(pool, roots):
    """Alias components: [{"root", "rows": [ids, canonical first], "order", ...}]."""
    members = collections.defaultdict(list)
    for rid in pool:
        root = roots[rid]
        members[root if root in pool else rid].append(rid)
    units = []
    for root, ids in members.items():
        ids.sort(key=lambda i: (i != root, pool[i][0]))
        order, row, rel = pool[root]
        units.append(
            {
                "root": root,
                "ids": ids,
                "order": order,
                "factory": row.get("factory"),
                "domain": str(row.get("domain", "")),
                "corpus": first_corpus(row),
                "program": ",".join(row.get("program") or []),
            }
        )
    return units


def unit_sort_key(unit):
    """Corpus first (rows of one corpus end up adjacent), then program, then Register order."""
    corpus = unit["corpus"]
    return (corpus is None, corpus or "", unit["program"], unit["order"])


def chunk_units(units, min_rows, max_rows, label):
    """Cut an ordered list of units into balanced chunks of min_rows..max_rows rows.

    `label(unit)` names the cluster of a unit; a cut is moved to a cluster boundary when one
    lies in the allowed size window (nearest to the balanced position), else to the nearest
    unit boundary in the window, else to the nearest unit boundary.
    """
    sizes = [len(u["ids"]) for u in units]
    total = sum(sizes)
    if total == 0:
        return []
    count = max(1, math.ceil(total / float(max_rows)))
    prefix = [0]
    for size in sizes:
        prefix.append(prefix[-1] + size)
    chunks = []
    start = 0  # unit index
    for left in range(count, 1, -1):
        remaining = total - prefix[start]
        ideal = prefix[start] + remaining / float(left)
        low = prefix[start] + min_rows
        high = prefix[start] + max_rows
        # the rest has to fit in the chunks that remain
        high_rest = total - (left - 1) * min_rows
        low_rest = total - (left - 1) * max_rows
        low = max(low, low_rest)
        high = min(high, high_rest)
        candidates = [
            j for j in range(start + 1, len(units)) if low <= prefix[j] <= high
        ]
        if candidates:
            on_edge = [
                j for j in candidates if label(units[j - 1]) != label(units[j])
            ]
            pool = on_edge or candidates
        else:
            pool = list(range(start + 1, len(units)))
        if not pool:
            break
        cut = min(pool, key=lambda j: (abs(prefix[j] - ideal), j))
        chunks.append(units[start:cut])
        start = cut
    chunks.append(units[start:])
    return [c for c in chunks if c]


def plan_tasks(pool, roots, min_rows, max_rows, reserved_ids=()):
    """The planned tasks as dicts: {"id","factory","domains","units"} in a stable order."""
    units = build_units(pool, roots)
    by_factory = collections.defaultdict(lambda: collections.defaultdict(list))
    for unit in units:
        by_factory[unit["factory"]][unit["domain"].lower()].append(unit)
    taken = set(reserved_ids)
    counters = collections.Counter()
    planned = []

    def new_id(factory, name):
        base = "T-%s-%s" % (factory, name)
        while True:
            counters[base] += 1
            candidate = "%s-%02d" % (base, counters[base])
            if candidate not in taken:
                taken.add(candidate)
                return candidate

    def rows_of(units):
        return sum(len(u["ids"]) for u in units)

    for factory in sorted(by_factory, key=lambda f: (f is None, str(f))):
        domains = by_factory[factory]
        small = []
        mine = []  # [name, units] in order
        for key in sorted(domains):
            group = sorted(domains[key], key=unit_sort_key)
            if rows_of(group) < min_rows:
                small.extend((key, u) for u in group)
                continue
            for chunk in chunk_units(
                group, min_rows, max_rows, lambda u: u["corpus"] or ""
            ):
                mine.append([slug(key), chunk])
        if small:
            # pooled domains: stable order by domain, then the usual unit order
            small.sort(key=lambda pair: (pair[0], unit_sort_key(pair[1])))
            pooled = [u for _, u in small]
            chunks = chunk_units(
                pooled, min_rows, max_rows, lambda u: u["domain"].lower()
            )
            for chunk in chunks:
                if rows_of(chunk) < min_rows and mine:
                    # a stub of fewer than min_rows rows: fold it into the smallest task of
                    # the factory that can take it, or share rows with the smallest one
                    fit = [m for m in mine if rows_of(m[1]) + rows_of(chunk) <= max_rows]
                    if fit:
                        target = min(fit, key=lambda m: (rows_of(m[1]), m[0]))
                        target[1] = sorted(target[1] + chunk, key=unit_sort_key)
                        continue
                    target = min(mine, key=lambda m: (rows_of(m[1]), m[0]))
                    merged = sorted(target[1] + chunk, key=unit_sort_key)
                    at = mine.index(target)
                    mine[at : at + 1] = [
                        [target[0], part]
                        for part in chunk_units(
                            merged, min_rows, max_rows, lambda u: u["corpus"] or ""
                        )
                    ]
                    continue
                mine.append(["misc", chunk])
        for name, units in mine:
            planned.append(
                {"id": new_id(factory, name), "factory": factory, "units": units}
            )
    return planned


# --------------------------------------------------------------------------------------
# building the task files
# --------------------------------------------------------------------------------------


def points_for(weighted_rows):
    need = weighted_rows / ROWS_PER_POINT_UNIT
    for step in POINT_STEPS:
        if need <= step:
            return step
    return POINT_STEPS[-1]


def build_task(plan, pool, roots, hints, factory_waves, hint_source):
    """The task file content (a dict) of one planned task."""
    ids = []
    for unit in plan["units"]:
        ids.extend(unit["ids"])
    rows = [pool[i][1] for i in ids]
    files = sorted({pool[i][2] for i in ids})
    domains = sorted({str(r.get("domain", "")) for r in rows})
    programs = sorted({p for r in rows for p in (r.get("program") or [])})
    aliases = [i for i in ids if pool[i][1].get("alias_of")]
    countable = [i for i in ids if not pool[i][1].get("alias_of")]
    waves = [w for w in (wave_of(r, factory_waves) for r in rows) if w is not None]
    wave = max(waves) if waves else None
    corpora = sorted({c for c in (first_corpus(r) for r in rows) if c})

    row_hint = {}
    without = []
    existing_rows = 0
    cmd_agg, ker_agg = {}, {}
    for rid in ids:
        h = hints.get(rid) if hints is not None else None
        if h and (h["commands"] or h["kernel"]):
            row_hint[rid] = {
                "commands": [c["name"] for c in h["commands"]],
                "kernel": [k["symbol"] for k in h["kernel"]],
            }
            if has_existing(h):
                existing_rows += 1
            for c in h["commands"]:
                cur = cmd_agg.setdefault(
                    c["name"], {"name": c["name"], "status": c["status"], "score": 0, "rows": 0}
                )
                cur["rows"] += 1
                cur["score"] = max(cur["score"], c["score"])
            for k in h["kernel"]:
                cur = ker_agg.setdefault(
                    k["symbol"],
                    {"symbol": k["symbol"], "header": k["header"], "score": 0, "rows": 0},
                )
                cur["rows"] += 1
                cur["score"] = max(cur["score"], k["score"])
        else:
            without.append(rid)

    def top(agg, name_key):
        items = sorted(agg.values(), key=lambda d: (-d["score"], -d["rows"], d[name_key]))
        return items[:TOP_PER_TASK]

    # provisional difficulty: rows without any existing counterpart cost more, Best costs more
    weighted = 0.0
    for rid in countable:
        row = pool[rid][1]
        h = hints.get(rid) if hints is not None else None
        weight = 1.0 if (h and has_existing(h)) else 1.5
        if row.get("target") == "Best":
            weight += 0.5
        weighted += weight

    name = plan["id"]
    misc = "-misc-" in name
    title_domains = domains if len(domains) <= 4 else domains[:4] + ["..."]
    task = {
        "id": name,
        "title": "%s: %s (%d rows)"
        % (plan["factory"], ", ".join(title_domains), len(countable)),
        "factory": plan["factory"],
        "domains": domains,
        "pooled_domains": misc,
        "programs": programs,
        "wave": wave,
        "status": "open",
        "branch": "task/%s" % name,
        "retries": 0,
        "deps": [],
        "points": points_for(weighted),
        "points_basis": "provisional: rows weighted 1.0 with an existing counterpart, 1.5 without, +0.5 for a Best target, per 30 weighted rows; the factory lead sets the real value",
        "handoff": "",
        "rows": ids,
        "alias_rows": aliases,
        "counts": {
            "rows": len(ids),
            "countable": len(countable),
            "aliases": len(aliases),
            "best_target": sum(1 for i in countable if pool[i][1].get("target") == "Best"),
            "with_hint": len(row_hint),
            "without_hint": len(without),
            "with_existing_hint": existing_rows,
        },
        "register_files": files,
        "corpora": corpora[:12],
        "hints": {
            "label": HINT_LABEL,
            "source": hint_source,
            "commands": top(cmd_agg, "name"),
            "kernel": top(ker_agg, "symbol"),
        },
        "row_hints": row_hint,
        "rows_without_hint": without,
    }
    return task


# --------------------------------------------------------------------------------------
# existing files
# --------------------------------------------------------------------------------------


def read_existing(out_dir):
    """{task id: (filename, data)} of the task files on disk (index.json excluded)."""
    found = {}
    if os.path.isdir(out_dir):
        for fname in sorted(os.listdir(out_dir)):
            if not fname.endswith(".json") or fname == "index.json":
                continue
            try:
                with open(os.path.join(out_dir, fname), "r", encoding="utf-8") as handle:
                    data = json.load(handle)
            except (OSError, ValueError):
                continue
            if isinstance(data, dict):
                found[data.get("id") or fname[:-5]] = (fname, data)
    return found


# --------------------------------------------------------------------------------------
# index and report
# --------------------------------------------------------------------------------------


def _blank():
    return {
        "tasks": 0,
        "rows": 0,
        "countable": 0,
        "aliases": 0,
        "with_hint": 0,
        "without_hint": 0,
        "with_existing_hint": 0,
        "points": 0,
    }


def _add(bucket, task):
    c = task.get("counts") or {}
    bucket["tasks"] += 1
    bucket["rows"] += c.get("rows", len(task.get("rows", [])))
    bucket["countable"] += c.get("countable", 0)
    bucket["aliases"] += c.get("aliases", 0)
    bucket["with_hint"] += c.get("with_hint", 0)
    bucket["without_hint"] += c.get("without_hint", 0)
    bucket["with_existing_hint"] += c.get("with_existing_hint", 0)
    bucket["points"] += task.get("points") or 0


def build_index(tasks, digest, settings):
    by_factory = collections.defaultdict(_blank)
    by_wave = collections.defaultdict(_blank)
    by_status = collections.Counter()
    total = _blank()
    sizes = []
    for task in tasks:
        _add(by_factory[task["factory"]], task)
        wave = task.get("wave")
        _add(by_wave[str(wave) if wave is not None else NO_WAVE], task)
        _add(total, task)
        by_status[task.get("status", "open")] += 1
        sizes.append((task.get("counts") or {}).get("rows", len(task["rows"])))
    wave_factory = collections.defaultdict(lambda: collections.defaultdict(_blank))
    for task in tasks:
        wave = task.get("wave")
        _add(
            wave_factory[str(wave) if wave is not None else NO_WAVE][task["factory"]],
            task,
        )
    return {
        "version": 1,
        "note": "Generated by ledger/tools/gen_tasks.py; do not edit. Hints are not evidence.",
        "register_sha256": digest,
        "settings": settings,
        "total": total,
        "task_size": {
            "min": min(sizes) if sizes else 0,
            "max": max(sizes) if sizes else 0,
            "outside_window": sum(
                1
                for s in sizes
                if s < settings["min_rows"] or s > settings["max_rows"]
            ),
        },
        "by_status": dict(sorted(by_status.items())),
        "by_factory": {k: v for k, v in sorted(by_factory.items())},
        "by_wave": {k: v for k, v in sorted(by_wave.items())},
        "by_wave_factory": {
            w: {f: v for f, v in sorted(fs.items())}
            for w, fs in sorted(wave_factory.items())
        },
        "tasks": [
            {
                "id": t["id"],
                "factory": t["factory"],
                "wave": t.get("wave"),
                "points": t.get("points"),
                "rows": (t.get("counts") or {}).get("rows", len(t["rows"])),
                "status": t.get("status", "open"),
            }
            for t in tasks
        ],
    }


def _pct(part, whole):
    return "%.1f%%" % (100.0 * part / whole) if whole else "n/a"


def build_report(index, tasks, hint_summary):
    total = index["total"]
    lines = []
    w = lines.append
    w("# Wave 1 work queue report")
    w("")
    w("Generated by `ledger/tools/gen_tasks.py` from `parity/register/` (Register sha256 `%s`)." % index["register_sha256"][:16])
    w("Do not edit by hand. Hints are fuzzy name matches against what Dino 8 ships; **hints are not evidence**,")
    w("and no row is verified by this queue.")
    w("")
    w("## Totals")
    w("")
    w("| Item | Count |")
    w("|---|---:|")
    w("| Tasks | %d |" % total["tasks"])
    w("| Rows in tasks | %d |" % total["rows"])
    w("| of which countable (not aliases) | %d |" % total["countable"])
    w("| of which aliases (travel with their canonical row) | %d |" % total["aliases"])
    w("| Provisional points | %d |" % total["points"])
    ts = index["task_size"]
    w("| Task size (rows), min / max | %d / %d |" % (ts["min"], ts["max"]))
    w("| Tasks outside the %d to %d window | %d |" % (index["settings"]["min_rows"], index["settings"]["max_rows"], ts["outside_window"]))
    w("")
    w("## Hint coverage")
    w("")
    w("Rows are all rows in tasks (aliases included). A row *has a hint* when the name match (score at least %s) finds a command or a kernel symbol; *existing* means a registered (implemented or partial) command or a public kernel symbol." % index["settings"]["min_score"])
    w("")
    w("| Measure | Rows | Share |")
    w("|---|---:|---:|")
    w("| At least one hint | %d | %s |" % (total["with_hint"], _pct(total["with_hint"], total["rows"])))
    w("| No hint at all (likely to need new features) | %d | %s |" % (total["without_hint"], _pct(total["without_hint"], total["rows"])))
    w("| Hint to something that exists (registered command or kernel symbol) | %d | %s |" % (total["with_existing_hint"], _pct(total["with_existing_hint"], total["rows"])))
    w("| Hints only to planned catalog placeholders | %d | %s |" % (total["with_hint"] - total["with_existing_hint"], _pct(total["with_hint"] - total["with_existing_hint"], total["rows"])))
    if hint_summary:
        w("| Exact-name hint (score 1.0) | %d | %s |" % (hint_summary["exact"], _pct(hint_summary["exact"], total["rows"])))
    w("")
    w("## Rows per factory")
    w("")
    w("| Factory | Tasks | Rows | Aliases | With hint | No hint | No hint share | Points |")
    w("|---|---:|---:|---:|---:|---:|---:|---:|")
    for factory, b in index["by_factory"].items():
        w("| %s | %d | %d | %d | %d | %d | %s | %d |" % (factory, b["tasks"], b["rows"], b["aliases"], b["with_hint"], b["without_hint"], _pct(b["without_hint"], b["rows"]), b["points"]))
    w("")
    w("## Per wave (ledger/waves.json, a row's own wave wins)")
    w("")
    w("| Wave | Tasks | Rows | With hint | No hint | Points |")
    w("|---|---:|---:|---:|---:|---:|")
    for wave, b in index["by_wave"].items():
        w("| %s | %d | %d | %d | %d | %d |" % (wave, b["tasks"], b["rows"], b["with_hint"], b["without_hint"], b["points"]))
    w("")
    w("## Task status")
    w("")
    for status, count in index["by_status"].items():
        w("* %s: %d" % (status, count))
    w("")
    w("## Largest groups without hints (candidates for new features)")
    w("")
    ranked = sorted(
        tasks,
        key=lambda t: (-(t.get("counts") or {}).get("without_hint", 0), t["id"]),
    )[:15]
    w("| Task | Factory | Rows | No hint |")
    w("|---|---|---:|---:|")
    for t in ranked:
        c = t.get("counts") or {}
        w("| %s | %s | %d | %d |" % (t["id"], t["factory"], c.get("rows", 0), c.get("without_hint", 0)))
    w("")
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------------------
# generation
# --------------------------------------------------------------------------------------


def generate(
    register_dir=DEFAULT_REGISTER,
    out_dir=DEFAULT_OUT,
    waves_path=DEFAULT_WAVES,
    catalog_dir=DEFAULT_CATALOG,
    min_rows=MIN_ROWS,
    max_rows=MAX_ROWS,
    min_score=MIN_SCORE,
    hints_on=True,
):
    """Compute the output: {"files": {relative name: text}, "tasks": [...], "index": {...}}."""
    loaded = load_rows(register_dir)
    pool_all = {row["id"]: (order, row, rel) for order, row, rel in loaded}
    roots = alias_roots(pool_all)
    factory_waves = load_waves(waves_path)

    existing = read_existing(out_dir)
    frozen = {
        tid: data for tid, (_, data) in existing.items() if data.get("status", "open") != "open"
    }
    frozen_rows = {r for data in frozen.values() for r in data.get("rows", [])}
    pool = {rid: v for rid, v in pool_all.items() if rid not in frozen_rows}

    hints = None
    hint_summary = None
    hint_source = None
    if hints_on:
        mod, idx = build_hint_index(catalog_dir)
        hints = {rid: row_hints(mod, idx, v[1], min_score) for rid, v in pool.items()}
        hint_source = "parity/tools/catalog/map_rows.py, min score %s" % min_score

    plans = plan_tasks(pool, roots, min_rows, max_rows, reserved_ids=frozen.keys())
    new_tasks = []
    for plan in plans:
        new_tasks.append(build_task(plan, pool, roots, hints, factory_waves, hint_source))
    all_tasks = new_tasks + [frozen[t] for t in sorted(frozen)]

    def order_key(task):
        wave = task.get("wave")
        return (wave is None, wave if wave is not None else 0, task["factory"], task["id"])

    all_tasks.sort(key=order_key)
    settings = {"min_rows": min_rows, "max_rows": max_rows, "min_score": min_score, "hints": bool(hints_on)}
    index = build_index(all_tasks, register_digest(register_dir), settings)
    if hints is not None:
        exact = 0
        for rid in pool:
            h = hints[rid]
            if any(c["score"] >= 0.999 for c in h["commands"]) or any(
                k["score"] >= 0.999 for k in h["kernel"]
            ):
                exact += 1
        hint_summary = {"exact": exact}
    files = {}
    for task in new_tasks:
        files[task["id"] + ".json"] = ledgerlib.dump_json(task)
    files["index.json"] = ledgerlib.dump_json(index)
    files["REPORT.md"] = build_report(index, all_tasks, hint_summary)
    return {"files": files, "tasks": all_tasks, "index": index, "frozen": sorted(frozen), "existing": existing}


def write_output(result, out_dir, prune=True):
    """Write the files that differ; remove stale open task files. Returns a change summary."""
    os.makedirs(out_dir, exist_ok=True)
    written, unchanged, removed = [], 0, []
    for name, text in sorted(result["files"].items()):
        path = os.path.join(out_dir, name)
        old = None
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as handle:
                old = handle.read()
        if old == text:
            unchanged += 1
            continue
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        written.append(name)
    if prune:
        keep = set(result["files"]) | {
            fname for tid, (fname, data) in result["existing"].items() if tid in result["frozen"]
        }
        for tid, (fname, data) in sorted(result["existing"].items()):
            if fname not in keep and data.get("status", "open") == "open":
                os.remove(os.path.join(out_dir, fname))
                removed.append(fname)
    return {"written": written, "unchanged": unchanged, "removed": removed}


def check_output(result, out_dir, prune=True):
    """Differences between the would-be output and the disk (empty list: up to date)."""
    problems = []
    for name, text in sorted(result["files"].items()):
        path = os.path.join(out_dir, name)
        if not os.path.isfile(path):
            problems.append("missing: %s" % name)
            continue
        with open(path, "r", encoding="utf-8") as handle:
            if handle.read() != text:
                problems.append("differs: %s" % name)
    if prune:
        keep = set(result["files"]) | {
            fname for tid, (fname, data) in result["existing"].items() if tid in result["frozen"]
        }
        for tid, (fname, data) in sorted(result["existing"].items()):
            if fname not in keep and data.get("status", "open") == "open":
                problems.append("stale: %s" % fname)
    return problems


# --------------------------------------------------------------------------------------
# validation of the queue on disk
# --------------------------------------------------------------------------------------


def validate_queue(out_dir, register_dir):
    """Consistency problems of the task files against the Register (empty list: clean)."""
    problems = []
    loaded = load_rows(register_dir)
    rows = {row["id"]: row for _, row, _ in loaded}
    roots = alias_roots({row["id"]: (o, row, r) for o, row, r in loaded})
    tasks = read_task_files_strict(out_dir, problems)
    owner = {}
    for tid, task in sorted(tasks.items()):
        if not claims.valid_name(tid):
            problems.append("%s: not a valid claim name" % tid)
        try:
            claims._check_task_file(out_dir, tid)
        except claims.ClaimError as exc:
            if task.get("status") == "open":
                problems.append("%s: claims.py refuses it: %s" % (tid, exc))
        if task.get("status") not in KNOWN_STATUS:
            problems.append("%s: unknown status %r" % (tid, task.get("status")))
        if task.get("points") not in POINT_STEPS:
            problems.append("%s: points %r not in %s" % (tid, task.get("points"), POINT_STEPS))
        if not isinstance(task.get("retries"), int):
            problems.append("%s: retries is not an integer" % tid)
        if not isinstance(task.get("deps"), list):
            problems.append("%s: deps is not a list" % tid)
        if task.get("branch") != "task/%s" % tid:
            problems.append("%s: branch %r is not task/<id>" % (tid, task.get("branch")))
        ids = task.get("rows") or []
        if not ids:
            problems.append("%s: no rows" % tid)
        factories = {rows[i].get("factory") for i in ids if i in rows}
        if len(factories) > 1:
            problems.append("%s: mixes factories %s" % (tid, sorted(map(str, factories))))
        if factories and task.get("factory") not in factories:
            problems.append("%s: factory field %r does not match its rows" % (tid, task.get("factory")))
        for rid in ids:
            if rid not in rows:
                problems.append("%s: row %s is not in the Register" % (tid, rid))
            elif rid in owner:
                problems.append("%s: row %s is also in %s" % (tid, rid, owner[rid]))
            else:
                owner[rid] = tid
        inside = set(ids)
        for rid in ids:
            root = roots.get(rid)
            if rid in rows and root and root != rid and root not in inside:
                problems.append("%s: alias %s is apart from its canonical row %s" % (tid, rid, root))
        counts = task.get("counts") or {}
        if counts.get("rows") != len(ids):
            problems.append("%s: counts.rows disagrees with rows" % tid)
        for key in ("verified", "evidence", "tier_now"):
            if key in task:
                problems.append("%s: a task file must not carry %s" % (tid, key))
    missing = sorted(set(rows) - set(owner))
    if missing:
        problems.append("%d Register row(s) are in no task, first: %s" % (len(missing), missing[0]))
    # the watchdog reads the same directory
    import watchdog  # noqa: E402

    seen = watchdog.read_task_files(out_dir)
    for tid in tasks:
        if tid not in seen:
            problems.append("%s: watchdog.read_task_files does not see it" % tid)
    index_path = os.path.join(out_dir, "index.json")
    if os.path.isfile(index_path):
        with open(index_path, "r", encoding="utf-8") as handle:
            index = json.load(handle)
        if index.get("total", {}).get("tasks") != len(tasks):
            problems.append("index.json: total.tasks %s != %d task files" % (index.get("total", {}).get("tasks"), len(tasks)))
        if index.get("total", {}).get("rows") != len(owner):
            problems.append("index.json: total.rows %s != %d rows in tasks" % (index.get("total", {}).get("rows"), len(owner)))
        listed = {t["id"] for t in index.get("tasks", [])}
        if listed != set(tasks):
            problems.append("index.json: task list differs from the task files")
    else:
        problems.append("index.json is missing")
    return problems


def read_task_files_strict(out_dir, problems):
    found = {}
    if not os.path.isdir(out_dir):
        problems.append("no task directory %s" % out_dir)
        return found
    for fname in sorted(os.listdir(out_dir)):
        if not fname.endswith(".json") or fname == "index.json":
            continue
        try:
            with open(os.path.join(out_dir, fname), "r", encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError) as exc:
            problems.append("%s: unreadable: %s" % (fname, exc))
            continue
        if not isinstance(data, dict) or data.get("id") != fname[:-5]:
            problems.append("%s: id does not match the file name" % fname)
            continue
        found[data["id"]] = data
    return found


# --------------------------------------------------------------------------------------
# command line
# --------------------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate claimable task batches from the Register.")
    ap.add_argument("--register", default=DEFAULT_REGISTER)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--waves", default=DEFAULT_WAVES)
    ap.add_argument("--catalog", default=DEFAULT_CATALOG)
    ap.add_argument("--min-rows", type=int, default=MIN_ROWS)
    ap.add_argument("--max-rows", type=int, default=MAX_ROWS)
    ap.add_argument("--min-score", type=float, default=MIN_SCORE)
    ap.add_argument("--check", action="store_true", help="write nothing; exit 1 if the disk differs")
    ap.add_argument("--validate", action="store_true", help="write nothing; validate the queue on disk")
    ap.add_argument("--no-prune", action="store_true")
    ap.add_argument("--no-hints", action="store_true")
    args = ap.parse_args(argv)
    if args.min_rows < 1 or args.max_rows < args.min_rows:
        print("gen_tasks: need 1 <= min-rows <= max-rows", file=sys.stderr)
        return 2
    try:
        if args.validate:
            problems = validate_queue(args.out, args.register)
            print(json.dumps({"ok": not problems, "problems": problems[:50], "problem_count": len(problems)}, indent=2))
            return 1 if problems else 0
        result = generate(
            args.register,
            args.out,
            args.waves,
            args.catalog,
            args.min_rows,
            args.max_rows,
            args.min_score,
            hints_on=not args.no_hints,
        )
    except (OSError, ValueError) as exc:
        print("gen_tasks: %s" % exc, file=sys.stderr)
        return 2
    index = result["index"]
    summary = {"tasks": index["total"]["tasks"], "rows": index["total"]["rows"], "frozen": len(result["frozen"])}
    if args.check:
        problems = check_output(result, args.out, prune=not args.no_prune)
        summary["problems"] = problems[:50]
        summary["problem_count"] = len(problems)
        print(json.dumps(summary, indent=2))
        return 1 if problems else 0
    change = write_output(result, args.out, prune=not args.no_prune)
    summary.update({"written": len(change["written"]), "unchanged": change["unchanged"], "removed": len(change["removed"])})
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
