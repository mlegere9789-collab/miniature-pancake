#!/usr/bin/env python3
"""Clean-room guard: fail if vendor-derived text is present in the shipped data files.

Rules (see docs/clean-room/VENDOR_TEXT_SCRUB.md):

  * dino8-app/data/commands.json
      - every entry has a non-empty description of at most 25 words;
      - the help field is empty (or at most 25 words);
      - the product names McNeel and Rhinoceros appear nowhere, and the word Rhino appears
        in no description, help, toolbar or menu text;
      - only the optional "compat" field (at most 12 words) may carry a product name, as a short
        nominative compatibility note;
      - no description equals one of the vendor-derived descriptions that existed before the
        scrub (compared by digest; docs/clean-room/vendor_text_hashes.txt holds digests only);
      - no entry carries a vendor documentation URL or a "[full page" marker.
  * dino8-app/docs/site/commands.json and commands_data.js: the same description rules.
  * dino8-app/data/i18n/*.json: McNeel / Rhinoceros appear only in the attribution string
    about.kernel (the OpenNURBS library credit); English cmddesc.* values obey the description
    rules and equal the catalog description of the same command.
  * dino8-app/data/hatchpatterns.pat: pattern descriptions obey the description rules.
  * every other text file under dino8-app/data: no McNeel / Rhinoceros.

Usage:
    python3 tools/check_vendor_text.py            # check the tree this script lives in
    python3 tools/check_vendor_text.py --self-test  # prove the checks fire on planted violations
Exit status 0 = clean, 1 = violations (printed one per line), 2 = usage or read error.
"""
import hashlib
import json
import os
import re
import sys

MAX_WORDS = 25
MAX_COMPAT_WORDS = 12
HASH_LEN = 20
PRODUCT_ANYWHERE = re.compile(r"McNeel|Rhinoceros", re.I)
PRODUCT_RHINO = re.compile(r"\bRhino\b|\bRhino\d", re.I)
VENDOR_URL = re.compile(r"mcneel|rhino3d|\[full page|https?://", re.I)
# (file relative to dino8-app/data, key) pairs where a product name is an attribution, not text.
ALLOWED_ATTRIBUTION = {("i18n", "about.kernel")}


def norm(s):
    return re.sub(r"[^a-z0-9]+", " ", s.lower().replace("‑", "-")).strip()


def digest(s):
    return hashlib.sha256(norm(s).encode("utf-8")).hexdigest()[:HASH_LEN]


def words(s):
    return len(s.split())


def load_denylist(root):
    path = os.path.join(root, "docs", "clean-room", "vendor_text_hashes.txt")
    out = set()
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    out.add(line)
    except OSError as e:
        raise SystemExit("check_vendor_text: cannot read %s: %s" % (path, e))
    return out


def check_description(where, text, deny, problems, allow_empty=False):
    if not isinstance(text, str):
        problems.append("%s: description is not a string" % where)
        return
    if not text.strip():
        if not allow_empty:
            problems.append("%s: description is empty" % where)
        return
    n = words(text)
    if n > MAX_WORDS:
        problems.append("%s: description has %d words (limit %d)" % (where, n, MAX_WORDS))
    if PRODUCT_ANYWHERE.search(text) or PRODUCT_RHINO.search(text):
        problems.append("%s: description names a vendor product" % where)
    if VENDOR_URL.search(text):
        problems.append("%s: description carries a vendor URL or page marker" % where)
    if digest(text) in deny:
        problems.append("%s: description equals a vendor-derived description removed in the scrub" % where)


def check_catalog_entries(label, entries, deny, problems):
    seen = set()
    for i, e in enumerate(entries):
        if not isinstance(e, dict):
            problems.append("%s[%d]: entry is not an object" % (label, i))
            continue
        name = e.get("name")
        where = "%s %s" % (label, name if isinstance(name, str) and name else "#%d" % i)
        if not isinstance(name, str) or not name:
            problems.append("%s: missing name" % where)
        elif name.lower() in seen:
            problems.append("%s: duplicate name" % where)
        else:
            seen.add(name.lower())
        check_description(where, e.get("description"), deny, problems)
        for key, val in e.items():
            if key == "compat":
                if not isinstance(val, str) or words(val) > MAX_COMPAT_WORDS:
                    problems.append("%s: compat note must be a string of at most %d words" % (where, MAX_COMPAT_WORDS))
                continue
            flat = val if isinstance(val, str) else json.dumps(val, ensure_ascii=False)
            if PRODUCT_ANYWHERE.search(flat):
                problems.append("%s: field %s names a vendor company or product outside compat" % (where, key))
            if key in ("help", "toolbars", "menu") and isinstance(val, str) and PRODUCT_RHINO.search(val):
                problems.append("%s: field %s names a vendor product outside compat" % (where, key))
            if key == "help" and isinstance(val, str) and val.strip():
                if words(val) > MAX_WORDS:
                    problems.append("%s: help has %d words (limit %d)" % (where, words(val), MAX_WORDS))
                if VENDOR_URL.search(val):
                    problems.append("%s: help carries a vendor URL or page marker" % where)


def read_json(path, problems):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        problems.append("%s: cannot read as JSON (%s)" % (path, e))
        return None


def parse_pat_descriptions(text):
    out = []
    for line in text.splitlines():
        m = re.match(r"^\*([^,\s]+)\s*,\s*(.*)$", line.strip())
        if m:
            out.append((m.group(1), m.group(2)))
    return out


