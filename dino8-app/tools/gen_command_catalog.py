#!/usr/bin/env python3
"""Generate dino8-app's command catalog from the Rhino 8 Command Line Reference text.

Input format (one entry per command, as extracted from the reference .docx):

    <CommandName>
    Toolbar(s): ...   |   Menu: ...
    <one-line description>
    id="mc-main-content">
    <help body ... until the next entry>

An entry boundary is any line immediately followed by a line starting with
"Toolbar(s):". Everything between two boundaries is that command's help body.

Output: data/commands.json - a list of {name, description, toolbars, menu,
options, help} objects, alphabetical, de-duplicated by name.
"""
import json
import re
import sys
from pathlib import Path

OPTION_RE = re.compile(r"^([A-Z][A-Za-z0-9]+)\s*(\((?:[A-Za-z0-9 /\-]+)\))?\s*$")
NOISE = {
    "Toolbar", "Menu", "Command-line options", "Example", "Note", "Notes", "Options",
    "See also", "Yes", "No", "Steps", "User's Guide", "Redirecting to topic. Please wait.",
}


def parse(text: str):
    lines = text.splitlines()
    entries = []
    boundaries = [i for i in range(len(lines) - 1) if lines[i + 1].startswith("Toolbar(s):")]
    for idx, start in enumerate(boundaries):
        end = boundaries[idx + 1] if idx + 1 < len(boundaries) else len(lines)
        name = lines[start].strip()
        if not name or " " in name and not name[0].isalnum():
            continue
        toolbar_line = lines[start + 1].strip()
        toolbars, menu = "", ""
        parts = toolbar_line.split("|", 1)
        toolbars = parts[0].replace("Toolbar(s):", "").strip()
        if len(parts) > 1:
            menu = parts[1].replace("Menu:", "").strip()
        description = ""
        body_start = start + 2
        if body_start < end and not lines[body_start].startswith("id="):
            description = lines[body_start].strip()
            body_start += 1
        body_lines = []
        for line in lines[body_start:end]:
            s = line.strip()
            if not s or s.startswith("id=") or s.startswith("[full page:") or s == name:
                continue
            body_lines.append(s)
        # Crude option extraction: lines under "Command-line options" that look
        # like an option token, optionally with a (Yes/No)-style value list.
        options = []
        in_options = False
        for s in body_lines:
            if s == "Command-line options":
                in_options = True
                continue
            if not in_options:
                continue
            if s in ("Example", "See also", "Note", "Notes"):
                in_options = False
                continue
            m = OPTION_RE.match(s)
            if m and s not in NOISE and len(s) <= 40:
                token = m.group(1)
                if token not in options:
                    options.append(token)
        help_text = "\n".join(body_lines)
        # Collapse "Redirecting to topic" stubs to an empty help body.
        if "Redirecting to topic" in help_text and len(body_lines) <= 2:
            help_text = ""
        entries.append({
            "name": name,
            "description": description,
            "toolbars": toolbars,
            "menu": menu,
            "options": options,
            "help": help_text,
        })
    # De-duplicate by name, preferring the entry with the longest help body.
    best = {}
    for e in entries:
        cur = best.get(e["name"])
        if cur is None or len(e["help"]) > len(cur["help"]):
            best[e["name"]] = e
    return sorted(best.values(), key=lambda e: e["name"].lower())


def main():
    if len(sys.argv) != 3:
        print("usage: gen_command_catalog.py <reference.txt> <out.json>", file=sys.stderr)
        return 2
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    entries = parse(src.read_text(encoding="utf-8", errors="replace"))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(entries, indent=1, ensure_ascii=False), encoding="utf-8")
    with_help = sum(1 for e in entries if e["help"])
    print(f"{len(entries)} commands ({with_help} with full help bodies) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
