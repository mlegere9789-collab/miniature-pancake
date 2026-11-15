#!/usr/bin/env python3
"""Fuzzy-match Register rows to existing Dino 8 commands and kernel symbols.

HINTS, NOT EVIDENCE. The output only suggests where to look. A row moves to
`verified` through a named test (acceptance[].test_id plus recorded evidence),
never through this mapping. Every output line carries the label.

Usage:
  map_rows.py REGISTER_DIR [--catalog catalog.jsonl] [--kernel kernel_api.jsonl]
              [--out candidates.jsonl] [--min-score 0.6] [--top 5]

REGISTER_DIR is searched recursively for *.jsonl files (row schema:
schema/row.schema.json). Matching uses, per row, the quoted titles in
`statement`, and the last segments of `source.ref`; each is compared with
command names + aliases and kernel symbols by exact normalised match (score
1.0) or a blend of character-trigram Dice and CamelCase-token overlap.
"""
import argparse, glob, json, os, re, sys
from collections import defaultdict, Counter

LABEL = ("HINTS, NOT EVIDENCE: fuzzy name match only. A row becomes verified "
         "only through a named test with recorded evidence, never through this file.")


def norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def tokens(s):
    s = re.sub(r'([a-z0-9])([A-Z])', r'\1 \2', s)
    s = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1 \2', s)
    return [t for t in re.split(r'[^A-Za-z0-9]+', s.lower()) if t]


def trigrams(n):
    p = '^' + n + '$'
    return {p[i:i + 3] for i in range(len(p) - 2)} if len(p) >= 3 else {p}


STOP = {'the', 'a', 'an', 'of', 'to', 'in', 'on', 'for', 'and', 'or', 'with', 'by', 'from', 'as', 'user',
        'can', 'bpy', 'ops', 'types', 'props', 'api', 'node', 'nodes', 'operator', 'object', 'data', 'set',
        'get', 'new', 'add', 'all', 'type'}


def queries_for(row):
    """(text, weight) pairs describing the row."""
    qs = []
    st = row.get('statement', '') or ''
    for m in re.finditer(r'["“]([^"”]{3,80})["”]', st):
        qs.append((m.group(1), 1.0, 'statement-title'))
    ref = (row.get('source') or {}).get('ref', '') or ''
    segs = [x for x in re.split(r'[.|/>]+|\s+-\s+', ref) if x.strip()]
    if segs:
        qs.append((segs[-1].strip(), 1.0, 'source.ref'))
        if len(segs) >= 2:
            qs.append((segs[-2].strip() + ' ' + segs[-1].strip(), 0.9, 'source.ref'))
    if ref:
        qs.append((ref, 0.8, 'source.ref-full'))
    seen, out = set(), []
    for q, w, src in qs:
        k = norm(q)
        if len(k) >= 3 and k not in seen:
            seen.add(k)
            out.append((q, w, src))
    return out


class Index:
    def __init__(self):
        self.items = []  # (kind, display, norm, tokset, trigs, extra)
        self.tri = defaultdict(list)

    def add(self, kind, display, key_text, extra=None):
        n = norm(key_text)
        if len(n) < 3:
            return
        toks = {t for t in tokens(key_text) if t not in STOP}
        tg = trigrams(n)
        i = len(self.items)
        self.items.append((kind, display, n, toks, tg, extra or {}))
        for g in tg:
            self.tri[g].append(i)

    def match(self, q, weight, min_score):
        qn = norm(q)
        qt = {t for t in tokens(q) if t not in STOP}
        qg = trigrams(qn)
        hits = Counter()
        for g in qg:
            for i in self.tri.get(g, ()):
                hits[i] += 1
        out = []
        for i, c in hits.items():
            kind, disp, n, toks, tg, extra = self.items[i]
            if qn == n:
                s = 1.0
            else:
                inter = len(qg & tg)
                dice = 2.0 * inter / (len(qg) + len(tg))
                ov = 0.0
                if toks and qt:
                    ov = len(toks & qt) / max(len(toks), len(qt))
                s = 0.7 * dice + 0.3 * ov
                # a long query that merely contains a short name is weak evidence
                if len(n) < 5 and qn != n:
                    s *= 0.7
                s = min(s, 0.99)
            s *= weight
            if s >= min_score:
                out.append((s, kind, disp, extra))
        return out


