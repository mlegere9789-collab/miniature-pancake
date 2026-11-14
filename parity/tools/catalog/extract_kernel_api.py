#!/usr/bin/env python3
"""Extract the public C++ API declared in dino8-kernel headers.

Scans dino8-kernel/include/**/*.h and dino8-kernel/src/**/*.h (tests/ is
skipped). Comments, string literals and preprocessor lines are blanked, then a
small brace/scope tracker walks each header and records:

  kind = function        free function in a namespace
         method          non-static member function of a class/struct
         static_method   static member function
         constructor | destructor | operator
         type            class / struct / enum / union declaration

Only public members count (struct default public, class default private,
public:/protected:/private: respected). Functions inside `namespace detail`
or an unnamed namespace are tagged internal=true, and src/ headers get
internal=true as well, but are still listed.

Output kernel_api.jsonl: {symbol, header, line, kind, name, scope, internal}
`symbol` is namespace-qualified (dino8::kernel::Mesh::Tessellate); overloads
share a symbol and appear as separate rows (distinguished by `line`).

Usage: extract_kernel_api.py [--repo DIR] [--out kernel_api.jsonl]
"""
import argparse, bisect, json, os, re, sys
from collections import Counter

KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'alignas', 'alignof',
            'decltype', 'static_assert', 'noexcept', 'catch', 'defined', 'typeid',
            'throw', 'requires', 'template', 'new', 'delete', 'case', 'using',
            'typedef', 'friend'}


def blank(src):
    """Blank comments, string/char literals and preprocessor lines, keeping
    offsets and newlines identical."""
    out = list(src)
    n = len(src)
    i = 0
    at_line_start = True
    while i < n:
        c = src[i]
        if c == '\n':
            at_line_start = True
            i += 1
            continue
        if at_line_start and c in ' \t':
            i += 1
            continue
        if at_line_start and c == '#':
            j = i
            while j < n:
                if src[j] == '\n' and (j == i or src[j - 1] != '\\'):
                    break
                j += 1
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
            continue
        at_line_start = False
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif c == '"':
            j = i + 1
            while j < n and src[j] != '"' and src[j] != '\n':
                if src[j] == '\\':
                    j += 1
                j += 1
            for k in range(i + 1, min(j, n)):
                if out[k] != '\n':
                    out[k] = ' '
            i = j + 1
        elif c == "'" and not (i > 0 and src[i - 1].isalnum()):
            j = i + 1
            while j < n and src[j] != "'" and src[j] != '\n':
                if src[j] == '\\':
                    j += 1
                j += 1
            for k in range(i + 1, min(j, n)):
                out[k] = ' '
            i = j + 1
        else:
            i += 1
    return ''.join(out)


def match_close(t, i):
    pairs = {'(': ')', '{': '}', '[': ']'}
    stack = [pairs[t[i]]]
    j = i + 1
    while j < len(t) and stack:
        c = t[j]
        if c in pairs:
            stack.append(pairs[c])
        elif c == stack[-1]:
            stack.pop()
        j += 1
    return j - 1


NAME_RE = re.compile(r'((?:[A-Za-z_]\w*(?:\s*<[^<>()]*>)?\s*::\s*)*(?:~\s*)?(?:operator\s*(?:\(\s*\)|\[\s*\]|"[^"]*"|[^\s\w(]+|(?:new|delete)(?:\s*\[\s*\])?|[A-Za-z_][\w:<>\s*&]*?)|[A-Za-z_]\w*))\s*\(')


