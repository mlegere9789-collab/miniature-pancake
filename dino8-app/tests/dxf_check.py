#!/usr/bin/env python3
"""Independent DXF checker for the DXLT_ smoke section (tests/sections/93_dxlt.sh). Python standard library only: it parses the
group-code/value pairs of an exported DXF itself (no ezdxf, which the runner does not have) and compares the LTYPE / LAYER tables and the
entities' linetype groups with values known from the DXF specification (element lengths: positive = dash, negative = gap, 0 = dot; group 40 =
total pattern length = sum of the absolute element lengths; group 73 = element count; group 6 = the linetype name, a name the LTYPE table
defines; ByBlock / ByLayer / Continuous are always defined) and from the documented Dino 8 defaults (Dashed 5,2; Center 10,2,2,2; Hidden 2,2).

  dxf_check.py export FILE   the drawing written by dxflt_export_script.txt
  dxf_check.py fixture OUT   writes the hand-built AutoCAD-style DXF read by dxflt_import_script.txt (LTYPE HIDDEN / CENTER / DOT / a complex
                             GAS_LINE; layers Cut (HIDDEN) and Plain (CONTINUOUS); six horizontal lines at y = 0, 10, ..., 50)
  dxf_check.py svg FILE      the SVG that Print wrote of the imported drawing: the six lines' stroke-dasharray values
  dxf_check.py reexport FILE the imported fixture exported again: the LTYPE table carries HIDDEN / CENTER / DOT with the file's patterns and the
                             layers Cut / Plain keep HIDDEN / Continuous
prints "ok   ..." / "FAIL ..." lines; exit status 1 when any FAIL
"""
import re
import sys

failed = 0


def check(cond, what):
    global failed
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failed += 1


def parse(path):
    """-> (sections: {name: [records]}) where a record is (type, [(code, value), ...]) split at every group 0."""
    lines = open(path, encoding='latin-1').read().split('\n')
    pairs = []
    for i in range(0, len(lines) - 1, 2):
        try:
            pairs.append((int(lines[i].strip()), lines[i + 1].strip()))
        except ValueError:
            pass
    sections, cur_sec, rec = {}, None, None
    i = 0
    while i < len(pairs):
        code, val = pairs[i]
        if code == 0 and val == 'SECTION':
            cur_sec = pairs[i + 1][1]
            sections[cur_sec] = []
            rec = None
            i += 2
            continue
        if code == 0 and val in ('ENDSEC', 'EOF'):
            cur_sec, rec = None, None
        elif code == 0 and cur_sec is not None:
            rec = (val, [])
            sections[cur_sec].append(rec)
        elif rec is not None:
            rec[1].append((code, val))
        i += 1
    return sections


def vals(rec, code):
    return [v for c, v in rec[1] if c == code]


def first(rec, code, default=None):
    v = vals(rec, code)
    return v[0] if v else default


