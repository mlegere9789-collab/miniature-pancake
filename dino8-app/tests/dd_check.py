#!/usr/bin/env python3
"""Independent checks for the drafting-docs app sections (tests/sections/94_dd*.sh). Python standard library only: it parses the DXF
files the app exported (group-code / value pairs, DXF specification) and compares them with values worked out by hand in the comments
next to each check. Prints "ok   ..." / "FAIL ..." lines, exit status 1 when any FAIL.

  dd_check.py tcount F1 F2 F3        TCOUNT      exports after the three numbering passes
  dd_check.py textalign F1 ... F8    TEXTALIGN   exports after each alignment step
  dd_check.py txt2mtxt F1 F2 F3      TXT2MTXT    exports of the TopDown, Selection and Combine=No runs
  dd_check.py dimchain FILE KIND     DIMCONTINUE / DIMBASELINE   (KIND = continue | baseline)
  dd_check.py dimspace F1 F2 F3 H    DIMSPACE    exports after Spacing=10, Spacing=Auto (H = the text height) and Spacing=0
  dd_check.py dimdis FILE            DIMDISASSOCIATE
  dd_check.py hatchback FILE         HATCHTOBACK the app output of dd_hatchback_script.txt (two GetUserText dumps, before and after the command)
  dd_check.py usertext FILE          prints "id key value" rows of a GetUserText listing read from FILE (helper for the shell)
"""
import math
import re
import sys

failed = 0


def check(cond, what):
    global failed
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failed += 1


def near(a, b, tol=1e-6):
    return a is not None and abs(a - b) <= tol


def entities(path):
    """The ENTITIES section of a DXF as [(type, {code: [values]})]."""
    lines = open(path, encoding='latin-1').read().split('\n')
    pairs = []
    for i in range(0, len(lines) - 1, 2):
        try:
            pairs.append((int(lines[i].strip()), lines[i + 1].strip()))
        except ValueError:
            pass
    out, in_ent, cur = [], False, None
    i = 0
    while i < len(pairs):
        c, v = pairs[i]
        if c == 0 and v == 'SECTION':
            in_ent = pairs[i + 1][1] == 'ENTITIES'
            cur = None
            i += 2
            continue
        if c == 0 and v in ('ENDSEC', 'EOF'):
            in_ent, cur = False, None
        elif c == 0 and in_ent:
            cur = (v, {})
            out.append(cur)
        elif cur is not None:
            cur[1].setdefault(c, []).append(v)
        i += 1
    return out


def f(rec, code):
    v = rec[1].get(code)
    return float(v[0]) if v else None


def texts(path):
    """TEXT entities as [(x, y, height, string)], sorted by (x, y)."""
    r = [(f(e, 10), f(e, 20), f(e, 40), e[1].get(1, [''])[0]) for e in entities(path) if e[0] == 'TEXT']
    return sorted(r, key=lambda t: (round(t[0], 6), round(t[1], 6)))


def mtexts(path):
    r = []
    for e in entities(path):
        if e[0] != 'MTEXT':
            continue
        body = ''.join(e[1].get(3, [])) + e[1].get(1, [''])[0]
        r.append((f(e, 10), f(e, 20), f(e, 40), int(f(e, 71) or 0), body))
    return r


def dims(path):
    """DIMENSION entities as [(dimline x, y, p13 (x, y), p14 (x, y))] in file order."""
    return [((f(e, 10), f(e, 20)), (f(e, 13), f(e, 23)), (f(e, 14), f(e, 24))) for e in entities(path) if e[0] == 'DIMENSION']


def has_text(ts, x, y, s, tol=1e-6):
    return any(near(t[0], x, tol) and near(t[1], y, tol) and t[3] == s for t in ts)


