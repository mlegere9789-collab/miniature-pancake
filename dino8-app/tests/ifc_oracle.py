#!/usr/bin/env python3
"""Stand-alone oracle for the IFC4 STEP files Dino 8 exports (standard library only, shares no code with the C++ writer).

    ifc_oracle.py FILE.ifc [--expect EXPECT.json]     check one file, print "ok   <check>" / "FAIL <check>" lines, exit 1 on a failure
    ifc_oracle.py --selftest                          prove the checks bite: corrupt a known-good file or its expectations in 10 ways, each must be caught

What it checks, from the text alone:
  * the ISO 10303-21 frame (HEADER with FILE_DESCRIPTION / FILE_NAME / FILE_SCHEMA IFC4, DATA, END-ISO-10303-21;)
  * every entity statement parses (typed values, nested lists, strings with '' and \\X2\\ escapes, enums, $ and *)
  * ids are unique and every #id that is referenced is defined; ids are positive
  * every IfcRoot GlobalId is 22 characters of the IFC base-64 alphabet, first character 0..3, unique in the file
  * the reference graph is typed: IfcRelAggregates / IfcRelContainedInSpatialStructure / IfcRelDefinesByProperties point at the
    right kinds of entity; project -> site -> building -> storey chain is complete; every element is contained in exactly one storey
  * each element has a placement and a SweptSolid body: IfcExtrudedAreaSolid over an IfcRectangleProfileDef
  * the written IfcElementQuantity values equal the values recomputed from that profile and depth (length, area, volume per kind)
  * with --expect: entity counts and named quantities equal hand-computed values from the JSON file:
        {"counts": {"IFCWALL": 1, ...}, "total_entities": 114, "quantities": {"Wall A": {"GrossVolume": 2.4, ...}}}
"""
import json
import math
import re
import sys

ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$"
ELEMENTS = {"IFCWALL", "IFCSLAB", "IFCCOLUMN", "IFCBEAM"}


class Ref:
    def __init__(self, n):
        self.n = n

    def __repr__(self):
        return "#%d" % self.n


class Enum:
    def __init__(self, s):
        self.s = s

    def __repr__(self):
        return "." + self.s + "."


class Typed:
    def __init__(self, name, args):
        self.name, self.args = name, args


class StepError(Exception):
    pass


class Parser:
    def __init__(self, text):
        self.t, self.i = text, 0

    def ws(self):
        while self.i < len(self.t) and self.t[self.i] in " \t\r\n":
            self.i += 1

    def peek(self):
        self.ws()
        return self.t[self.i] if self.i < len(self.t) else ""

    def expect(self, ch):
        if self.peek() != ch:
            raise StepError("expected %r at offset %d, found %r" % (ch, self.i, self.peek()))
        self.i += 1

    def string(self):
        assert self.t[self.i] == "'"
        self.i += 1
        out = []
        while True:
            if self.i >= len(self.t):
                raise StepError("unterminated string")
            c = self.t[self.i]
            if c == "'":
                if self.t.startswith("''", self.i):
                    out.append("'")
                    self.i += 2
                    continue
                self.i += 1
                break
            if c == "\\":
                if self.t.startswith("\\\\", self.i):
                    out.append("\\")
                    self.i += 2
                    continue
                m = re.compile(r"\\X2\\((?:[0-9A-F]{4})+)\\X0\\").match(self.t, self.i)
                if m:
                    h = m.group(1)
                    out.append("".join(chr(int(h[k:k + 4], 16)) for k in range(0, len(h), 4)))
                    self.i = m.end()
                    continue
                raise StepError("bad escape in string at offset %d" % self.i)
            out.append(c)
            self.i += 1
        return "".join(out)

    def value(self):
        c = self.peek()
        if c == "#":
            m = re.compile(r"#(\d+)").match(self.t, self.i)
            if not m:
                raise StepError("bad reference at offset %d" % self.i)
            self.i = m.end()
            return Ref(int(m.group(1)))
        if c == "'":
            return self.string()
        if c == "$" or c == "*":
            self.i += 1
            return c
        if c == ".":
            m = re.compile(r"\.([A-Z_0-9]+)\.").match(self.t, self.i)
            if not m:
                raise StepError("bad enumeration at offset %d" % self.i)
            self.i = m.end()
            return Enum(m.group(1))
        if c == "(":
            return self.args()
        m = re.compile(r"[-+]?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?").match(self.t, self.i)
        if m and (c.isdigit() or c in "+-"):
            self.i = m.end()
            s = m.group(0)
            return float(s) if ("." in s or "e" in s or "E" in s) else int(s)
        m = re.compile(r"[A-Z][A-Z0-9_]*").match(self.t, self.i)
        if m:
            name = m.group(0)
            self.i = m.end()
            return Typed(name, self.args())
        raise StepError("unexpected %r at offset %d" % (c, self.i))

    def args(self):
        self.expect("(")
        out = []
        if self.peek() == ")":
            self.i += 1
            return out
        while True:
            out.append(self.value())
            c = self.peek()
            if c == ",":
                self.i += 1
                continue
            if c == ")":
                self.i += 1
                return out
            raise StepError("expected , or ) at offset %d" % self.i)