def export(path):
    sec = parse(path)
    tables = sec.get('TABLES', [])
    ltypes = [r for r in tables if r[0] == 'LTYPE']
    layers = [r for r in tables if r[0] == 'LAYER']
    by_name = {first(r, 2): r for r in ltypes}
    check(all(n in by_name for n in ('ByBlock', 'ByLayer', 'Continuous')), 'LTYPE table defines ByBlock, ByLayer and Continuous (the file has %s)' % sorted(by_name))
    expect = {'Dashed': [5, -2], 'Center': [10, -2, 2, -2], 'Hidden': [2, -2], 'Cut': [6, -1.5, 0.5, -1.5]}
    for name, elems in expect.items():
        r = by_name.get(name)
        check(r is not None, 'LTYPE %s is defined' % name)
        if r is None:
            continue
        got = [float(v) for v in vals(r, 49)]
        check(got == elems, 'LTYPE %s elements %s (dash +, gap -) = %s' % (name, got, elems))
        check(int(first(r, 73, -1)) == len(elems), 'LTYPE %s element count (group 73) is %d' % (name, len(elems)))
        check(abs(float(first(r, 40, -1)) - sum(abs(e) for e in elems)) < 1e-9, 'LTYPE %s pattern length (group 40) is %g = sum |element|' % (name, sum(abs(e) for e in elems)))
        check(first(r, 72) == '65', 'LTYPE %s alignment code (72) is 65 (A)' % name)
    tab = [r for r in tables if r[0] == 'TABLE' and first(r, 2) == 'LTYPE']
    check(tab and int(first(tab[0], 70, -1)) == len(ltypes), 'the LTYPE table count (70) equals its %d records' % len(ltypes))
    lay = {first(r, 2): r for r in layers}
    check(first(lay.get('Walls', ('', [])), 6) == 'Hidden', 'layer Walls has linetype Hidden (group 6 = %s)' % first(lay.get('Walls', ('', [])), 6))
    check(first(lay.get('Default', ('', [])), 6) == 'Continuous', 'layer Default stays Continuous (group 6 = %s)' % first(lay.get('Default', ('', [])), 6))
    ents = [r for r in sec.get('ENTITIES', []) if r[0] == 'LINE']
    by_y = {}
    for e in ents:
        by_y[round(float(first(e, 20)), 6)] = e
    check(sorted(by_y) == [0.0, 5.0, 10.0], 'three LINE entities at y = 0, 5, 10 (got %s)' % sorted(by_y))
    if sorted(by_y) == [0.0, 5.0, 10.0]:
        check(first(by_y[0.0], 8) == 'Walls' and first(by_y[0.0], 6) is None, 'the Walls line has no entity linetype (ByLayer, so the layer decides)')
        check(first(by_y[5.0], 6) == 'Cut', 'the line set to Cut carries group 6 = Cut (got %s)' % first(by_y[5.0], 6))
        check(first(by_y[10.0], 6) is None, 'the plain line has no entity linetype')
    names = set(by_name)
    used = [first(r, 6) for r in layers + ents if first(r, 6) is not None]
    check(all(u in names for u in used), 'every linetype a layer or entity names is defined in the LTYPE table (%s)' % sorted(set(used)))
    handles = [v for r in tables + sec.get('ENTITIES', []) for v in vals(r, 5)]
    check(len(handles) == len(set(handles)), 'every handle (group 5) in the tables and entities is unique (%d handles)' % len(handles))


def reexport(path):
    sec = parse(path)
    tables = sec.get('TABLES', [])
    ltypes = [r for r in tables if r[0] == 'LTYPE']
    # DXF linetype names are case-insensitive and the document already has Hidden / Center, so the file's HIDDEN / CENTER come back under a
    # unique name (HIDDEN_2 ...): find each by its pattern and follow the name through the layers and entities.
    found = {}
    for name, elems in {'HIDDEN': [6.35, -3.175], 'CENTER': [31.75, -6.35, 6.35, -6.35], 'DOT': [0.635, -6.35]}.items():
        hits = [first(r, 2) for r in ltypes if [float(v) for v in vals(r, 49)] == elems]
        check(len(hits) == 1, 'exactly one LTYPE carries the %s elements %s (found %s)' % (name, elems, hits))
        found[name] = hits[0] if hits else None
        if hits:
            r = [r for r in ltypes if first(r, 2) == hits[0]][0]
            check(abs(float(first(r, 40)) - sum(abs(e) for e in elems)) < 1e-9 and int(first(r, 73)) == len(elems), 'LTYPE %s: pattern length %g and %d elements' % (hits[0], sum(abs(e) for e in elems), len(elems)))
    names = [first(r, 2).upper() for r in ltypes]
    check(len(names) == len(set(names)), 'LTYPE names are unique case-insensitively (%s)' % sorted(names))
    check(not any([float(v) for v in vals(r, 49)] == [10.0, -5.0] for r in ltypes), 'the complex linetype GAS_LINE was not defined, so it is not written back')
    lay = {first(r, 2): r for r in tables if r[0] == 'LAYER'}
    check(first(lay.get('Cut', ('', [])), 6) == found['HIDDEN'], 'layer Cut uses the imported HIDDEN pattern (group 6 = %s, LTYPE %s)' % (first(lay.get('Cut', ('', [])), 6), found['HIDDEN']))
    check(first(lay.get('Plain', ('', [])), 6) == 'Continuous', 'layer Plain is Continuous again (group 6 = %s)' % first(lay.get('Plain', ('', [])), 6))
    ents = {round(float(first(r, 20)), 6): r for r in sec.get('ENTITIES', []) if r[0] == 'LINE'}
    check(first(ents.get(10.0, ('', [])), 6) == found['CENTER'] and first(ents.get(20.0, ('', [])), 6) == found['DOT'], 'the explicit CENTER (y = 10) and DOT (y = 20) lines name the imported patterns (%s, %s)' % (found['CENTER'], found['DOT']))
    check(all(first(ents.get(y, ('', [])), 6) is None for y in (0.0, 30.0, 40.0, 50.0)), 'the ByLayer lines (y = 0, 30) and the lines with an undefined / complex linetype (y = 40, 50) carry no group 6')


