#!/usr/bin/env python3
"""Self-test of the SUDOC_* checkers (sudoc_rows.py): no application is run. For several cases a DXF file is AUTHORED HERE BY HAND in the form the
application writes (group codes per the DXF reference) from the values the case expects; the checker must accept it, and must reject copies of it that
are wrong in one value (a moved end point, a wrong radius, a missing entity, a swapped layer, one instance too many). This shows the checkers can fail
and that they read the file the way the DXF reference says. Prints `ok   ...` / `FAIL ...` lines, exit status 1 when any FAIL.
"""
import contextlib
import io
import math
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cad2d_rows as R  # noqa: E402
import sudoc_rows as S  # noqa: E402

bad = 0


def report(cond, what):
    global bad
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        bad += 1


def pairs(*kv):
    return ''.join('%s\n%s\n' % (c, v) for c, v in kv)


def ent(kind, *kv):
    return pairs((0, kind), *kv)


def line(x0, y0, x1, y1, layer='0'):
    return ent('LINE', (8, layer), (10, '%.9f' % x0), (20, '%.9f' % y0), (30, '0.0'), (11, '%.9f' % x1), (21, '%.9f' % y1), (31, '0.0'))


def arc(cx, cy, r, a0, a1, layer='0'):
    return ent('ARC', (8, layer), (10, '%.9f' % cx), (20, '%.9f' % cy), (30, '0.0'), (40, '%.9f' % r), (50, '%.9f' % a0), (51, '%.9f' % a1))


def poly(pts, layer='0', closed=True):
    kv = [(8, layer), (90, len(pts)), (70, 1 if closed else 0)]
    for x, y in pts:
        kv += [(10, '%.9f' % x), (20, '%.9f' % y)]
    return ent('LWPOLYLINE', *kv)


def insert(name, x, y, layer='0'):
    return ent('INSERT', (8, layer), (2, name), (10, '%.9f' % x), (20, '%.9f' % y), (30, '0.0'))


def block(name, bx, by, members):
    return ent('BLOCK', (8, '0'), (2, name), (70, 0), (10, '%.9f' % bx), (20, '%.9f' % by), (30, '0.0')) + ''.join(members) + ent('ENDBLK', (8, '0'))


def dxf(entities, blocks=(), layers=(('0', 0, 7),), insunits=4):
    out = pairs((0, 'SECTION'), (2, 'HEADER'), (9, '$INSUNITS'), (70, insunits), (0, 'ENDSEC'))
    out += pairs((0, 'SECTION'), (2, 'TABLES'), (0, 'TABLE'), (2, 'LAYER'))
    for n, fl, col in layers:
        out += ent('LAYER', (2, n), (70, fl), (62, col))
    out += pairs((0, 'ENDTAB'), (0, 'ENDSEC'))
    if blocks:
        out += pairs((0, 'SECTION'), (2, 'BLOCKS')) + ''.join(blocks) + pairs((0, 'ENDSEC'))
    out += pairs((0, 'SECTION'), (2, 'ENTITIES')) + ''.join(entities) + pairs((0, 'ENDSEC'), (0, 'EOF'))
    return out


def run_case(name, files, log=''):
    """Writes the files, runs the case's checker, returns (number of FAIL lines, printed text)."""
    with tempfile.TemporaryDirectory() as d:
        for fn, text in files.items():
            open(os.path.join(d, fn), 'w').write(text)
        script, chk = S.CASES[name]()
        R.failed = 0
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            chk(d, log)
        text = buf.getvalue()
        return sum(1 for ln in text.split('\n') if ln.startswith('FAIL')), text


def good_bad(name, good_files, mutants, log=''):
    n, text = run_case(name, good_files, log)
    report(n == 0, 'SELF_%s: the hand-authored good file is accepted (%d FAIL line(s)%s)' % (name, n, '' if n == 0 else ': ' + ' | '.join(x for x in text.split('\n') if x.startswith('FAIL'))[:300]))
    for label, files in mutants:
        n, _ = run_case(name, files, log)
        report(n > 0, 'SELF_%s: the checker rejects %s' % (name, label))