def parse_file(text):
    """Returns (header_text, entities {id: (NAME, args)}, duplicate ids, errors)."""
    errors, dups, ents = [], [], {}
    m = re.search(r"\nDATA;\n", text)
    e = text.rfind("ENDSEC;\nEND-ISO-10303-21;")
    if not text.startswith("ISO-10303-21;") or not m or e < 0:
        return text[: m.start()] if m else text, ents, dups, ["missing ISO-10303-21 frame (header, DATA; or END-ISO-10303-21;)"]
    header, body = text[: m.start()], text[m.end():e]
    p = Parser(body)
    while True:
        c = p.peek()
        if c == "":
            break
        try:
            if c != "#":
                raise StepError("statement does not start with #id at offset %d" % p.i)
            mm = re.compile(r"#(\d+)\s*=\s*([A-Z][A-Z0-9_]*)").match(body, p.i)
            if not mm:
                raise StepError("bad entity header at offset %d" % p.i)
            p.i = mm.end()
            args = p.args()
            p.expect(";")
            n = int(mm.group(1))
            if n in ents:
                dups.append(n)
            ents[n] = (mm.group(2), args)
        except StepError as ex:
            errors.append(str(ex))
            nxt = body.find(";", p.i)
            if nxt < 0:
                break
            p.i = nxt + 1
    return header, ents, dups, errors


def refs_in(v):
    if isinstance(v, Ref):
        yield v.n
    elif isinstance(v, list):
        for x in v:
            yield from refs_in(x)
    elif isinstance(v, Typed):
        yield from refs_in(v.args)


def num(v):
    return float(v) if isinstance(v, (int, float)) else None


class Checker:
    def __init__(self):
        self.results = []

    def check(self, name, ok, detail=""):
        self.results.append((name, bool(ok), detail))

    @property
    def failed(self):
        return [r for r in self.results if not r[1]]


def recompute(kind, xdim, ydim, depth):
    a = xdim * ydim
    if kind == "IFCWALL":
        return {"Length": xdim, "Width": ydim, "Height": depth, "GrossFootprintArea": a, "GrossSideArea": xdim * depth, "GrossVolume": a * depth}
    if kind == "IFCSLAB":
        return {"Depth": depth, "Perimeter": 2 * (xdim + ydim), "GrossArea": a, "GrossVolume": a * depth}
    return {"Length": depth, "CrossSectionArea": a, "GrossVolume": a * depth}


def element_quantities(ents, el):
    """Quantities written for element id `el`: {name: (entity kind, value)} from IfcRelDefinesByProperties -> IfcElementQuantity."""
    out = {}
    for _, (nm, a) in ents.items():
        if nm != "IFCRELDEFINESBYPROPERTIES":
            continue
        related = [r for r in refs_in(a[4])]
        rel = a[5].n if isinstance(a[5], Ref) else None
        if el in related and rel in ents and ents[rel][0] == "IFCELEMENTQUANTITY":
            for q in ents[rel][1][5]:
                if q.n in ents:
                    qn, qa = ents[q.n]
                    out[qa[0]] = (qn, num(qa[3]))
    return out


