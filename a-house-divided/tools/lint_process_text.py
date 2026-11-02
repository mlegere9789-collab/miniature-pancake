#!/usr/bin/env python3
"""No-process-text lint. Drafting phrases must not ship in data or specs.

Scans data/, docs/specs/, docs/adr/ and crates/*/src for phrases that betray authoring history.
A line containing 'lint-allow' is skipped. Exit 1 on any hit.
"""
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ["data", "docs/specs", "docs/adr", "crates", "shells"]
EXT = {".toml", ".json", ".md", ".rs"}
PATTERNS = [r"first draft", r"my earlier", r"my first", r"this session", r"in this pass", r"i could not", r"i have not verified",
            r"\bfixme\b", r"\bxxx\b", r"lorem ipsum", r"as an ai"]
RX = re.compile("|".join(PATTERNS), re.I)


def main():
    hits = 0
    for d in DIRS:
        for dp, dn, fn in os.walk(os.path.join(ROOT, d)):
            dn[:] = [x for x in dn if x not in {"target", "__pycache__"}]
            for name in fn:
                if os.path.splitext(name)[1] not in EXT:
                    continue
                p = os.path.join(dp, name)
                with open(p, encoding="utf-8", errors="ignore") as f:
                    for n, line in enumerate(f, 1):
                        if "lint-allow" in line:
                            continue
                        m = RX.search(line)
                        if m:
                            print("%s:%d: process text %r" % (os.path.relpath(p, ROOT), n, m.group(0)))
                            hits += 1
    print("lint_process_text: %d hits" % hits)
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
