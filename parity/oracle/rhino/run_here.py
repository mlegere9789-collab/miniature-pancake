# One-click launcher for the Observer step, to run inside Rhino 8's Script Editor (Python 3). No Claude session needed.
# 1. Edit REPO below to the folder where you cloned/unzipped the branch w0-rhino-oracle.
# 2. First run with LIMIT = 5 (smoke). If it prints "wrote 5 golden records", set LIMIT = 0 and run again for the full corpus.
# The full run also does a second pass into your temp folder and reports any record that differs between the two passes.
# Output: <REPO>\parity\oracle\rhino\golden\rhino8-<build>-<date>[-smoke].jsonl  (numbers and flags only).
import os, re, json, datetime, tempfile, runpy
import Rhino

REPO = r"C:\path\to\miniature-pancake"   # EDIT THIS
LIMIT = 5                                 # 5 = smoke test, 0 = full corpus

here = os.path.join(REPO, "parity", "oracle", "rhino")
cases = os.path.join(here, "cases.jsonl")
build = re.sub(r"[^0-9A-Za-z.]+", "-", str(Rhino.RhinoApp.Version))
stamp = datetime.date.today().isoformat()
name = "rhino8-%s-%s%s.jsonl" % (build, stamp, "-smoke" if LIMIT else "")
out1 = os.path.join(here, "golden", name)
os.environ["D8_CASES"] = cases
os.environ["D8_LIMIT"] = str(LIMIT)
os.environ["D8_OUT"] = out1
runpy.run_path(os.path.join(here, "run_rhino.py"))

if not LIMIT:
    out2 = os.path.join(tempfile.gettempdir(), "d8_pass2_" + name)
    os.environ["D8_OUT"] = out2
    runpy.run_path(os.path.join(here, "run_rhino.py"))

    def load(p):
        d = {}
        for line in open(p, encoding="utf-8"):
            if line.strip():
                r = json.loads(line)
                r.pop("ms", None)  # wall-clock differs by nature
                d[r["id"]] = r
        return d
    a, b = load(out1), load(out2)
    diff = sorted(k for k in a if a[k] != b.get(k))
    print("determinism: %d cases, %d differ between pass 1 and pass 2" % (len(a), len(diff)))
    for k in diff[:50]:
        print("  DIFFERS:", k)
print("Done. Golden file:", out1)
