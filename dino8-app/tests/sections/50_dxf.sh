# DXFU_: DXF $INSUNITS on export (every document unit) and unit conversion on import.
#
# Before this change ExportDxf wrote $INSUNITS only for mm/in/ft/cm/m (a kilometre, mil, micron, microinch or
# mile document was declared as millimetres, "None" as 4), and ImportDxf ignored $INSUNITS entirely, so an inch
# drawing imported into a millimetre document came in 25.4 times too small.
#
# Oracles are independent of the app: published unit definitions (1 in = 25.4 mm, 1 ft = 304.8 mm, 1 mi =
# 1609.344 m, 1 mil = 0.0254 mm, 1 microinch = 25.4e-6 mm, AutoCAD's $INSUNITS code table) applied to
# geometry whose length is known in closed form (line 10, circle 2*pi*5, semicircle by bulge 1 over a chord
# of 2 = pi, a x3-scaled INSERT of a unit line = 3). Negative controls: $INSUNITS 0 / absent / out of range,
# and a file in the document's own unit, must come in untouched. Run on its own with:
#   tests/run_sections.sh -b <Dino8> dxf
dxfu_dir="$TMPW/dxfu"
rm -rf "$dxfu_dir"; mkdir -p "$dxfu_dir"
dxfu_run() {
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 120 --script "$1" 2>&1 || true
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$1" 2>&1 || true
  fi
}
python3 -I - gen "$dxfu_dir" <<'PY'
import json, math, os, shutil, sys
d = sys.argv[2]
def w(name, text):
    with open(os.path.join(d, name), 'w') as f: f.write(text)
def pairs(*kv): return ''.join('%s\n%s\n' % (str(k), str(v)) for k, v in kv)
def header(insunits):
    if insunits is None: return pairs((0, 'SECTION'), (2, 'HEADER'), (9, '$ACADVER'), (1, 'AC1015'), (0, 'ENDSEC'))
    return pairs((0, 'SECTION'), (2, 'HEADER'), (9, '$ACADVER'), (1, 'AC1015'), (9, '$INSUNITS'), (70, insunits), (0, 'ENDSEC'))
def line(x0, y0, x1, y1): return pairs((0, 'LINE'), (8, '0'), (10, x0), (20, y0), (30, 0), (11, x1), (21, y1), (31, 0))
def circle(r): return pairs((0, 'CIRCLE'), (8, '0'), (10, 0), (20, 20), (30, 0), (40, r))
def bulge_pl():  # one semicircle: chord (0,40)-(2,40), bulge 1
    return pairs((0, 'LWPOLYLINE'), (8, '0'), (90, 2), (70, 0), (10, 0), (20, 40), (42, 1), (10, 2), (20, 40), (42, 0))
def insert(): return pairs((0, 'INSERT'), (8, '0'), (2, 'UB'), (10, 0), (20, 60), (30, 0), (41, 3), (42, 1), (43, 1))
def blocks():
    return pairs((0, 'SECTION'), (2, 'BLOCKS'), (0, 'BLOCK'), (8, '0'), (2, 'UB'), (70, 0), (10, 0), (20, 0), (30, 0), (3, 'UB')) + \
           line(0, 0, 1, 0) + pairs((0, 'ENDBLK'), (8, '0'), (0, 'ENDSEC'))
def dxf(insunits, ents, with_blocks=False):
    return header(insunits) + (blocks() if with_blocks else '') + pairs((0, 'SECTION'), (2, 'ENTITIES')) + ''.join(ents) + pairs((0, 'ENDSEC'), (0, 'EOF'))

MM = {'Inches': 25.4, 'Feet': 304.8, 'Miles': 1609344.0, 'Millimeters': 1.0, 'Centimeters': 10.0, 'Meters': 1000.0,
      'Kilometers': 1e6, 'Microinches': 25.4e-6, 'Mils': 0.0254, 'Microns': 1e-3}
CODE = {'Inches': 1, 'Feet': 2, 'Miles': 3, 'Millimeters': 4, 'Centimeters': 5, 'Meters': 6, 'Kilometers': 7,
        'Microinches': 8, 'Mils': 9, 'Microns': 13, 'None': 0}