def tcount(f1, f2, f3):
    # Five single-line texts at (x, y): AA (40,0)  BB (10,5)  CC (30,10)  DD (0,15)  EE (20,20).
    # Pass 1: Order=X, Start=1, Increment=1, Prefix=P-, Mode=Overwrite. X ascending is DD(0) BB(10) EE(20) CC(30) AA(40) -> P-1..P-5.
    t = texts(f1)
    check(len(t) == 5, "DDTC_pass1: exactly 5 TEXT entities after numbering (old texts replaced, none duplicated): %d" % len(t))
    for x, y, s in [(0, 15, 'P-1'), (10, 5, 'P-2'), (20, 20, 'P-3'), (30, 10, 'P-4'), (40, 0, 'P-5')]:
        check(has_text(t, x, y, s), "DDTC_pass1: the text at (%g,%g) reads %s" % (x, y, s))
    # Pass 2 (a sixth text Z9 at (50,25) joins): Order=Y ascending, Mode=Replace Find=P-, Start=100, Increment=10.
    # Y ascending: P-5(y0) P-2(y5) P-4(y10) P-1(y15) P-3(y20) Z9(y25). "P-" is replaced by the label: 100,110,120,130,140 + the rest of the text.
    # Z9 has no "P-": it keeps its text and takes no number.
    t = texts(f2)
    check(len(t) == 6, "DDTC_pass2: 6 TEXT entities: %d" % len(t))
    for x, y, s in [(40, 0, '1005'), (10, 5, '1102'), (30, 10, '1204'), (0, 15, '1301'), (20, 20, '1403'), (50, 25, 'Z9')]:
        check(has_text(t, x, y, s), "DDTC_pass2: the text at (%g,%g) reads %s" % (x, y, s))
    # Pass 3: Mode=Prefix Prefix=N- Start=7 on Z9 alone -> "N-7Z9"; Mode=Suffix Suffix=. Start=3 on a new text Q at (60,30) -> "Q3."
    t = texts(f3)
    check(has_text(t, 50, 25, 'N-7Z9'), "DDTC_pass3: Mode=Prefix puts N-7 before Z9: N-7Z9")
    check(has_text(t, 60, 30, 'Q3.'), "DDTC_pass3: Mode=Suffix puts 3. after Q: Q3.")
    check(has_text(t, 40, 0, '1005'), "DDTC_pass3: the texts that were not selected keep their text")


def textalign(files):
    # Base B "Hi" at (10,10) height 4. T1 "Hi" at (30,25) height 4. T2 "Hi" at (-5,40) height 2. (Text widths are proportional to the
    # height: wB = 2 * w2, and T1 has the width of B.)
    def get(p, name):
        t = texts(p)
        return {'B': 10, 'T1': 30, 'T2': -5}[name], t
    def find(t, h):
        return [x for x in t if near(x[2], h)]
    def pos(p):
        t = texts(p)
        b = [x for x in t if near(x[2], 4) and near(x[0], 10) and (near(x[1], 10))]
        return t
    # Step 1 Left: every text gets B's left edge x = 10, y untouched (25 and 40).
    t = texts(files[0])
    check(len(t) == 3, "DDTA_left: 3 TEXT entities: %d" % len(t))
    check(has_text(t, 10, 10, 'Hi') and has_text(t, 10, 25, 'Hi') and has_text(t, 10, 40, 'Hi'), "DDTA_left: all three texts at x=10, y = 10, 25, 40 (only x moved)")
    # Step 2 Top: B's top is 10 + 4 = 14. T1 (height 4): y = 14 - 4 = 10. T2 (height 2): y = 14 - 2 = 12. x stays 10.
    t = texts(files[1])
    check(has_text(t, 10, 10, 'Hi') and sum(1 for x in t if near(x[1], 10)) == 2, "DDTA_top: T1 (height 4) top = 14 -> y = 10")
    check(any(near(x[0], 10) and near(x[1], 12) and near(x[2], 2) for x in t), "DDTA_top: T2 (height 2) top = 14 -> y = 12")
    # Step 3 Bottom: all baselines at B's y = 10. Step 4 Middle: B's middle = 12: T1 y = 12 - 2 = 10, T2 y = 12 - 1 = 11.
    t = texts(files[2])
    check(all(near(x[1], 10) for x in t) and len(t) == 3, "DDTA_bottom: all three baselines at y = 10")
    t = texts(files[3])
    check(any(near(x[2], 4) and near(x[1], 10) for x in t) and sum(1 for x in t if near(x[2], 4)) == 2, "DDTA_middle: both height-4 texts stay at y = 10 (middle 12)")
    check(any(near(x[2], 2) and near(x[1], 11) for x in t), "DDTA_middle: T2 (height 2) middle = 12 -> y = 11")
    # Step 5 Right and step 7 Center move T2 (x = 10 after Left) by (wB - w2) and (wB - w2) / 2; wB - w2 = wB / 2, so the Right shift is twice the Center shift
    # whatever the font's advance widths are. The height-4 text T1 has B's width: it must not move.
    r = [x for x in texts(files[4]) if near(x[2], 2)][0]
    c = [x for x in texts(files[6]) if near(x[2], 2)][0]
    shift_r, shift_c = r[0] - 10.0, c[0] - 10.0
    check(shift_r > 0.5, "DDTA_right: T2's right edge moved to B's: T2.x = 10 + %.4f" % shift_r)
    check(near(shift_r, 2.0 * shift_c, 1e-5), "DDTA_center: the Center shift %.5f is half the Right shift %.5f" % (shift_c, shift_r))
    check(all(near(x[0], 10) for x in texts(files[4]) if near(x[2], 4)) and all(near(x[0], 10) for x in texts(files[6]) if near(x[2], 4)),
          "DDTA_right: the height-4 texts (the width of B) stay at x = 10 for Right and Center")
    # Step 6 Left again restores T2.x = 10 (the Right shift is undone)
    check(any(near(x[2], 2) and near(x[0], 10) for x in texts(files[5])), "DDTA_left2: Left moves T2 back to x = 10")
    # Step 8 (picked base, not Base=First): done in the section with SelName; checked below as files[7]
    t = texts(files[7])
    check(all(near(x[0], 10) for x in t), "DDTA_pickbase: Left with the base picked at the second prompt puts all texts at x = 10")


