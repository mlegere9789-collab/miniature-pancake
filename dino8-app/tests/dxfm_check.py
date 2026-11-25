#!/usr/bin/env python3
"""Independent DXF reader for the DXFM_ section (tests/sections/69_dxfm.sh).

Usage: dxfm_check.py <dir holding m1.dxf> [<extra sys.path dir holding ezdxf>]

Parses the file Dino 8's ExportDxf wrote with its own group-code reader (nothing shared with the app) and, when ezdxf can be
imported, with ezdxf as well. Every line printed starts with "ok   ", "FAIL " or "SKIP ".

Scenario (tests/dxfm_script.txt): a hand-written DXF with one MTEXT (insertion (10,20,0), height 2.5, reference width 30, attachment
1, rotation 30 degrees, text "First line\\PSecond line\\PThird") is opened, an annotation style Arch (text height 3.5, arrow 2, font
DejaVuSans.ttf, 2 decimals, 1 angular decimal, suffix mm, extension gap 1, overshoot 1.5, text centred) is made, one single-line Text
and one DimLinear (0,0)-(40,0) are made in it, and everything is exported. Oracles, all closed form:
  * exactly one MTEXT with those values (direction (cos 30, sin 30, 0), the three lines joined by \\P) and no glyph polylines left;
  * STYLE records Standard, Default and Arch (Arch's font DejaVuSans.ttf); the MTEXT names Standard, the TEXT "Plan" names Arch;
  * DIMSTYLE records Standard, Default and Arch with Arch's DIMTXT 3.5, DIMASZ 2, DIMEXO 1, DIMEXE 1.5, DIMTAD 0 (centred), DIMDEC 2,
    DIMADEC 1, DIMPOST "<> mm", and Default's DIMTXT 2 (twice the grid spacing 1) and DIMTAD 1; the DIMENSION names Arch.
"""
import math
import os
import sys

d = sys.argv[1]
if len(sys.argv) > 2 and sys.argv[2]:
    sys.path.insert(0, sys.argv[2])
fails = 0


def chk(cond, name):
    global fails
    if cond:
        print("ok   " + name)
    else:
        print("FAIL " + name)
        fails += 1


def close(a, b, tol=1e-6):
    return abs(a - b) <= tol * max(1.0, abs(b))


with open(os.path.join(d, "m1.dxf"), encoding="utf-8", errors="replace") as f:
    lines = f.read().split("\n")
prs = []
for i in range(0, len(lines) - 1, 2):
    try:
        prs.append((int(lines[i].strip()), lines[i + 1].strip()))
    except ValueError:
        pass


def records(body):
    recs = []
    for c, v in body:
        if c == 0:
            recs.append([v, []])
        elif recs:
            recs[-1][1].append((c, v))
    return recs


sections = {}
i = 0
while i < len(prs):
    if prs[i] == (0, "SECTION"):
        name = prs[i + 1][1]
        j = i + 2
        while j < len(prs) and prs[j] != (0, "ENDSEC"):
            j += 1
        sections[name] = records(prs[i + 2:j])
        i = j
    i += 1

tables = {}
cur = None
for r in sections.get("TABLES", []):
    if r[0] == "TABLE":
        cur = dict(r[1]).get(2)
        tables[cur] = []
    elif r[0] != "ENDTAB" and cur:
        tables[cur].append(r)


def g(rec, code, default=None):
    for c, v in rec[1]:
        if c == code:
            return v
    return default


def gf(rec, code, default=None):
    v = g(rec, code)
    return float(v) if v is not None else default


ents = sections.get("ENTITIES", [])
styles = {g(r, 2): r for r in tables.get("STYLE", [])}
dimstyles = {g(r, 2): r for r in tables.get("DIMSTYLE", [])}

# --- MTEXT ---------------------------------------------------------------------------------------------------------------------
mt = [e for e in ents if e[0] == "MTEXT"]
chk(len(mt) == 1, "DXFM mtext: the multi-line text is written as one MTEXT entity (%d MTEXT, %d LWPOLYLINE glyph curves)" % (len(mt), sum(1 for e in ents if e[0] == "LWPOLYLINE")))
chk(not any(e[0] == "LWPOLYLINE" for e in ents), "DXFM mtext: no baked glyph polylines are left beside it")
if mt:
    e = mt[0]
    pos = (gf(e, 10, 0), gf(e, 20, 0), gf(e, 30, 0))
    chk(max(abs(a - b) for a, b in zip(pos, (10, 20, 0))) < 1e-6, "DXFM mtext: attachment point (10,20,0) (%s)" % (pos,))
    chk(close(gf(e, 40, -1), 2.5), "DXFM mtext: height 2.5 (%s)" % g(e, 40))
    chk(close(gf(e, 41, -1), 30.0), "DXFM mtext: reference width 30 (%s)" % g(e, 41))
    chk(g(e, 71) == "1", "DXFM mtext: attachment 1, top left (%s)" % g(e, 71))
    dx, dy = gf(e, 11, 0), gf(e, 21, 0)
    chk(close(dx, math.cos(math.radians(30))) and close(dy, math.sin(math.radians(30))), "DXFM mtext: direction (cos 30, sin 30) (%s, %s)" % (dx, dy))
    chk(close(gf(e, 50, -1), 30.0), "DXFM mtext: rotation 30 degrees (%s)" % g(e, 50))
    text = "".join(v for c, v in e[1] if c == 3) + (g(e, 1) or "")
    chk(text.split("\\P") == ["First line", "Second line", "Third"], "DXFM mtext: the three lines joined by \\P (%r)" % text)
    chk(g(e, 7) == "Standard" and "Standard" in styles, "DXFM mtext: style Standard (%s) is in the STYLE table" % g(e, 7))