def main():
    # ---- fil: Fillet radius 10 at K = (50,30), u = (cos 30, sin 30), v = (-sin 30, cos 30)
    K, U, V, at = S.K, S.U, S.V, S.at
    c = (K[0] + 10 * (U[0] + V[0]), K[1] + 10 * (U[1] + V[1]))

    def fil_file(radius=10.0, a0=210.0, shift=0.0):
        p1, p2 = at(10, U), at(10, V)
        return dxf([line(p1[0] + shift, p1[1], at(40, U)[0], at(40, U)[1]), line(p2[0], p2[1], at(40, V)[0], at(40, V)[1]), arc(c[0], c[1], radius, a0, 300.0)])
    good_bad('fil', {'fl.dxf': fil_file()}, [('a radius-9 arc', {'fl.dxf': fil_file(radius=9.0)}), ('arc angles 180 to 300', {'fl.dxf': fil_file(a0=180.0)}), ('a tangent point moved by 1e-6', {'fl.dxf': fil_file(shift=1e-6)})])

    # ---- chm
    p1, p2 = at(10, U), at(10, V)

    def chm_file(d1=10.0):
        q1 = at(d1, U)
        return dxf([line(q1[0], q1[1], p2[0], p2[1]), line(q1[0], q1[1], at(40, U)[0], at(40, U)[1]), line(p2[0], p2[1], at(40, V)[0], at(40, V)[1])])
    good_bad('chm', {'ch.dxf': chm_file()}, [('a distance of 9 on the first line', {'ch.dxf': chm_file(9.0)})])

    # ---- ext
    t3, t4 = (30 + 8) / 1.4, (45 + 8) / 1.4

    def ext_file(h1x=20 + 5 / 0.4):
        return dxf([line(0, 5, h1x, 5), line(0, 8, 20 + 8 / 0.4, 8), line(0, 30, t3, 30 - t3), line(0, 45, t4, 45 - t4), line(20, 0, 80, 24)])
    good_bad('ext', {'ex.dxf': ext_file()}, [('an end point 0.01 short of the boundary', {'ex.dxf': ext_file(h1x=32.49)})])

    # ---- line
    c30, s30 = math.cos(math.radians(30)), math.sin(math.radians(30))
    c135, s135 = math.cos(math.radians(135)), math.sin(math.radians(135))

    def line_file(ex=10 + 100 * c30, lt='Cut'):
        e1 = ent('LINE', (8, '0'), (6, lt), (10, '10.0'), (20, '10.0'), (30, '0.0'), (11, '%.9f' % ex), (21, '%.9f' % (10 + 100 * s30)), (31, '0.0'))
        body = dxf([e1, line(0, 0, 50 * c135, 50 * s135), line(5, 5, 25, 5)])
        lt_rec = pairs((0, 'SECTION'), (2, 'TABLES'), (0, 'TABLE'), (2, 'LTYPE')) + ent('LTYPE', (2, 'Cut'), (70, 0), (3, 'Cut'), (72, 65), (73, 4), (40, '9.5'), (49, '6.0'), (74, 0), (49, '-1.5'), (74, 0), (49, '0.5'), (74, 0), (49, '-1.5'), (74, 0)) + pairs((0, 'ENDTAB'), (0, 'ENDSEC'))
        return body.replace(pairs((0, 'SECTION'), (2, 'ENTITIES')), lt_rec + pairs((0, 'SECTION'), (2, 'ENTITIES')))
    good_bad('line', {'ln.dxf': line_file()}, [('an end point moved by 1e-6', {'ln.dxf': line_file(ex=10 + 100 * c30 + 1e-6)}), ('a line style other than Cut', {'ln.dxf': line_file(lt='Dashed')})])

    # ---- align
    def align_file(x=2.5):
        return dxf([poly(S.rect_pts(2.5, 0, 12.5, 4)), line(2.5, 10, 11.5, 12), poly(S.rect_pts(2.5, 20, 20.5, 25)), line(x, 30, x + 2, 36), poly(S.rect_pts(2.5, 40, 10.5, 41.5))])
    good_bad('align', {'al.dxf': align_file()}, [('an object left of the others (min x 2.4)', {'al.dxf': align_file(x=2.4)})])

    # ---- comp
    mem = [ent('CIRCLE', (8, '0'), (10, '20.0'), (20, '10.0'), (30, '0.0'), (40, '4.0')), line(10, 10, 30, 10), line(30, 10, 30, 20)]

    def comp_files(bx=12.0, n2=1):
        f1 = dxf([insert('Chair', 12, 8)], blocks=[block('Chair', bx, 8, mem)])
        f2 = dxf([insert('Chair', 12, 8), insert('Chair', 50, 60)] + [insert('Chair', 70, 70)] * (n2 - 1), blocks=[block('Chair', bx, 8, mem)])
        return {'cm1.dxf': f1, 'cm2.dxf': f2}
    good_bad('comp', comp_files(), [('a stored base of (13,8)', comp_files(bx=13.0)), ('three instances after the second Insert', comp_files(n2=2))], "history: Block 'Chair' defined with 3 object(s)")

    # ---- tags
    def tags_files(extra=False):
        lay = (('0', 0, 7), ('Default', 0, 7), ('Walls', 0, 1), ('Doors', 0, 3))
        lay_off = (('0', 0, 7), ('Default', 0, 7), ('Walls', 0, -1), ('Doors', 0, 3))
        doors = [line(0, 2, 10, 2, 'Doors'), line(0, 3, 10, 3, 'Doors'), line(0, 4, 10, 4, 'Default')]
        walls = [line(0, 0, 10, 0, 'Walls'), line(0, 1, 10, 1, 'Walls'), line(0, 4, 10, 4, 'Default')]
        return {'tg1.dxf': dxf(doors + ([line(0, 0, 10, 0, 'Walls')] if extra else []), layers=lay_off), 'tg2.dxf': dxf(walls, layers=lay), 'tg3.dxf': dxf([line(0, 0, 10, 0, 'Walls'), line(0, 1, 10, 1, 'Walls')] + doors, layers=lay)}
    good_bad('tags', tags_files(), [('a hidden-tag line that is still exported', tags_files(extra=True))])

    # ---- uniq
    def uniq_file(py=20):
        return dxf([insert('Part', 0, 0), insert('Part', 30, 0), insert('PartU', 60, 0)],
                   blocks=[block('Part', 0, 0, [poly(S.rect_pts(0, 0, 10, 5))]), block('PartU', 0, 0, [poly(S.rect_pts(0, py, 10, py + 5))])])
    good_bad('uniq', {'uq.dxf': uniq_file()}, [('the edit in the wrong place (y 21)', {'uq.dxf': uniq_file(py=21)})])

    print('selftest: %d failure(s)' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