def run(root):
    problems = []
    deny = load_denylist(root)
    data = os.path.join(root, "dino8-app", "data")
    ndesc = 0
    maxw = 0

    catalog = read_json(os.path.join(data, "commands.json"), problems)
    cat_desc = {}
    if isinstance(catalog, list):
        check_catalog_entries("commands.json", catalog, deny, problems)
        for e in catalog:
            if isinstance(e, dict) and isinstance(e.get("description"), str):
                ndesc += 1
                maxw = max(maxw, words(e["description"]))
                if isinstance(e.get("name"), str):
                    cat_desc[e["name"].lower()] = e["description"]
    elif catalog is not None:
        problems.append("commands.json: top level is not a list")

    site = os.path.join(root, "dino8-app", "docs", "site")
    sj = read_json(os.path.join(site, "commands.json"), problems) if os.path.exists(os.path.join(site, "commands.json")) else None
    if isinstance(sj, dict) and isinstance(sj.get("commands"), list):
        check_catalog_entries("docs/site/commands.json", sj["commands"], deny, problems)
    js = os.path.join(site, "commands_data.js")
    if os.path.exists(js):
        with open(js, encoding="utf-8") as f:
            t = f.read()
        if PRODUCT_ANYWHERE.search(t):
            problems.append("docs/site/commands_data.js: names a vendor company or product")

    i18n_dir = os.path.join(data, "i18n")
    if os.path.isdir(i18n_dir):
        for fn in sorted(os.listdir(i18n_dir)):
            if not fn.endswith(".json"):
                continue
            tab = read_json(os.path.join(i18n_dir, fn), problems)
            if not isinstance(tab, dict):
                continue
            for key, val in tab.items():
                if not isinstance(val, str):
                    continue
                if PRODUCT_ANYWHERE.search(val) and ("i18n", key) not in ALLOWED_ATTRIBUTION:
                    problems.append("i18n/%s: key %s names a vendor company or product" % (fn, key))
                if key.startswith("cmddesc."):
                    if fn == "en.json":
                        check_description("i18n/en.json %s" % key, val, deny, problems)
                        want = cat_desc.get(key[len("cmddesc."):])
                        if want is not None and want != val:
                            problems.append("i18n/en.json %s differs from the catalog description" % key)
                    elif not val.strip():
                        problems.append("i18n/%s: %s is empty" % (fn, key))

    for fn in sorted(os.listdir(data)):
        p = os.path.join(data, fn)
        if fn.endswith(".pat"):
            with open(p, encoding="utf-8") as f:
                for name, desc in parse_pat_descriptions(f.read()):
                    check_description("%s *%s" % (fn, name), desc, deny, problems, allow_empty=True)

    for dirpath, _dirs, files in os.walk(data):
        for fn in files:
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, data)
            if rel.startswith("i18n" + os.sep) or fn == "commands.json":
                continue
            try:
                with open(p, encoding="utf-8") as f:
                    t = f.read()
            except (OSError, UnicodeDecodeError):
                continue
            if PRODUCT_ANYWHERE.search(t):
                problems.append("data/%s: names a vendor company or product" % rel)
    return problems, ndesc, maxw


def self_test(root):
    """Plant one violation of every kind in memory and confirm each is reported."""
    deny = {digest("A known removed vendor sentence")}
    cases = [
        ("too long", {"name": "A", "description": " ".join(["word"] * (MAX_WORDS + 1))}),
        ("empty", {"name": "B", "description": ""}),
        ("McNeel", {"name": "C", "description": "Made by McNeel."}),
        ("Rhinoceros field", {"name": "D", "description": "Fine.", "menu": "Help | About Rhinoceros"}),
        ("Rhino in help", {"name": "E", "description": "Fine.", "help": "Open Rhino."}),
        ("denylist", {"name": "F", "description": "A known removed vendor sentence."}),
        ("url", {"name": "G", "description": "See docs.mcneel.com for more."}),
        ("long help", {"name": "H", "description": "Fine.", "help": " ".join(["w"] * 40)}),
        ("long compat", {"name": "I", "description": "Fine.", "compat": " ".join(["w"] * 13)}),
    ]
    ok = True
    for label, entry in cases:
        problems = []
        check_catalog_entries("self-test", [entry], deny, problems)
        if not problems:
            print("self-test FAILED: planted violation not detected: %s" % label)
            ok = False
    clean = []
    check_catalog_entries("self-test", [
        {"name": "Ok", "description": "Moves selected objects.", "compat": "Command names follow Rhino 8.",
         "toolbars": "Standard", "menu": "Edit | Move", "options": ["Copy"], "help": ""}], deny, clean)
    if clean:
        print("self-test FAILED: a clean entry was rejected: %s" % clean)
        ok = False
    print("self-test %s" % ("passed (%d planted violations detected, 1 clean entry accepted)" % len(cases) if ok else "FAILED"))
    return ok


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if "--self-test" in argv:
        return 0 if self_test(root) else 1
    if len(argv) > 1:
        print(__doc__)
        return 2
    problems, ndesc, maxw = run(root)
    for p in problems:
        print("FAIL " + p)
    if problems:
        print("check_vendor_text: %d problem(s)" % len(problems))
        return 1
    print("check_vendor_text: OK (%d catalog descriptions, longest %d words, no vendor product names)" % (ndesc, maxw))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