METRES = {1: .0254, 2: .3048, 3: 1609.344, 4: .001, 5: .01, 6: 1.0, 7: 1000.0, 8: 2.54e-8, 9: 2.54e-5, 10: .9144,
          11: 1e-10, 12: 1e-9, 13: 1e-6, 14: .1, 15: 10.0, 16: 100.0}
UNITS = ['Inches', 'Feet', 'Miles', 'Millimeters', 'Centimeters', 'Meters', 'Kilometers', 'Microinches', 'Mils', 'Microns', 'None']
for u in UNITS: w('set_%s.lua' % u, 'rs.UnitSystem("%s")\n' % u)
w('set_Millimeters.lua', 'rs.UnitSystem("Millimeters")\n')

script, expect = [], []   # expect: (name, command-marker, value, unit)
def add_len(name, marker, value, unit): expect.append(dict(name=name, marker=marker, value=value, unit=unit))
def length(): script.extend(['SelAll', 'Length'])
def do_import(name, src_text, doc_unit='Millimeters', exp=None):
    p = os.path.join(d, name + '.dxf')
    if src_text is not None: w(name + '.dxf', src_text)   # else the app exported it in phase A
    script.append('New')
    if doc_unit != 'Millimeters': script.append('RunScript %s/set_%s.lua' % (d, doc_unit))
    script.append('Import %s' % p)
    length()
    if exp is not None: add_len(name, '/' + name + '.dxf', exp, doc_unit)

# --- A: documents in every unit export a line (10) + circle (r 5); header declares the unit -------------------
L = 10 + 2 * math.pi * 5
# B (below) imports these files; each import needs its own file name so its output line can be told apart.
RT = [('rt_%s_into_mm' % u, u, 'Millimeters', L * MM[u]) for u in UNITS if u != 'None']
RT += [('rt_None_into_mm', 'None', 'Millimeters', L),                                    # unitless file: untouched
       ('rt_Inches_into_Inches', 'Inches', 'Inches', L),                                 # same unit: untouched
       ('rt_Meters_into_Inches', 'Meters', 'Inches', L * 1000.0 / 25.4),
       ('rt_Kilometers_into_Feet', 'Kilometers', 'Feet', L * 1e6 / 304.8),
       ('rt_Millimeters_into_Mils', 'Millimeters', 'Mils', L / 0.0254),
       ('rt_Inches_into_None', 'Inches', 'None', L)]                                      # unitless document: untouched
for u in UNITS:
    script += ['New', 'RunScript %s/set_%s.lua' % (d, u), 'Line 0,0,0 10,0,0', 'Circle 0,0,0 5', 'SelAll', 'Export %s/exp_%s.dxf' % (d, u)]
    for name, src, _, _ in RT:
        if src == u: script.append('Export %s/%s.dxf' % (d, name))
# --- B: round trip: the exported files imported into documents of various units ---------------------------------
for name, src, doc_unit, want in RT: do_import(name, None, doc_unit, want)
# --- C: hand-written files, every entity kind, against hand-computed lengths ----------------------------------
ents = {'line': (line(0, 0, 10, 0), 10.0), 'circle': (circle(5), 2 * math.pi * 5), 'bulge': (bulge_pl(), math.pi)}
for k, (e, ln) in ents.items():
    do_import('hand_%s_in' % k, dxf(1, [e]), 'Millimeters', ln * 25.4)
do_import('hand_insert_in', dxf(1, [insert()], True), 'Millimeters', 3 * 25.4)       # block member (1 unit) x3 scale, in inches
do_import('hand_all_in', dxf(1, [e for e, _ in ents.values()] + [insert()], True), 'Millimeters', (10 + 2 * math.pi * 5 + math.pi + 3) * 25.4)
# block definition scaled too: delete the placed instance, insert a fresh one from the definition (1 inch long)
script += ['SelAll', 'Delete', 'Insert UB 0,0,0', 'Enter', 'SelAll', 'Length']
add_len('block_definition_scaled', 'UB 0,0,0', 25.4, 'Millimeters')
# every documented code 1..16, a line sized to give a measurable length
for code, m in METRES.items():
    lf = 10.0
    while lf * m * 1000.0 < 10.0: lf *= 10.0
    do_import('hand_code_%d' % code, dxf(code, [line(0, 0, lf, 0)]), 'Millimeters', lf * m * 1000.0)