def txt2mtxt(f1, f2, f3):
    # Texts "AAA" (0,0), "BBB" (0,20), "CCC" (0,10), all height 3.
    m = mtexts(f1)
    check(len(m) == 1, "DDTM_topdown: one MTEXT entity: %d" % len(m))
    if m:
        x, y, h, att, body = m[0]
        check(body == 'BBB\\PCCC\\PAAA', "DDTM_topdown: lines in top-down order BBB, CCC, AAA: %s" % body)
        check(near(x, 0) and near(y, 20 + 3), "DDTM_topdown: top-left corner = first line's baseline (0,20) + one text height (0,23): (%g,%g)" % (x, y))
        check(near(h, 3) and att == 1, "DDTM_topdown: text height 3, attachment 1 (top left): %g, %d" % (h, att))
    check(not texts(f1), "DDTM_topdown: the three single-line texts are gone")
    m = mtexts(f2)
    check(len(m) == 1 and m[0][4] == 'AAA\\PBBB\\PCCC', "DDTM_selection: Order=Selection keeps the order of selection AAA, BBB, CCC: %s" % (m[0][4] if m else None))
    if m:
        check(near(m[0][0], 0) and near(m[0][1], 0 + 3), "DDTM_selection: top-left = first selected text's baseline (0,0) + height: (%g,%g)" % (m[0][0], m[0][1]))
    m = mtexts(f3)
    check(len(m) == 3 and sorted(x[4] for x in m) == ['AAA', 'BBB', 'CCC'], "DDTM_separate: Combine=No gives one MTEXT per text: %s" % sorted(x[4] for x in m))
    check(all(near(x[2], 3) for x in m) and any(near(x[0], 0) and near(x[1], 23) for x in m), "DDTM_separate: each keeps its height; BBB's corner is (0,23)")