def load_jsonl(p):
    with open(p, encoding='utf-8') as fh:
        return [json.loads(l) for l in fh if l.strip()]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument('register_dir')
    ap.add_argument('--catalog', default=os.path.join(here, 'catalog.jsonl'))
    ap.add_argument('--kernel', default=os.path.join(here, 'kernel_api.jsonl'))
    ap.add_argument('--out', default=os.path.join(here, 'candidates.jsonl'))
    ap.add_argument('--min-score', type=float, default=0.6)
    ap.add_argument('--top', type=int, default=5)
    a = ap.parse_args()

    idx = Index()
    for c in load_jsonl(a.catalog):
        ex = {'status': c.get('status'), 'impl_file': c.get('impl_file'), 'impl_line': c.get('impl_line')}
        idx.add('command', c['name'], c['name'], ex)
        for al in c.get('aliases', []):
            if len(al) >= 4:  # 2-3 letter aliases ("a", "bd") are noise for fuzzy matching
                idx.add('command', c['name'], al, ex)
    for k in load_jsonl(a.kernel):
        if k['kind'] in ('constructor', 'destructor', 'operator') or k.get('internal'):
            continue
        parts = k['symbol'].split('::')
        text = ' '.join(parts[-2:]) if len(parts) >= 2 and k['kind'] in ('method', 'static_method') else parts[-1]
        idx.add('kernel', k['symbol'], text, {'header': k['header'], 'line': k['line'], 'sym_kind': k['kind']})

    files = sorted(glob.glob(os.path.join(a.register_dir, '**', '*.jsonl'), recursive=True))
    rows = []
    for f in files:
        for l in open(f, encoding='utf-8'):
            if l.strip():
                try:
                    r = json.loads(l)
                except ValueError:
                    continue
                if isinstance(r, dict) and 'id' in r:
                    r['_file'] = os.path.relpath(f, a.register_dir)
                    rows.append(r)

    n_hint = n_cmd = n_ker = n_exact = 0
    with open(a.out, 'w', encoding='utf-8') as fh:
        for r in rows:
            best = {}
            for q, w, src in queries_for(r):
                for s, kind, disp, extra in idx.match(q, w, a.min_score):
                    key = (kind, disp)
                    if key not in best or s > best[key]['score']:
                        d = {'kind': kind, 'name': disp, 'score': round(s, 3), 'matched_on': q, 'via': src}
                        if kind == 'command':
                            d['registered_status'] = extra.get('status')
                            d['impl'] = (extra.get('impl_file') or '') + (':%s' % extra['impl_line'] if extra.get('impl_line') else '')
                        else:
                            d['header'] = '%s:%s' % (extra['header'], extra['line'])
                        best[key] = d
            cands = sorted(best.values(), key=lambda d: (-d['score'], d['kind'], d['name']))
            # top N per kind
            out, per = [], Counter()
            for d in cands:
                if per[d['kind']] < a.top:
                    out.append(d)
                    per[d['kind']] += 1
            if out:
                n_hint += 1
                n_cmd += any(d['kind'] == 'command' for d in out)
                n_ker += any(d['kind'] == 'kernel' for d in out)
                n_exact += any(d['score'] >= 0.999 for d in out)
            fh.write(json.dumps({'row_id': r['id'], 'register_file': r['_file'], 'label': 'hints, not evidence',
                                 'candidates': out}) + '\n')
    print(json.dumps({'label': LABEL, 'rows': len(rows), 'rows_with_any_hint': n_hint,
                      'rows_with_command_hint': n_cmd, 'rows_with_kernel_hint': n_ker,
                      'rows_with_exact_name_hint': n_exact, 'min_score': a.min_score}, indent=2))


if __name__ == '__main__':
    main()