def verify(text, expect=None):
    c = Checker()
    header, ents, dups, errors = parse_file(text)
    c.check("frame: ISO-10303-21 header, DATA and END-ISO-10303-21", not any("frame" in e for e in errors), "; ".join(e for e in errors if "frame" in e))
    c.check("header: FILE_DESCRIPTION, FILE_NAME and FILE_SCHEMA IFC4", all(k in header for k in ("FILE_DESCRIPTION(", "FILE_NAME(")) and "FILE_SCHEMA(('IFC4'))" in header)
    c.check("syntax: every entity statement parses", not [e for e in errors if "frame" not in e] and len(ents) > 0, "; ".join(errors[:3]))
    c.check("ids: positive and unique", not dups and all(n > 0 for n in ents), "duplicates %s" % dups[:5])

    dangling = sorted({(n, r) for n, (_, a) in ents.items() for r in refs_in(a) if r not in ents})
    c.check("integrity: every #id reference is defined", not dangling, "dangling %s" % dangling[:5])

    # GlobalIds.
    guids = []
    for n, (nm, a) in ents.items():
        if nm.startswith("IFC") and a and isinstance(a[0], str) and nm in (
            "IFCPROJECT", "IFCSITE", "IFCBUILDING", "IFCBUILDINGSTOREY", "IFCWALL", "IFCSLAB", "IFCCOLUMN", "IFCBEAM", "IFCELEMENTQUANTITY",
            "IFCPROPERTYSET", "IFCRELAGGREGATES", "IFCRELCONTAINEDINSPATIALSTRUCTURE", "IFCRELDEFINESBYPROPERTIES"):
            guids.append((n, a[0]))
    bad = [(n, g) for n, g in guids if not (len(g) == 22 and g[0] in "0123" and all(ch in ALPHABET for ch in g))]
    c.check("guid: every GlobalId is 22 characters of the IFC alphabet, first 0..3", guids and not bad, "bad %s" % bad[:3])
    c.check("guid: GlobalIds are unique", len({g for _, g in guids}) == len(guids))

    kinds = lambda n: ents[n][0] if n in ents else None
    # Spatial hierarchy.
    projects = [n for n, (nm, _) in ents.items() if nm == "IFCPROJECT"]
    sites = [n for n, (nm, _) in ents.items() if nm == "IFCSITE"]
    blds = [n for n, (nm, _) in ents.items() if nm == "IFCBUILDING"]
    storeys = [n for n, (nm, _) in ents.items() if nm == "IFCBUILDINGSTOREY"]
    c.check("structure: exactly one project, site and building", len(projects) == len(sites) == len(blds) == 1)
    aggs = [(a[4].n, [r.n for r in a[5]]) for n, (nm, a) in ents.items() if nm == "IFCRELAGGREGATES" and isinstance(a[4], Ref)]
    chain = (
        bool(projects and sites and (projects[0], [sites[0]]) in aggs)
        and bool(sites and blds and (sites[0], [blds[0]]) in aggs)
        and (not storeys or any(p == blds[0] and sorted(ch) == sorted(storeys) for p, ch in aggs))
    )
    c.check("structure: project -> site -> building -> storeys aggregation chain is complete", chain)
    c.check("structure: IfcProject has a context and a unit assignment", projects and isinstance(ents[projects[0]][1][7], list) and isinstance(ents[projects[0]][1][8], Ref) and kinds(ents[projects[0]][1][8].n) == "IFCUNITASSIGNMENT")
    c.check("units: length, area, volume and plane angle are SI metre, square metre, cubic metre, radian",
            {a[1].s + "/" + a[3].s for _, (nm, a) in ents.items() if nm == "IFCSIUNIT"} == {"LENGTHUNIT/METRE", "AREAUNIT/SQUARE_METRE", "VOLUMEUNIT/CUBIC_METRE", "PLANEANGLEUNIT/RADIAN"})

    elements = [n for n, (nm, _) in ents.items() if nm in ELEMENTS]
    contained = {}
    ok_types = True
    for n, (nm, a) in ents.items():
        if nm == "IFCRELCONTAINEDINSPATIALSTRUCTURE":
            if not (isinstance(a[5], Ref) and kinds(a[5].n) == "IFCBUILDINGSTOREY"):
                ok_types = False
            for r in a[4]:
                contained[r.n] = contained.get(r.n, 0) + 1
                if kinds(r.n) not in ELEMENTS:
                    ok_types = False
        if nm == "IFCRELAGGREGATES":
            allowed = {"IFCPROJECT": {"IFCSITE"}, "IFCSITE": {"IFCBUILDING"}, "IFCBUILDING": {"IFCBUILDINGSTOREY"}}
            if kinds(a[4].n) not in allowed or any(kinds(r.n) not in allowed[kinds(a[4].n)] for r in a[5]):
                ok_types = False
        if nm == "IFCRELDEFINESBYPROPERTIES":
            if kinds(a[5].n) not in ("IFCELEMENTQUANTITY", "IFCPROPERTYSET") or any(kinds(r.n) not in ELEMENTS for r in a[4]):
                ok_types = False
    c.check("types: relationships connect the right kinds of entity", ok_types)
    c.check("structure: every element is contained in exactly one storey", all(contained.get(e) == 1 for e in elements) and set(contained) <= set(elements))

    # Geometry and quantities per element.
    names = {}
    geom_ok, qty_ok, qty_detail = True, True, []
    for el in elements:
        nm, a = ents[el]
        names[a[2]] = el
        place = a[5].n if isinstance(a[5], Ref) else None
        shape = a[6].n if isinstance(a[6], Ref) else None
        if kinds(place) != "IFCLOCALPLACEMENT" or kinds(shape) != "IFCPRODUCTDEFINITIONSHAPE":
            geom_ok = False
            continue
        reps = ents[shape][1][2]
        sol = None
        for r in reps:
            rn, ra = ents[r.n]
            if rn == "IFCSHAPEREPRESENTATION" and ra[1] == "Body" and ra[2] == "SweptSolid" and ra[3] and kinds(ra[3][0].n) == "IFCEXTRUDEDAREASOLID":
                sol = ents[ra[3][0].n][1]
        if sol is None or kinds(sol[0].n) != "IFCRECTANGLEPROFILEDEF":
            geom_ok = False
            continue
        prof = ents[sol[0].n][1]
        xd, yd, depth = num(prof[3]), num(prof[4]), num(sol[3])
        if None in (xd, yd, depth) or min(xd, yd, depth) <= 0:
            geom_ok = False
            continue
        want = recompute(nm, xd, yd, depth)
        have = element_quantities(ents, el)
        for qname, v in want.items():
            if qname not in have or have[qname][1] is None or abs(have[qname][1] - v) > 1e-9 * max(1.0, abs(v)):
                qty_ok = False
                qty_detail.append("%s.%s written %s, recomputed %s" % (a[2], qname, have.get(qname), v))
        for qname, (qk, v) in have.items():
            if (qname in want or qname.startswith(("Net", "Gross"))) and qk not in ("IFCQUANTITYLENGTH", "IFCQUANTITYAREA", "IFCQUANTITYVOLUME"):
                qty_ok = False
    c.check("geometry: each element has a placement and a SweptSolid body of an extruded rectangle", elements and geom_ok)
    c.check("quantities: every written base quantity equals the value recomputed from the profile and depth", elements and qty_ok, "; ".join(qty_detail[:3]))
    kind_of_q = {"Length": "IFCQUANTITYLENGTH", "Width": "IFCQUANTITYLENGTH", "Height": "IFCQUANTITYLENGTH", "Depth": "IFCQUANTITYLENGTH", "Perimeter": "IFCQUANTITYLENGTH",
                 "GrossFootprintArea": "IFCQUANTITYAREA", "GrossSideArea": "IFCQUANTITYAREA", "GrossArea": "IFCQUANTITYAREA", "CrossSectionArea": "IFCQUANTITYAREA", "GrossVolume": "IFCQUANTITYVOLUME"}
    kinds_ok = all(have_k == kind_of_q.get(qn, have_k) for el in elements for qn, (have_k, _) in element_quantities(ents, el).items())
    c.check("quantities: length, area and volume quantities use IfcQuantityLength, IfcQuantityArea and IfcQuantityVolume", kinds_ok)

    if expect:
        counts = {}
        for _, (nm, _) in ents.items():
            counts[nm] = counts.get(nm, 0) + 1
        if "total_entities" in expect:
            c.check("expected: %d entities" % expect["total_entities"], len(ents) == expect["total_entities"], "found %d" % len(ents))
        for k, v in sorted(expect.get("counts", {}).items()):
            c.check("expected: %d x %s" % (v, k), counts.get(k, 0) == v, "found %d" % counts.get(k, 0))
        for ename, qs in sorted(expect.get("quantities", {}).items()):
            el = names.get(ename)
            have = element_quantities(ents, el) if el else {}
            for qn, v in sorted(qs.items()):
                got = have.get(qn, (None, None))[1]
                c.check("expected: %s %s = %s" % (ename, qn, v), got is not None and abs(got - v) <= 1e-9 * max(1.0, abs(v)), "found %s" % got)
    return c