def classify(stmt, scope_kind, scope_name):
    """Given a declaration statement (text up to ; or {), return (name, kind)
    if it declares a function, else None."""
    s = ' '.join(stmt.split())
    if not s or '(' not in s:
        return None
    first = s.split(' ', 1)[0]
    if first in ('typedef', 'using', 'friend', 'static_assert', 'return', 'namespace', 'extern'):
        if not (first == 'extern' and '(' in s and '"C"' not in s):
            return None
    # drop template<...> prefix
    s2 = re.sub(r'^template\s*<[^;{}]*?>\s*', '', s)
    # attributes / macros before the declaration are left in place
    for m in NAME_RE.finditer(s2):
        nm = re.sub(r'\s+', ' ', m.group(1)).strip()
        base = nm.split('::')[-1].strip()
        if base in KEYWORDS:
            continue
        # skip things inside a default argument or function-pointer typedef: require
        # that the match not sit inside an earlier unmatched '('
        if s2[:m.start()].count('(') != s2[:m.start()].count(')'):
            continue
        # "Type name(args)" variable with ctor init in header: has '=' before '('
        if '=' in s2[:m.start()] and 'operator' not in s2[:m.start()]:
            return None
        prefix = s2[:m.start()]
        if base.startswith('~'):
            kind = 'destructor'
        elif base.startswith('operator'):
            kind = 'operator'
        elif scope_kind == 'class' and base == scope_name and not re.search(r'\b(?:\w+)\s*$', prefix.replace('explicit', '').replace('inline', '').replace('constexpr', '').replace('virtual', '')):
            kind = 'constructor'
        elif scope_kind == 'class':
            kind = 'static_method' if re.search(r'\bstatic\b', prefix) else 'method'
        else:
            kind = 'function'
        return nm.replace(' ', ''), kind
    return None


