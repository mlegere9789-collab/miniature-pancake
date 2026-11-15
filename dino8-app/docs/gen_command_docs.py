#!/usr/bin/env python3
"""Generates docs/site/commands.json, the data file behind the searchable
command reference page, from:

  * data/commands.json        - the 1055-command Rhino 8 catalog (name,
                                 one-line description, toolbar/menu location)
  * src/commands/cmd_*.cpp    - the actual Dino 8 command registrations,
                                 each a `Reg(e, "Name", factory, status, note)`
                                 call (status/note are optional, defaulting
                                 to CommandStatus::Implemented / "")

For every catalog command this cross-references the registration source to
report whether Dino 8 actually implements it (Implemented / Partial), or
whether it is still a stub that only opens the help text (Planned), plus any
implementor's note explaining the gap.

Run it from dino8-app/:
    python3 docs/gen_command_docs.py

It is safe to re-run any time src/commands/*.cpp or data/commands.json
changes -- it only reads those and rewrites docs/site/commands.json plus a
plain-text coverage summary on stdout.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # dino8-app/
CATALOG_PATH = os.path.join(ROOT, "data", "commands.json")
CMD_DIR = os.path.join(ROOT, "src", "commands")
OUT_PATH = os.path.join(ROOT, "docs", "site", "commands.json")
# A .js twin of the same data, assigned to a global, so the reference page
# can <script src="commands_data.js"> it directly and work when opened as a
# plain file:// page (fetch() of local JSON is blocked by CORS in most
# browsers without a local server; a same-origin-agnostic <script> tag is
# not).
OUT_JS_PATH = os.path.join(ROOT, "docs", "site", "commands_data.js")

STATUS_RE = re.compile(r"CommandStatus::(Implemented|Partial|Planned)")


def find_matching_paren(text, open_idx):
    """Given the index of an opening '(' in text, return the index of its
    matching ')' accounting for nested parens, string/char literals, and
    // and /* */ comments (a bare apostrophe in a comment like "it's" must
    not be mistaken for the start of a char literal)."""
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            i = text.find('\n', i)
            if i < 0:
                return -1
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            end = text.find('*/', i + 2)
            i = end + 2 if end >= 0 else n
        elif c == '"':
            i += 1
            while i < n and text[i] != '"':
                if text[i] == '\\':
                    i += 1
                i += 1
        elif c == "'":
            i += 1
            while i < n and text[i] != "'":
                if text[i] == '\\':
                    i += 1
                i += 1
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def extract_trailing_note(call_text):
    """Best-effort: pull the last plain string literal argument out of a
    Reg(...) call (the `note` parameter), skipping ones that are clearly
    inside a nested lambda body (heuristic: only look after the last
    top-level comma at depth 1)."""
    # Find top-level commas (depth counted from the call's own outer parens,
    # which the caller has already stripped so call_text starts right after
    # 'Reg(' and ends right before the matching ')').
    depth = 0
    top_commas = []
    i = 0
    n = len(call_text)
    while i < n:
        c = call_text[i]
        if c == '/' and i + 1 < n and call_text[i + 1] == '/':
            nl = call_text.find('\n', i)
            i = nl if nl >= 0 else n
            continue
        elif c == '/' and i + 1 < n and call_text[i + 1] == '*':
            end = call_text.find('*/', i + 2)
            i = end + 2 if end >= 0 else n
            continue
        elif c == '"':
            i += 1
            while i < n and call_text[i] != '"':
                if call_text[i] == '\\':
                    i += 1
                i += 1
        elif c == "'":
            i += 1
            while i < n and call_text[i] != "'":
                if call_text[i] == '\\':
                    i += 1
                i += 1
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ',' and depth == 0:
            top_commas.append(i)
        i += 1
    if len(top_commas) < 4:
        return ""
    # args: e, "Name", factory, status[, note]
    if len(top_commas) >= 4:
        tail = call_text[top_commas[3] + 1:].strip()
        m = re.match(r'^"((?:[^"\\]|\\.)*)"', tail)
        if m:
            return m.group(1).replace('\\"', '"').replace('\\n', ' ')
    return ""


def registration_order():
    """Dino 8's CommandEngine::Register() overwrites by name (registry_[key]
    = ...), so when two files register the same command (a handful of
    commands get a simple stub first and a real implementation later -- see
    the comments in src/app/Application.cpp), the one called LAST at
    startup is what actually runs. Returns the cmd_*.cpp filenames in the
    same order Application.cpp calls their RegisterXCommands(), so the
    generator can replay that same overwrite behaviour instead of an
    arbitrary (e.g. alphabetical) file order."""
    app_cpp = os.path.join(os.path.dirname(CMD_DIR), "app", "Application.cpp")
    all_files = sorted(f for f in os.listdir(CMD_DIR) if f.startswith("cmd_") and f.endswith(".cpp"))
    if not os.path.exists(app_cpp):
        return all_files
    with open(app_cpp, "r", encoding="utf-8", errors="replace") as f:
        app_text = f.read()
    # e.g. "RegisterSrfEditCommands(*engine_);" -> function name "RegisterSrfEditCommands"
    call_order = re.findall(r"\b(Register\w+Commands)\s*\(\s*\*?engine_?\s*\)", app_text)
    # Map each RegisterXCommands function name to the file that defines it.
    func_to_file = {}
    for fname in all_files:
        with open(os.path.join(CMD_DIR, fname), "r", encoding="utf-8", errors="replace") as f:
            t = f.read()
        for fm in re.finditer(r"^void (Register\w+Commands)\(", t, re.M):
            func_to_file[fm.group(1)] = fname
    ordered = []
    seen = set()
    for func in call_order:
        fname = func_to_file.get(func)
        if fname and fname not in seen:
            ordered.append(fname)
            seen.add(fname)
    # Any cmd_*.cpp not referenced by Application.cpp's call list (shouldn't
    # normally happen) is appended at the end so nothing is silently dropped.
    for fname in all_files:
        if fname not in seen:
            ordered.append(fname)
    return ordered


def parse_registrations():
    """Returns {lowercase_name: {"status": str, "note": str, "file": str}}"""
    regs = {}
    for fname in registration_order():
        path = os.path.join(CMD_DIR, fname)
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
        for m in re.finditer(r'\bReg\(e,\s*"([A-Za-z0-9_]+)"', text):
            name = m.group(1)
            reg_open = text.index("(", m.start())
            reg_close = find_matching_paren(text, reg_open)
            if reg_close < 0:
                continue
            call_body = text[reg_open + 1:reg_close]
            status_match = STATUS_RE.search(call_body)
            status = status_match.group(1) if status_match else "Implemented"
            note = extract_trailing_note(call_body) if status != "Implemented" or status_match else ""
            key = name.lower()
            # CommandEngine::Register() overwrites by name, and a handful of
            # commands are intentionally registered twice (a stub, then a
            # real implementation later in startup order -- see
            # registration_order() above) -- so the LAST registration in
            # startup order wins, matching the real app.
            regs[key] = {"status": status, "note": note, "file": fname}
    return regs


def parse_table_driven_registrations(catalog_names, already_found):
    """A handful of command families register their name through a local
    variable rather than a string literal directly in a Reg(e, "Name", ...)
    call (e.g. cmd_meshtools.cpp's `Metric metrics[]` table, looped with
    `Reg(e, m.extract, ...)` / `Reg(e, m.sel, ...)`), which
    parse_registrations() cannot see. Rather than writing a full C++
    expression evaluator, fall back to: for any catalog command not already
    matched, if its exact name appears as a quoted string literal anywhere
    else in src/commands/*.cpp, treat it as Implemented (a command that is
    genuinely unbuilt has no reason for its exact name to appear in the
    source at all)."""
    extra = {}
    remaining = [n for n in catalog_names if n.lower() not in already_found]
    if not remaining:
        return extra
    all_text = ""
    files_by_name = {}
    for fname in sorted(os.listdir(CMD_DIR)):
        if not (fname.startswith("cmd_") and fname.endswith(".cpp")):
            continue
        with open(os.path.join(CMD_DIR, fname), "r", encoding="utf-8", errors="replace") as f:
            t = f.read()
        all_text += t
        for name in remaining:
            if f'"{name}"' in t:
                files_by_name.setdefault(name, fname)
    for name in remaining:
        if f'"{name}"' in all_text:
            extra[name.lower()] = {
                "status": "Implemented",
                "note": "Registered through a table-driven command family rather than a "
                        "standalone Reg(...) call; see the source file for the shared implementation.",
                "file": files_by_name.get(name, ""),
            }
    return extra


def parse_aliases():
    """Best-effort extraction of default command-line aliases from
    CommandEngine::InstallDefaultAliases (alias -> canonical name), so the
    reference can show e.g. 'b' as an alias of Box."""
    path = os.path.join(CMD_DIR, "CommandEngine.cpp")
    aliases = {}
    if not os.path.exists(path):
        return aliases
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    # InstallDefaultAliases holds a `{"alias", "Canonical"}, ...` table.
    m = re.search(r"InstallDefaultAliases\(\)\s*\{.*?defaults\[\]\s*=\s*\{(.*?)\};", text, re.S)
    body = m.group(1) if m else text
    for am in re.finditer(r'\{\s*"([a-zA-Z0-9_]+)"\s*,\s*"([A-Za-z0-9_]+)"\s*\}', body):
        alias, canonical = am.group(1), am.group(2)
        aliases.setdefault(canonical.lower(), []).append(alias)
    return aliases


def main():
    with open(CATALOG_PATH, "r", encoding="utf-8") as f:
        catalog = json.load(f)

    regs = parse_registrations()
    regs.update(parse_table_driven_registrations([c["name"] for c in catalog], set(regs.keys())))
    aliases = parse_aliases()

    entries = []
    counts = {"Implemented": 0, "Partial": 0, "Planned": 0}
    for cmd in catalog:
        name = cmd["name"]
        key = name.lower()
        reg = regs.get(key)
        if reg:
            status = reg["status"]
            note = reg["note"]
            source = reg["file"]
        else:
            status = "Planned"
            note = ""
            source = ""
        counts[status] += 1
        entries.append({
            "name": name,
            "description": cmd.get("description", ""),
            "toolbars": cmd.get("toolbars", ""),
            "menu": cmd.get("menu", ""),
            "status": status,
            "note": note,
            "source": source,
            "aliases": aliases.get(key, []),
        })

    entries.sort(key=lambda e: e["name"].lower())

    payload = {
        "generated_from": "data/commands.json + src/commands/cmd_*.cpp",
        "total": len(entries),
        "counts": counts,
        "commands": entries,
    }

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=1)
        f.write("\n")

    with open(OUT_JS_PATH, "w", encoding="utf-8") as f:
        f.write("// Generated by docs/gen_command_docs.py -- do not edit by hand.\n")
        f.write("window.DINO8_COMMANDS = ")
        json.dump(payload, f, indent=1)
        f.write(";\n")

    print(f"Wrote {OUT_PATH}")
    print(f"Wrote {OUT_JS_PATH}")
    print(f"Total catalog commands: {len(entries)}")
    for status in ("Implemented", "Partial", "Planned"):
        print(f"  {status}: {counts[status]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