def dimchain(path, kind):
    d = dims(path)
    if kind == 'continue':
        # Dim (0,0)-(4,0) with the line at y=-5, then DimContinue to x=9 and x=15: each starts where the last ended, same dimension line y=-5.
        want = [((0, -5), (0, 0), (4, 0)), ((4, -5), (4, 0), (9, 0)), ((9, -5), (9, 0), (15, 0))]
        check(len(d) == 3, "DDCN_chain: 3 DIMENSION entities (base + 2 continued): %d" % len(d))
        for i, w in enumerate(want[:len(d)]):
            g = d[i]
            ok = near(g[0][1], w[0][1]) and near(g[1][0], w[1][0]) and near(g[1][1], w[1][1]) and near(g[2][0], w[2][0]) and near(g[2][1], w[2][1])
            check(ok, "DDCN_chain: dimension %d runs %s -> %s on the line y=%g (got %s -> %s, y=%s)" % (i + 1, w[1], w[2], w[0][1], g[1], g[2], g[0][1]))
        check(len(set(round(g[0][1], 6) for g in d)) == 1, "DDCN_collinear: all dimension lines have the same y")
        check([round(g[2][0] - g[1][0], 6) for g in d] == [4, 5, 6], "DDCN_values: the measured lengths are 4, 5, 6")
    else:
        # Base (0,0)-(4,0) line y=-5, DimBaseline Spacing=3 to x=9 and x=15: first extension line shared (0,0), lines at y=-8 and -11 (away from the points).
        # A second base (0,50)-(4,50) with its line ABOVE at y=56, default spacing 3.75, to x=10: line at 59.75.
        want = [((0, -5), (0, 0), (4, 0)), ((0, -8), (0, 0), (9, 0)), ((0, -11), (0, 0), (15, 0)), ((0, 56), (0, 50), (4, 50)), ((0, 59.75), (0, 50), (10, 50))]
        check(len(d) == 5, "DDBL_chain: 5 DIMENSION entities: %d" % len(d))
        for i, w in enumerate(want[:len(d)]):
            g = d[i]
            ok = near(g[0][1], w[0][1]) and near(g[1][0], w[1][0]) and near(g[1][1], w[1][1]) and near(g[2][0], w[2][0]) and near(g[2][1], w[2][1])
            check(ok, "DDBL_chain: dimension %d runs %s -> %s on the line y=%g (got %s -> %s, y=%s)" % (i + 1, w[1], w[2], w[0][1], g[1], g[2], g[0][1]))


def dimspace(f1, f2, f3, h):
    # Base D1 (0,0)-(4,0) at y=-5. Others: D2 (0,0)-(9,0) at y=-7, D3 (0,0)-(15,0) at y=-20, D4 (0,0)-(20,0) at y=+6.
    def line_y(path):
        r = {}
        for g in dims(path):
            r[round(g[2][0], 6)] = g[0][1]   # keyed by the x of the second measured point: 4, 9, 15, 20
        return r
    # Spacing=10: D2 (nearest below) -5-10=-15, D3 -5-20=-25, D4 (above the base) -5+10=5.
    y = line_y(f1)
    check(near(y.get(4), -5) and near(y.get(9), -15) and near(y.get(15), -25) and near(y.get(20), 5),
          "DDSP_10: base stays -5; the others at -15, -25 (below) and +5 (above): %s" % y)
    # Auto: twice the text height h each: D2 -5-2h, D3 -5-4h, D4 -5+2h.
    y = line_y(f2)
    check(near(y.get(4), -5) and near(y.get(9), -5 - 2 * h) and near(y.get(15), -5 - 4 * h) and near(y.get(20), -5 + 2 * h),
          "DDSP_auto: spacing = 2 x text height (%g): %s" % (h, y))
    # Spacing=0: all on the base's line.
    y = line_y(f3)
    check(all(near(v, -5) for v in y.values()) and len(y) == 4, "DDSP_0: Spacing=0 aligns every dimension line with the base at y=-5: %s" % y)
    # the measured points are unchanged
    d = dims(f3)
    check(all(near(g[1][0], 0) and near(g[1][1], 0) for g in d) and sorted(g[2][0] for g in d) == [4, 9, 15, 20], "DDSP_points: the measured points are unchanged")


