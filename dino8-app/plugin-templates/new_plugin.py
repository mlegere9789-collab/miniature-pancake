#!/usr/bin/env python3
"""Generate a Dino 8 plug-in project from one of the templates in this folder.

    python3 new_plugin.py command        --out ./work --set plugin_name="Hello" --set plugin_slug=hello --set command_name=Hello
    python3 new_plugin.py import-export  --out ./work
    python3 new_plugin.py panel          --out ./work --defaults

Each template directory is a cookiecutter template (cookiecutter.json plus a
"{{cookiecutter.plugin_slug}}" project directory), so if you already use the
real `cookiecutter` tool, `cookiecutter plugin-templates/command` works too.
This script exists so that nothing but Python's standard library is needed.

Variables you can set with --set key=value (defaults come from cookiecutter.json):
  plugin_name   display name, shown in the Plug-in Manager and the marketplace
  plugin_slug   lower-case identifier [a-z][a-z0-9_]* : library file name and marketplace id
  command_name  command line command, CamelCase [A-Za-z][A-Za-z0-9]*
  version       dotted number, e.g. 1.0.0
  author, description, index_tags (a JSON fragment such as "a", "b")
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VAR = re.compile(r"\{\{\s*cookiecutter\.([A-Za-z_][A-Za-z0-9_]*)\s*\}\}")
CHECKS = {
    "plugin_slug": (re.compile(r"^[a-z][a-z0-9_]*$"), "lower-case letters, digits and underscores, starting with a letter"),
    "command_name": (re.compile(r"^[A-Za-z][A-Za-z0-9]*$"), "letters and digits only, starting with a letter"),
    "version": (re.compile(r"^[0-9]+(\.[0-9]+){0,3}$"), "dotted numbers such as 1.0.0"),
    # Values land inside C string literals and JSON strings: no quote, backslash or newline.
    "plugin_name": (re.compile(r'^[^"\\\n\r]+$'), 'no quotes, backslashes or newlines'),
    "author": (re.compile(r'^[^"\\\n\r]+$'), 'no quotes, backslashes or newlines'),
    "description": (re.compile(r'^[^"\\\n\r]+$'), 'no quotes, backslashes or newlines'),
}


def render(text, values):
    def sub(m):
        key = m.group(1)
        if key not in values:
            raise SystemExit("template uses unknown variable cookiecutter.%s" % key)
        return values[key]
    return VAR.sub(sub, text)


def main(argv):
    templates = sorted(
        d for d in os.listdir(HERE) if os.path.isfile(os.path.join(HERE, d, "cookiecutter.json"))
    )
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("template", choices=templates)
    ap.add_argument("--out", required=True, help="directory to create the project in (it must not already contain the project)")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE")
    ap.add_argument("--defaults", action="store_true", help="accept every default (the only mode; kept for cookiecutter muscle memory)")
    args = ap.parse_args(argv)

    tdir = os.path.join(HERE, args.template)
    with open(os.path.join(tdir, "cookiecutter.json"), encoding="utf-8") as f:
        values = {k: str(v) for k, v in json.load(f).items()}
    for kv in args.set:
        if "=" not in kv:
            ap.error("--set expects KEY=VALUE, got %r" % kv)
        k, v = kv.split("=", 1)
        if k not in values:
            ap.error("unknown variable %r (known: %s)" % (k, ", ".join(sorted(values))))
        values[k] = v
    for k, (rx, why) in CHECKS.items():
        if k in values and not rx.match(values[k]):
            ap.error("%s=%r is not valid: %s" % (k, values[k], why))

    project = None
    for entry in sorted(os.listdir(tdir)):
        p = os.path.join(tdir, entry)
        if os.path.isdir(p):
            project = entry
    if project is None:
        raise SystemExit("template %s has no project directory" % args.template)
    dest_root = os.path.join(args.out, render(project, values))
    if os.path.exists(dest_root):
        raise SystemExit("refusing to overwrite existing %s" % dest_root)

    src_root = os.path.join(tdir, project)
    count = 0
    for base, dirs, files in os.walk(src_root):
        dirs.sort()
        rel = os.path.relpath(base, src_root)
        out_base = dest_root if rel == "." else os.path.join(dest_root, *[render(p, values) for p in rel.split(os.sep)])
        os.makedirs(out_base, exist_ok=True)
        for name in sorted(files):
            with open(os.path.join(base, name), encoding="utf-8", newline="") as f:
                data = render(f.read(), values)
            with open(os.path.join(out_base, render(name, values)), "w", encoding="utf-8", newline="") as f:
                f.write(data)
            count += 1
    print("created %s (%d files)" % (dest_root, count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