# --- text style ----------------------------------------------------------------------------------------------------------------
chk({"Standard", "Default", "Arch"} <= set(styles), "DXFM STYLE: records Standard, Default and Arch (%s)" % sorted(styles))
if "Arch" in styles:
    chk(g(styles["Arch"], 3) == "DejaVuSans.ttf", "DXFM STYLE: Arch uses the font DejaVuSans.ttf (%s)" % g(styles["Arch"], 3))
tx = [e for e in ents if e[0] == "TEXT" and g(e, 1) == "Plan"]
chk(len(tx) == 1, "DXFM text: the single-line text 'Plan' is one TEXT entity")
if tx:
    chk(g(tx[0], 7) == "Arch" and close(gf(tx[0], 40, -1), 3.5), "DXFM text: 'Plan' names its style Arch, height 3.5 (%s, %s)" % (g(tx[0], 7), g(tx[0], 40)))

# --- dimension style -----------------------------------------------------------------------------------------------------------
chk({"Standard", "Default", "Arch"} <= set(dimstyles), "DXFM DIMSTYLE: records Standard, Default and Arch (%s)" % sorted(dimstyles))
if "Arch" in dimstyles:
    r = dimstyles["Arch"]
    want = {140: 3.5, 41: 2.0, 42: 1.0, 44: 1.5, 77: 0, 271: 2, 179: 1}
    got = {k: gf(r, k) for k in want}
    chk(all(got[k] is not None and close(got[k], v) for k, v in want.items()),
        "DXFM DIMSTYLE: Arch has DIMTXT 3.5, DIMASZ 2, DIMEXO 1, DIMEXE 1.5, DIMTAD 0, DIMDEC 2, DIMADEC 1 (%s)" % got)
    chk(g(r, 3) == "<> mm", "DXFM DIMSTYLE: Arch's DIMPOST is '<> mm' (%s)" % g(r, 3))
if "Default" in dimstyles:
    r = dimstyles["Default"]
    chk(close(gf(r, 140, -1), 2.0) and gf(r, 77) == 1, "DXFM DIMSTYLE: Default has DIMTXT 2 (twice the grid spacing 1) and DIMTAD 1 (%s, %s)" % (g(r, 140), g(r, 77)))
dm = [e for e in ents if e[0] == "DIMENSION"]
chk(len(dm) == 1 and g(dm[0], 3) == "Arch", "DXFM dimension: the DIMENSION names its dimension style Arch (%s)" % (g(dm[0], 3) if dm else None))

# --- ezdxf (optional) ----------------------------------------------------------------------------------------------------------
try:
    import ezdxf
except Exception as ex:
    print("SKIP DXFM ezdxf: not importable (%s); the closed-form checks above are the whole verdict" % ex)
    ezdxf = None
if ezdxf:
    doc = ezdxf.readfile(os.path.join(d, "m1.dxf"))
    aud = doc.audit()
    chk(len(aud.errors) == 0 and len(aud.fixes) == 0, "DXFM ezdxf: audit finds nothing to fix (errors %d, fixes %d)" % (len(aud.errors), len(aud.fixes)))
    doc = ezdxf.readfile(os.path.join(d, "m1.dxf"))
    m = list(doc.modelspace().query("MTEXT"))
    if m:
        e = m[0]
        chk(e.plain_text().replace("\r", "").split("\n") == ["First line", "Second line", "Third"], "DXFM ezdxf: MTEXT plain text is the three lines (%r)" % e.plain_text())
        chk(close(e.dxf.char_height, 2.5) and close(e.dxf.width, 30.0) and close(e.dxf.rotation if e.dxf.hasattr("rotation") else math.degrees(math.atan2(e.dxf.text_direction.y, e.dxf.text_direction.x)), 30.0),
            "DXFM ezdxf: MTEXT height 2.5, width 30, rotation 30")
    else:
        chk(False, "DXFM ezdxf: no MTEXT")
    ds = doc.dimstyles.get("Arch") if "Arch" in doc.dimstyles else None
    if ds is not None:
        chk(close(ds.dxf.dimtxt, 3.5) and close(ds.dxf.dimasz, 2.0) and ds.dxf.dimdec == 2 and ds.dxf.dimpost == "<> mm", "DXFM ezdxf: dimension style Arch reads DIMTXT 3.5, DIMASZ 2, DIMDEC 2, DIMPOST '<> mm'")
    else:
        chk(False, "DXFM ezdxf: dimension style Arch exists")

print("dxfm_check: %d failure(s)" % fails)
sys.exit(1 if fails else 0)