def parse_header(path, rel, internal_hdr):
    raw = open(path, encoding='utf-8', errors='replace').read()
    t = blank(raw)
    starts = [0] + [m.end() for m in re.finditer('\n', t)]
    out = []
    # scope stack entries: dict(kind, name, access, internal)
    stack = [{'kind': 'file', 'name': '', 'access': 'public', 'internal': internal_hdr}]
    i, n = 0, len(t)
    stmt_start = 0

    def ns_path():
        return [s['name'] for s in stack if s['kind'] in ('namespace', 'class') and s['name']]

    def emit(pos, name, kind, stmt_pos):
        sc = stack[-1]
        if sc['kind'] == 'class' and sc['access'] != 'public':
            return
        if sc['kind'] not in ('file', 'namespace', 'class'):
            return
        scope = '::'.join(x for x in ns_path() if x != '<anon>')
        sym = name if '::' in name and not scope else (scope + '::' + name if scope else name)
        base = name.split('::')[-1]
        internal = any(s['internal'] for s in stack)
        out.append({'symbol': sym, 'header': rel, 'line': bisect.bisect_right(starts, stmt_pos),
                    'kind': kind, 'name': base, 'scope': scope, 'internal': bool(internal)})

    def first_nonspace(p):
        while p < n and t[p].isspace():
            p += 1
        return p

    while i < n:
        c = t[i]
        if c == ';':
            stmt = t[stmt_start:i]
            r = classify(stmt, stack[-1]['kind'], stack[-1]['name'])
            if r and stack[-1]['kind'] in ('file', 'namespace', 'class'):
                emit(i, r[0], r[1], first_nonspace(stmt_start))
            elif re.match(r'\s*(?:enum\b|class\b|struct\b|union\b)', stmt) and '(' not in stmt:
                m = re.match(r'\s*(?:enum\s+(?:class|struct)\s+|enum\s+|class\s+|struct\s+|union\s+)(?:\[\[[^\]]*\]\]\s*)?([A-Za-z_]\w*)', stmt)
                # forward declarations only: skip (the definition is recorded)
            stmt_start = i + 1
            i += 1
        elif c == ':' and i + 1 < n and t[i + 1] != ':' and (i == 0 or t[i - 1] != ':'):
            w = t[stmt_start:i].strip()
            if w in ('public', 'private', 'protected') and stack[-1]['kind'] == 'class':
                stack[-1]['access'] = w
                stmt_start = i + 1
            i += 1
        elif c == '{':
            stmt = t[stmt_start:i]
            sflat = ' '.join(stmt.split())
            sflat_nt = re.sub(r'^template\s*<[^;{}]*?>\s*', '', sflat)
            nsm = re.match(r'(?:inline\s+)?namespace\s*([\w:]*)\s*$', sflat_nt)
            tm = re.match(r'(?:typedef\s+)?(enum\s+class|enum\s+struct|enum|class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?(?:[A-Z_][A-Z0-9_]*\s+)?([A-Za-z_]\w*)?', sflat_nt)
            if nsm and True:
                names = [x for x in nsm.group(1).split('::') if x] or ['<anon>']
                for k, nm in enumerate(names):
                    stack.append({'kind': 'namespace', 'name': nm, 'access': 'public',
                                  'internal': nm in ('detail', 'internal', '<anon>'),
                                  'chain': k < len(names) - 1})
                stmt_start = i + 1
                i += 1
            elif tm and '(' not in sflat_nt.split('{')[0].split(':')[0]:
                kw = tm.group(1)
                name = tm.group(2) or ''
                if kw.startswith('enum'):
                    if name:
                        o = {'symbol': '::'.join(ns_path() + [name]), 'header': rel,
                             'line': bisect.bisect_right(starts, first_nonspace(stmt_start)), 'kind': 'type',
                             'name': name, 'scope': '::'.join(ns_path()), 'internal': any(s['internal'] for s in stack)}
                        if not (stack[-1]['kind'] == 'class' and stack[-1]['access'] != 'public'):
                            out.append(o)
                    j = match_close(t, i)
                    i = j + 1
                    stmt_start = i
                else:
                    if name and not (stack[-1]['kind'] == 'class' and stack[-1]['access'] != 'public'):
                        out.append({'symbol': '::'.join(ns_path() + [name]), 'header': rel,
                                    'line': bisect.bisect_right(starts, first_nonspace(stmt_start)), 'kind': 'type',
                                    'name': name, 'scope': '::'.join(ns_path()),
                                    'internal': any(s['internal'] for s in stack)})
                    stack.append({'kind': 'class', 'name': name, 'access': 'public' if kw != 'class' else 'private',
                                  'internal': False, 'pop': 1})
                    stmt_start = i + 1
                    i += 1
            else:
                # function body / initializer: emit the function (if any), skip body
                r = classify(stmt, stack[-1]['kind'], stack[-1]['name'])
                prev = t[stmt_start:i].rstrip()[-1:] if t[stmt_start:i].strip() else ''
                has_init_colon = bool(re.search(r'\)\s*(?:noexcept\s*)?:(?!:)', sflat))
                if r and has_init_colon and re.search(r'[\w>]\s*$', t[stmt_start:i]) and not re.search(r'\)\s*(?:const|noexcept|override|final)?\s*$', t[stmt_start:i]):
                    # brace initializer inside a constructor init list; keep scanning the statement
                    j = match_close(t, i)
                    i = j + 1
                    continue
                if r:
                    emit(i, r[0], r[1], first_nonspace(stmt_start))
                j = match_close(t, i)
                i = j + 1
                stmt_start = i
                # swallow a trailing ';' after a body
        elif c == '}':
            sc = stack[-1]
            if sc['kind'] in ('namespace', 'class') and len(stack) > 1:
                stack.pop()
                # namespace a::b { pushed several entries: pop the whole chain
                while len(stack) > 1 and stack[-1].get('chain'):
                    stack.pop()
            stmt_start = i + 1
            i += 1
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=None)
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    repo = a.repo or os.path.abspath(os.path.join(here, '..', '..', '..'))
    out = a.out or os.path.join(here, 'kernel_api.jsonl')
    kdir = os.path.join(repo, 'dino8-kernel')
    rows = []
    nheaders = 0
    for sub in ('include', 'src'):
        for root, _, files in os.walk(os.path.join(kdir, sub)):
            for f in sorted(files):
                if f.endswith(('.h', '.hpp')):
                    p = os.path.join(root, f)
                    nheaders += 1
                    rel = os.path.relpath(p, repo)
                    rows.extend(parse_header(p, rel, internal_hdr=(sub == 'src')))
    rows.sort(key=lambda r: (r['header'], r['line'], r['symbol']))
    with open(out, 'w', encoding='utf-8') as fh:
        for r in rows:
            fh.write(json.dumps(r) + '\n')
    kinds = Counter(r['kind'] for r in rows)
    print(json.dumps({'headers_scanned': nheaders, 'rows': len(rows),
                      'by_kind': dict(kinds),
                      'internal_rows': sum(1 for r in rows if r['internal']),
                      'public_non_internal_functions': sum(1 for r in rows if not r['internal'] and r['kind'] not in ('type',)),
                      'unique_function_symbols': len({r['symbol'] for r in rows if r['kind'] != 'type'})}, indent=2))


if __name__ == '__main__':
    main()
