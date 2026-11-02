#!/usr/bin/env python3
"""Forbidden-terms scan (gate G4). Scans product code/data/assets for reference-game names.

Usage: python3 tools/scan_forbidden.py [root]
Env:   FORBIDDEN_EXTRA=path to an extra term file kept outside the repository.
Scans crates/, shells/, data/, qc/ text files. Exit 1 on any hit.
"""
import os
import sys

ROOT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_DIRS = ["crates", "shells", "data", "qc"]
SKIP_DIRS = {"target", ".git", "__pycache__", "golden-replays"}
TEXT_EXT = {".rs", ".toml", ".json", ".md", ".txt", ".csv", ".py", ".yml", ".yaml", ".ron", ".wgsl", ".glsl"}


def load_terms():
    terms = []
    paths = [os.path.join(ROOT, "tools", "forbidden_terms.txt")]
    extra = os.environ.get("FORBIDDEN_EXTRA")
    if extra and os.path.exists(extra):
        paths.append(extra)
    for p in paths:
        with open(p, encoding="utf-8") as f:
            for line in f:
                line = line.strip().lower()
                if line and not line.startswith("#"):
                    terms.append(line)
    return terms


def main():
    terms = load_terms()
    hits = 0
    scanned = 0
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        for dp, dn, fn in os.walk(base):
            dn[:] = [x for x in dn if x not in SKIP_DIRS]
            for name in fn:
                if os.path.splitext(name)[1].lower() not in TEXT_EXT:
                    continue
                path = os.path.join(dp, name)
                scanned += 1
                try:
                    with open(path, encoding="utf-8") as f:
                        for n, line in enumerate(f, 1):
                            low = line.lower()
                            for t in terms:
                                if t in low:
                                    print("%s:%d: forbidden term %r" % (os.path.relpath(path, ROOT), n, t))
                                    hits += 1
                except UnicodeDecodeError:
                    continue
    print("scan_forbidden: %d files scanned, %d hits" % (scanned, hits))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
