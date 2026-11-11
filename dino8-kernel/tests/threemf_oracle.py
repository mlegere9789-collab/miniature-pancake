#!/usr/bin/env python3
"""Independent 3MF oracle for tests/extra/threemf_tests.cpp (Python stdlib only: zipfile + xml.etree).

  threemf_oracle.py fixture OUT.3mf      write the deflate-compressed fixture whose bytes are embedded in threemf_tests.cpp
  threemf_oracle.py hex OUT.3mf          write it and print it as a C array body (what is pasted into the test)
  threemf_oracle.py dump FILE.3mf        read any 3MF with zipfile + ElementTree and print what it holds (unit, objects, build)
  threemf_oracle.py check FILE.3mf EXPECT.json   dump + compare against an expected structure (exit 1 on any difference)

The fixture is a tetrahedron object, an assembly of two components of it (one rotated 90 degrees about z and moved, one mirrored
in x), and two build items, one scaled by 2 and lifted: it exercises units, components, row-vector transforms, a mirror (winding
flip) and deflate, none of which share code with the kernel under test.
"""
import json
import sys
import xml.etree.ElementTree as ET
import zipfile

NS = '{http://schemas.microsoft.com/3dmanufacturing/core/2015/02}'

TET_V = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)]
TET_T = [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)]  # outward oriented, volume 1/6


def fixture_model():
    v = ''.join('<vertex x="%s" y="%s" z="%s"/>' % p for p in TET_V)
    t = ''.join('<triangle v1="%d" v2="%d" v3="%d"/>' % p for p in TET_T)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<model unit="inch" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
        '<metadata name="Title">fixture</metadata>'
        '<resources>'
        '<object id="1" name="tet" type="model"><mesh><vertices>' + v + '</vertices><triangles>' + t + '</triangles></mesh></object>'
        '<object id="2" name="pair" type="model"><components>'
        '<component objectid="1" transform="0 1 0 -1 0 0 0 0 1 2 0 0"/>'
        '<component objectid="1" transform="-1 0 0 0 1 0 0 0 1 0 3 0"/>'
        '</components></object>'
        '</resources>'
        '<build>'
        '<item objectid="2" transform="2 0 0 0 2 0 0 0 2 0 0 10"/>'
        '<item objectid="1"/>'
        '</build></model>'
    )


def write_fixture(path):
    ct = ('<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', ct)
        z.writestr('_rels/.rels', rels)
        z.writestr('3D/3dmodel.model', fixture_model())


def dump(path):
    with zipfile.ZipFile(path) as z:
        bad = z.testzip()
        assert bad is None, 'corrupt entry ' + str(bad)
        names = z.namelist()
        rels = ET.fromstring(z.read('_rels/.rels'))
        target = [r.get('Target') for r in rels if r.get('Type', '').endswith('/3dmodel')][0].lstrip('/')
        model = ET.fromstring(z.read(target))
        methods = {i.filename: i.compress_type for i in z.infolist()}
    objects = {}
    for o in model.iter(NS + 'object'):
        d = {'name': o.get('name'), 'verts': [], 'tris': [], 'components': []}
        for vtx in o.iter(NS + 'vertex'):
            d['verts'].append([float(vtx.get(k)) for k in 'xyz'])
        for tri in o.iter(NS + 'triangle'):
            d['tris'].append([int(tri.get(k)) for k in ('v1', 'v2', 'v3')])
        for c in o.iter(NS + 'component'):
            d['components'].append([int(c.get('objectid')), c.get('transform')])
        objects[int(o.get('id'))] = d
    build = [[int(i.get('objectid')), i.get('transform')] for i in model.iter(NS + 'item')]
    return {'entries': names, 'methods': methods, 'model_part': target, 'unit': model.get('unit'), 'objects': objects, 'build': build}


def main(argv):
    cmd = argv[1]
    if cmd in ('fixture', 'hex'):
        write_fixture(argv[2])
        if cmd == 'hex':
            data = open(argv[2], 'rb').read()
            print(len(data))
            print(', '.join('0x%02x' % b for b in data))
    elif cmd == 'dump':
        print(json.dumps(dump(argv[2]), indent=1, sort_keys=True))
    elif cmd == 'check':
        got = dump(argv[2])
        want = json.load(open(argv[3]))
        ok = True
        for k, v in want.items():
            if k == 'objects':
                for oid, od in v.items():
                    g = got['objects'].get(int(oid))
                    if g is None:
                        print('missing object', oid); ok = False; continue
                    for kk, vv in od.items():
                        if kk == 'verts':
                            same = len(g['verts']) == len(vv) and all(abs(a - b) <= 1e-6 * max(1, abs(b)) for p, q in zip(g['verts'], vv) for a, b in zip(p, q))
                        else:
                            same = g[kk] == vv
                        if not same:
                            print('object', oid, kk, 'differs:', g[kk], 'vs', vv); ok = False
            elif got.get(k) != v:
                print(k, 'differs:', got.get(k), 'vs', v); ok = False
        print('OK' if ok else 'DIFFERENT')
        sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main(sys.argv)