# negative controls: unitless (0), absent, out of range, already millimetres
for nm, code in (('code_0', 0), ('absent', None), ('code_99', 99), ('code_21', 21), ('code_4_mm', 4)):
    do_import('neg_' + nm, dxf(code, [line(0, 0, 10, 0), circle(5)]), 'Millimeters', 10 + 2 * math.pi * 5)
do_import('neg_code_1_into_None', dxf(1, [line(0, 0, 10, 0)]), 'None', 10.0)

w('imp.txt', '\n'.join(script) + '\n')
json.dump(dict(expect=expect, codes=CODE, units=UNITS), open(os.path.join(d, 'manifest.json'), 'w'))
PY
dxfu_out="$(dxfu_run "$dxfu_dir/imp.txt")"
printf '%s\n' "$dxfu_out" > "$dxfu_dir/app_output.txt"
python3 -I - check "$dxfu_dir" > "$dxfu_dir/verdict.txt" <<'PY' || true
import json, os, re, sys
d = sys.argv[2]
m = json.load(open(os.path.join(d, 'manifest.json')))
out = open(os.path.join(d, 'app_output.txt'), errors='replace').read().split('\n')
bad = 0
def ok(n): print('ok   DXFU_' + n)
def no(n, why=''):
    global bad; bad += 1; print('FAIL DXFU_%s %s' % (n, why))
# export side: $INSUNITS in each exported header
for u, code in m['codes'].items():
    p = os.path.join(d, 'exp_%s.dxf' % u)
    got = None
    if os.path.exists(p):
        g = [l.strip() for l in open(p, errors='replace').read().split('\n')]
        for i in range(len(g) - 3):
            if g[i] == '9' and g[i + 1] == '$INSUNITS' and g[i + 2] == '70': got = g[i + 3]; break
    if got == str(code): ok('export_%s_document_declares_INSUNITS_%d' % (u, code))
    else: no('export_%s_document_declares_INSUNITS_%d' % (u, code), '(got %s)' % got)
# import side: each Length line belongs to the nearest preceding "Command: Import <path>" / "Command: Insert ..."
res, marker = {}, None
for l in out:
    mm = re.search(r'Command: (Import|Insert) (\S.*)$', l)
    if mm: marker = mm.group(2).strip(); continue
    mt = re.search(r'Total length = (\S+) (\S+)\s*$', l)
    if mt and marker is not None: res[marker] = (float(mt.group(1)), mt.group(2)); marker = None
for e in m['expect']:
    key = [k for k in res if k.endswith(e['marker'])] if e['marker'].startswith('/') else [e['marker']] if e['marker'] in res else []
    if not key: no(e['name'], '(no Total length printed for %s)' % e['marker']); continue
    v, unit = res[key[0]]
    want = e['value']
    if abs(v - want) <= 1e-3 * abs(want) and unit == e['unit']: ok('%s_%.6g_%s' % (e['name'], want, e['unit']))
    else: no(e['name'], '(got %g %s, want %.6g %s)' % (v, unit, want, e['unit']))
# the import summary names the conversion
for want, nm in (('units INSUNITS=1 -> document Millimeters, scaled x25.4', 'summary_reports_inch_factor_25.4'),
                 ('units INSUNITS=6 -> document Inches, scaled x39.37', 'summary_reports_metre_to_inch_factor'),
                 ('units INSUNITS=7 -> document Feet, scaled x3280.8', 'summary_reports_km_to_feet_factor')):
    if any(want in l for l in out): ok(nm)
    else: no(nm, '(missing "%s")' % want)
if not any('INSUNITS=0 ' in l or 'INSUNITS=99' in l or 'INSUNITS=21' in l for l in out): ok('summary_names_no_conversion_for_unitless_absent_or_invalid_codes')
else: no('summary_names_no_conversion_for_unitless_absent_or_invalid_codes')
sys.exit(1 if bad else 0)
PY
cat "$dxfu_dir/verdict.txt"
if grep -q '^FAIL' "$dxfu_dir/verdict.txt" || ! grep -q '^ok' "$dxfu_dir/verdict.txt"; then
  fail=1
  echo "--- dxfu app output tail"; grep -E "Command: (Import|Insert)|Total length|DXF:|Warn|!" "$dxfu_dir/app_output.txt" | tail -n 40 | cut -c1-220
fi