def dimdis(path):
    # L1 (0,0)-(10,0) with an associative Dim; L2 (0,20)-(10,20) with a Dim made associative and then DimDisassociate'd. Both lines are scaled x2 about the
    # origin: L1 -> (0,0)-(20,0), L2 -> (0,40)-(20,40). UpdateDimensions follows L1 only.
    d = dims(path)
    check(len(d) == 2, "DDDA_count: 2 DIMENSION entities: %d" % len(d))
    a = [g for g in d if near(g[1][1], 0)]
    b = [g for g in d if near(g[1][1], 20) or near(g[1][1], 40)]
    check(len(a) == 1 and near(a[0][2][0], 20), "DDDA_assoc: the associative dimension followed its line to x = 20: %s" % a)
    check(len(b) == 1 and near(b[0][1][1], 20) and near(b[0][2][0], 10) and near(b[0][2][1], 20), "DDDA_free: the disassociated dimension kept (0,20)-(10,20): %s" % b)


def user_text_dumps(path):
    """[(before, after)]: {id: {key: value}} of the GetUserText lines printed before / after the line 'Command: HatchToBack'."""
    before, after, cur = {}, {}, None
    cur = before
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        if re.match(r'^(history: )?Command: HatchToBack', line):
            cur = after
            continue
        m = re.match(r'^(?:history: )?(\d+): (\S+) = (.*)$', line)
        if m:
            cur.setdefault(m.group(1), {})[m.group(2)] = m.group(3)
    return before, after


def hatchback(path):
    # Two solid hatches (Hatch=Solid), their two boundary rectangles, a circle sent to the back before (DrawOrder -1) and a line (no DrawOrder).
    # HatchToBack must give the hatches a draw order below every other object: the lowest other draw order is -1 (circle) -> hatches get -2; the others keep theirs.
    before, after = user_text_dumps(path)
    hat = [i for i, t in after.items() if t.get('Hatch') == 'Solid']
    circ = [i for i, t in after.items() if t.get('Role') == 'Circle']
    line = [i for i, t in after.items() if t.get('Role') == 'Line']
    check(len(hat) == 2 and len(circ) == 1 and len(line) == 1, "DDHB_objects: 2 hatch objects, 1 circle, 1 line listed: %d %d %d" % (len(hat), len(circ), len(line)))
    check(all('DrawOrder' not in before.get(i, {}) for i in hat), "DDHB_before: the hatches had no draw order before the command")
    check(before.get(circ[0], {}).get('DrawOrder') == '-1' if circ else False, "DDHB_before: the circle was sent to the back: draw order -1")
    check(all(after[i].get('DrawOrder') == '-2' for i in hat), "DDHB_after: both hatches have draw order -2, behind the circle at -1: %s" % [after[i].get('DrawOrder') for i in hat])
    check(circ and after[circ[0]].get('DrawOrder') == '-1', "DDHB_after: the circle keeps -1")
    check(line and 'DrawOrder' not in after[line[0]], "DDHB_after: the line still has no draw order")
    others = [i for i in after if i not in hat]
    check(all('DrawOrder' not in after[i] or after[i]['DrawOrder'] == '-1' for i in others), "DDHB_after: no other object was given a draw order")


def usertext(path):
    for line in open(path, encoding='latin-1'):
        m = re.match(r'^(?:history: )?(\d+): (\S+) = (.*)$', line.rstrip('\n'))
        if m:
            print(m.group(1), m.group(2), m.group(3))


if __name__ == '__main__':
    mode, args = sys.argv[1], sys.argv[2:]
    if mode == 'tcount':
        tcount(*args)
    elif mode == 'textalign':
        textalign(args)
    elif mode == 'txt2mtxt':
        txt2mtxt(*args)
    elif mode == 'dimchain':
        dimchain(*args)
    elif mode == 'dimspace':
        dimspace(args[0], args[1], args[2], float(args[3]))
    elif mode == 'dimdis':
        dimdis(args[0])
    elif mode == 'hatchback':
        hatchback(args[0])
    elif mode == 'usertext':
        usertext(args[0])
    else:
        sys.exit('unknown mode ' + mode)
    sys.exit(1 if failed else 0)