GOOD = None


def selftest():
    """The checks must bite: a hand-written good file passes, and each corruption is reported by the check that should report it."""
    good = "\n".join([
        "ISO-10303-21;", "HEADER;", "FILE_DESCRIPTION(('x'),'2;1');", "FILE_NAME('a','b',('c'),(''),'d','e','');", "FILE_SCHEMA(('IFC4'));", "ENDSEC;", "DATA;",
        "#1=IFCCARTESIANPOINT((0.,0.,0.));", "#2=IFCDIRECTION((0.,0.,1.));", "#3=IFCDIRECTION((1.,0.,0.));", "#4=IFCAXIS2PLACEMENT3D(#1,#2,#3);",
        "#5=IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#4,$);",
        "#6=IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.);", "#7=IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.);", "#8=IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.);", "#9=IFCSIUNIT(*,.PLANEANGLEUNIT.,$,.RADIAN.);",
        "#10=IFCUNITASSIGNMENT((#6,#7,#8,#9));", "#11=IFCPROJECT('0aaaaaaaaaaaaaaaaaaaa1',$,'P',$,$,$,$,(#5),#10);",
        "#12=IFCLOCALPLACEMENT($,#4);", "#13=IFCSITE('0aaaaaaaaaaaaaaaaaaaa2',$,'S',$,$,#12,$,$,.ELEMENT.,$,$,$,$,$);",
        "#14=IFCBUILDING('0aaaaaaaaaaaaaaaaaaaa3',$,'B',$,$,#12,$,$,.ELEMENT.,$,$,$);",
        "#15=IFCBUILDINGSTOREY('0aaaaaaaaaaaaaaaaaaaa4',$,'L',$,$,#12,$,$,.ELEMENT.,0.);",
        "#16=IFCRELAGGREGATES('0aaaaaaaaaaaaaaaaaaaa5',$,$,$,#11,(#13));", "#17=IFCRELAGGREGATES('0aaaaaaaaaaaaaaaaaaaa6',$,$,$,#13,(#14));",
        "#18=IFCRELAGGREGATES('0aaaaaaaaaaaaaaaaaaaa7',$,$,$,#14,(#15));",
        "#19=IFCAXIS2PLACEMENT2D(#1,#3);", "#20=IFCRECTANGLEPROFILEDEF(.AREA.,$,#19,2.,0.5);", "#21=IFCAXIS2PLACEMENT3D(#1,$,$);",
        "#22=IFCEXTRUDEDAREASOLID(#20,#21,#2,3.);", "#23=IFCSHAPEREPRESENTATION(#5,'Body','SweptSolid',(#22));", "#24=IFCPRODUCTDEFINITIONSHAPE($,$,(#23));",
        "#25=IFCWALL('0aaaaaaaaaaaaaaaaaaaa8',$,'W',$,$,#12,#24,$,.STANDARD.);",
        "#26=IFCQUANTITYLENGTH('Length',$,$,2.,$);", "#27=IFCQUANTITYLENGTH('Width',$,$,0.5,$);", "#28=IFCQUANTITYLENGTH('Height',$,$,3.,$);",
        "#29=IFCQUANTITYAREA('GrossFootprintArea',$,$,1.,$);", "#30=IFCQUANTITYAREA('GrossSideArea',$,$,6.,$);", "#31=IFCQUANTITYVOLUME('GrossVolume',$,$,3.,$);",
        "#32=IFCELEMENTQUANTITY('0aaaaaaaaaaaaaaaaaaaa9',$,'Qto_WallBaseQuantities',$,$,(#26,#27,#28,#29,#30,#31));",
        "#33=IFCRELDEFINESBYPROPERTIES('0aaaaaaaaaaaaaaaaaaaaA',$,$,$,(#25),#32);",
        "#34=IFCRELCONTAINEDINSPATIALSTRUCTURE('0aaaaaaaaaaaaaaaaaaaaB',$,$,$,(#25),#15);", "ENDSEC;", "END-ISO-10303-21;", ""])
    expect = {"total_entities": 34, "counts": {"IFCWALL": 1}, "quantities": {"W": {"GrossVolume": 3.0, "Length": 2.0}}}
    fails = []

    def run(label, text, must_fail, exp=expect):
        got = [n for n, ok, _ in verify(text, exp).results if not ok]
        if must_fail is None:
            if got:
                fails.append("%s: expected a clean file, failed %s" % (label, got))
        elif not any(must_fail in n for n in got):
            fails.append("%s: expected a failure containing %r, got %s" % (label, must_fail, got))

    run("good file", good, None)
    run("dangling reference", good.replace("(#26,#27,#28,#29,#30,#31)", "(#26,#27,#28,#29,#30,#99)"), "every #id reference is defined")
    run("duplicate id", good.replace("#33=IFCREL", "#32=IFCREL"), "ids: positive and unique")
    run("bad GlobalId", good.replace("0aaaaaaaaaaaaaaaaaaaa8", "0aaaaaaaa8"), "GlobalId is 22")
    run("duplicate GlobalId", good.replace("0aaaaaaaaaaaaaaaaaaaa8", "0aaaaaaaaaaaaaaaaaaaa7"), "GlobalIds are unique")
    run("wrong quantity", good.replace("'GrossVolume',$,$,3.,$", "'GrossVolume',$,$,3.1,$"), "written base quantity equals")
    run("missing trailer", good.replace("END-ISO-10303-21;", ""), "frame")
    run("syntax error", good.replace("#26=IFCQUANTITYLENGTH('Length',$,$,2.,$);", "#26=IFCQUANTITYLENGTH('Length',$,$,2.,$;"), "every entity statement parses")
    run("element not contained", good.replace("(#25),#15);", "(#25),#14);"), "relationships connect")
    run("wrong expectation", good, "expected: 2 x IFCWALL", {"counts": {"IFCWALL": 2}})
    run("wrong hand quantity", good, "expected: W GrossVolume", {"quantities": {"W": {"GrossVolume": 3.5}}})
    return fails


def main(argv):
    if "--selftest" in argv:
        fails = selftest()
        for f in fails:
            print("FAIL selftest " + f)
        if not fails:
            print("ok   selftest: a good file passes and each of 10 corruptions or wrong expectations is reported by its check")
        return 1 if fails else 0
    paths = [a for a in argv[1:] if not a.startswith("--")]
    expect = None
    if "--expect" in argv:
        expect = json.load(open(argv[argv.index("--expect") + 1]))
        paths = [p for p in paths if p != argv[argv.index("--expect") + 1]]
    if len(paths) != 1:
        print(__doc__)
        return 2
    c = verify(open(paths[0], encoding="utf-8").read(), expect)
    for name, ok, detail in c.results:
        print(("ok   " if ok else "FAIL ") + name + ("" if ok or not detail else " (" + detail + ")"))
    return 1 if c.failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