def pairs_text(*kv):
    return ''.join('%s\n%s\n' % (k, v) for k, v in kv)


def fixture(out):
    def line(y, layer, lt=None):
        kv = [(0, 'LINE'), (8, layer)] + ([(6, lt)] if lt else []) + [(10, 0), (20, y), (30, 0), (11, 100), (21, y), (31, 0)]
        return pairs_text(*kv)

    def ltype(name, elems, complex_=False):
        kv = [(0, 'LTYPE'), (2, name), (70, 0), (3, name + ' description'), (72, 65), (73, len(elems)), (40, sum(abs(e) for e in elems))]
        for e in elems:
            kv += [(49, e), (74, 2 if complex_ else 0)]
        return pairs_text(*kv)

    text = pairs_text((0, 'SECTION'), (2, 'HEADER'), (9, '$ACADVER'), (1, 'AC1015'), (9, '$INSUNITS'), (70, 4), (0, 'ENDSEC'),
                      (0, 'SECTION'), (2, 'TABLES'), (0, 'TABLE'), (2, 'LTYPE'), (70, 6))
    text += ltype('ByBlock', []) + ltype('ByLayer', []) + ltype('CONTINUOUS', [])
    text += ltype('HIDDEN', [6.35, -3.175]) + ltype('CENTER', [31.75, -6.35, 6.35, -6.35]) + ltype('DOT', [0, -6.35]) + ltype('GAS_LINE', [10, -5], True)
    text += pairs_text((0, 'ENDTAB'), (0, 'TABLE'), (2, 'LAYER'), (70, 2),
                       (0, 'LAYER'), (2, 'Cut'), (70, 0), (62, 1), (6, 'HIDDEN'),
                       (0, 'LAYER'), (2, 'Plain'), (70, 0), (62, 7), (6, 'CONTINUOUS'),
                       (0, 'ENDTAB'), (0, 'ENDSEC'), (0, 'SECTION'), (2, 'ENTITIES'))
    text += line(0, 'Cut') + line(10, 'Plain', 'CENTER') + line(20, 'Plain', 'DOT') + line(30, 'Cut', 'BYLAYER') + line(40, 'Plain', 'GAS_LINE') + line(50, 'Plain')
    text += pairs_text((0, 'ENDSEC'), (0, 'EOF'))
    open(out, 'w').write(text)
    print('wrote', out)


def svg(path):
    text = open(path).read()
    paths = re.findall(r'<path stroke="#[0-9a-fA-F]{6}"((?: stroke-dasharray="[^"]*")?) d="([^"]*)"', text)
    rows = []
    for dash, d in paths:
        ys = [float(m) for m in re.findall(r'[ML]\s*[-0-9.]+ ([-0-9.]+)', d)]
        m = re.search(r'stroke-dasharray="([^"]*)"', dash)
        rows.append((ys[0], m.group(1) if m else None))
    check(len(rows) == 6, 'the SVG holds the six lines (got %d paths)' % len(rows))
    if len(rows) != 6:
        return
    rows.sort(reverse=True)  # SVG y grows downward: the largest y is the drawing's y = 0
    want = ['6.35,3.175', '31.75,6.35,6.35,6.35', '0.635,6.35', '6.35,3.175', None, None]
    why = ['layer Cut, no group 6 -> the layer linetype HIDDEN', 'explicit CENTER', 'explicit DOT (a dot becomes a dash of 0.1 x the smallest element)',
           'group 6 = BYLAYER on layer Cut -> HIDDEN', 'complex linetype GAS_LINE is not defined -> Continuous', 'layer Plain (CONTINUOUS), nothing set -> solid']
    for i, (y, dash) in enumerate(rows):
        check(dash == want[i], 'line y = %d: stroke-dasharray %s (%s)' % (10 * i, dash, why[i]))


cmd = sys.argv[1]
if cmd == 'export':
    export(sys.argv[2])
elif cmd == 'fixture':
    fixture(sys.argv[2])
elif cmd == 'svg':
    svg(sys.argv[2])
elif cmd == 'reexport':
    reexport(sys.argv[2])
else:
    print('usage: dxf_check.py export|fixture|svg FILE')
    sys.exit(2)
sys.exit(1 if failed else 0)
