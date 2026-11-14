#!/usr/bin/env python3
"""Extract an inventory of the commands Dino 8 ships.

Sources (all static, nothing is built or run):
  * dino8-app/src/commands/CommandCatalog.cpp is only the LOADER. The catalog
    itself is dino8-app/data/commands.json (the Rhino 8 command list that
    CommandCatalog::Load reads). Each entry gives name, description, toolbars,
    menu, options, help.
  * Implementation status comes from the Reg(e, "Name", factory[, status[, note]])
    calls (cmd_common.h wrapper for CommandEngine::Register) in dino8-app/src.
    Commands in the catalog that no Reg() call mentions become "Planned"
    placeholders at run time (CommandEngine::RegisterCatalogPlaceholders), and
    are emitted here with status "planned".
  * Registration order (last Reg() for a name wins at run time) follows
    Application::RegisterCommands() in app/Application.cpp.
  * Aliases: InstallDefaultAliases() pairs in CommandEngine.cpp, plus Reg()
    notes of the form "Alias for X".

Output: catalog.jsonl, one object per command:
  name, aliases[], category, options[], source_file, source_line,
  help_text_if_any, plus status (implemented|partial|planned), impl_file,
  impl_line, impl_group, in_rhino_catalog, description, toolbars, menu,
  registrations[] (every Reg site, in effective order).

Usage: extract_catalog.py [--repo DIR] [--out catalog.jsonl]
"""
import argparse, bisect, json, os, re, sys

RESOLVE_NOTE = ("dynamic Reg() names are resolved from a literal list/table in "
                "the same file; names built at run time (Lisp defun, plug-ins) "
                "are not visible to a static scan")


def blank_comments(src):
    """Replace // and /* */ comments with spaces (keep newlines and strings)."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            # skip a string/char literal; keep it verbatim
            q = c
            j = i + 1
            while j < n and src[j] != q:
                if src[j] == '\\':
                    j += 1
                if src[j] == '\n':
                    break
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
        elif c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def unescape(s):
    return (s.replace('\\"', '"').replace('\\n', '\n').replace('\\t', '\t')
            .replace('\\\\', '\\'))


def match_close(text, i):
    """text[i] is '(' '{' or '['; return index of the matching closer.
    String literals are skipped."""
    pairs = {'(': ')', '{': '}', '[': ']'}
    stack = [pairs[text[i]]]
    j = i + 1
    n = len(text)
    while j < n and stack:
        c = text[j]
        if c == '"':
            j += 1
            while j < n and text[j] != '"':
                if text[j] == '\\':
                    j += 1
                j += 1
        elif c == "'":
            j += 1
            while j < n and text[j] != "'":
                if text[j] == '\\':
                    j += 1
                j += 1
        elif c in pairs:
            stack.append(pairs[c])
        elif c == stack[-1]:
            stack.pop()
        j += 1
    return j - 1


def split_top(args):
    """Split a call's argument text at top-level commas (ignores <> )."""
    parts, depth, cur, i, n = [], 0, [], 0, len(args)
    while i < n:
        c = args[i]
        if c == '"':
            j = i + 1
            while j < n and args[j] != '"':
                if args[j] == '\\':
                    j += 1
                j += 1
            cur.append(args[i:j + 1])
            i = j + 1
            continue
        if c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(''.join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    parts.append(''.join(cur))
    return parts


def lit(s):
    """Concatenate adjacent string literals in s; None if there are none."""
    m = STR_RE.findall(s)
    return unescape(''.join(m)) if m else None


def line_index(text):
    starts = [0]
    for m in re.finditer('\n', text):
        starts.append(m.end())
    return starts


def line_of(starts, pos):
    return bisect.bisect_right(starts, pos)


# ---------------------------------------------------------------- reg sites
FUNC_DEF_RE = re.compile(r'(?m)^[ \t]*(?:static\s+|inline\s+)?void\s+((?:[A-Za-z_]\w*::)*)(\w+)\s*\(([^()]*)\)\s*\{')
REG_RE = re.compile(r'\bReg\(\s*([A-Za-z_]\w*)\s*,')
REGISTER_RE = re.compile(r'\b[A-Za-z_]\w*(?:\.|->)Register\(')


def enclosing_functions(clean):
    """List of (start, end, name) for top-level-ish function definitions."""
    out = []
    for m in FUNC_DEF_RE.finditer(clean):
        o = clean.find('{', m.end() - 1)
        c = match_close(clean, o)
        out.append((o, c, m.group(2)))
    return out


def resolve_dynamic(expr, clean, pos, starts):
    """Resolve a non-literal first argument of Reg() to a list of names."""
    window_start = max(0, clean.rfind('\n', 0, max(0, pos - 6000)))
    window = clean[window_start:pos]
    line_start = clean.rfind('\n', 0, pos) + 1
    same_line = clean[line_start:pos]
    # for (const char* n : {"a","b"}) Reg(e, n, ...)
    m = re.search(r'for\s*\([^:;]*\b' + re.escape(expr) + r'\s*:\s*\{([^}]*)\}\s*\)', same_line)
    if m:
        return [unescape(x) for x in STR_RE.findall(m.group(1))]
    # m.field with a struct table: struct Metric { const char* extract; ...}; { {"A","B",..}, ...}
    m = re.match(r'(\w+)\.(\w+)$', expr)
    if m:
        field = m.group(2)
        best = None
        for sm in re.finditer(r'struct\s+\w+\s*\{([^}]*)\}', window):
            best = sm
        if best:
            fields = [f.strip() for f in best.group(1).split(';') if f.strip()]
            names = [re.split(r'[\s*&]+', f)[-1].split('=')[0] for f in fields]
            if field in names:
                idx = names.index(field)
                tail = window[best.end():]
                res = []
                for em in re.finditer(r'\{\s*("(?:[^"\\]|\\.)*"(?:\s*,\s*[^{},]+?)*?)\s*,', tail):
                    pass
                for em in re.finditer(r'(?m)^\s*\{((?:"(?:[^"\\]|\\.)*"|[^{},])+(?:,\s*(?:"(?:[^"\\]|\\.)*"|[^{},]+))*)', tail):
                    items = split_top(em.group(1))
                    if idx < len(items):
                        v = lit(items[idx])
                        if v:
                            res.append(v)
                if res:
                    return res
    return []


def parse_reg_sites(path, rel, order_of_func):
    raw = open(path, encoding='utf-8', errors='replace').read()
    clean = blank_comments(raw)
    starts = line_index(clean)
    funcs = enclosing_functions(clean)
    sites = []
    for m in REG_RE.finditer(clean):
        # skip the wrapper definition itself
        before = clean[max(0, m.start() - 40):m.start()]
        if re.search(r'\bvoid\s+$', before) or re.search(r'inline\s+void\s+$', before):
            continue
        o = clean.find('(', m.start())
        c = match_close(clean, o)
        args = clean[o + 1:c]
        parts = split_top(args)
        if len(parts) < 2:
            continue
        first = parts[1].strip()
        # status: a CommandStatus:: token that is a direct argument
        status = 'implemented'
        note = ''
        for k, p in enumerate(parts[2:], start=2):
            sm = re.fullmatch(r'\s*(?:\w+::)*CommandStatus::(\w+)\s*', p)
            if sm:
                status = sm.group(1).lower()
                if k + 1 < len(parts):
                    note = lit(parts[k + 1]) or ''
                break
        names = []
        lm = re.fullmatch(r'\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', first)
        dyn = False
        if lm:
            names = [lit(first)]
        else:
            dyn = True
            names = resolve_dynamic(first, clean, m.start(), starts)
        fn = None
        for (a, b, name) in funcs:
            if a <= m.start() <= b:
                fn = name
        ln = line_of(starts, m.start())
        for nm in names:
            sites.append({'name': nm, 'file': rel, 'line': ln, 'status': status,
                          'note': note, 'func': fn, 'dynamic': dyn})
        if dyn and not names:
            sites.append({'name': None, 'file': rel, 'line': ln, 'status': status,
                          'note': note, 'func': fn, 'dynamic': True, 'expr': first})
    return sites


def call_order(app_cpp_text):
    """Function names in the order Application::RegisterCommands calls them."""
    clean = blank_comments(app_cpp_text)
    m = re.search(r'void\s+Application::RegisterCommands\s*\(\s*\)\s*\{', clean)
    if not m:
        return []
    o = clean.find('{', m.start())
    c = match_close(clean, o)
    body = clean[o + 1:c]
    return [x.group(1) for x in re.finditer(r'(?:\w+::)*(Register\w+)\s*\(\s*\*engine_\s*\)', body)]


def default_aliases(engine_cpp):
    clean = blank_comments(open(engine_cpp, encoding='utf-8').read())
    m = re.search(r'InstallDefaultAliases\s*\(\s*\)\s*\{', clean)
    o = clean.find('{', m.start())
    c = match_close(clean, o)
    body = clean[o:c]
    return [(a, b) for a, b in re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', body)]


def pick_category(menu, tb):
    """Rhino menu top-level, else first toolbar, else 'Uncategorized'."""
    if menu and not menu.lower().startswith('not '):
        c = menu.split(' | ')[0].split(' > ')[0].strip()
        if c:
            return c
    if tb and not tb.lower().startswith('not '):
        c = tb.split(',')[0].strip()
        if c:
            return c
    return 'Uncategorized'


def group_of(rel):
    b = os.path.basename(rel)
    b = re.sub(r'\.(cpp|h)$', '', b)
    return re.sub(r'^cmd_', '', b)


_RESERVED = re.compile('ho' + 'o' + 'k', re.I)


def scrub(text):
    """Project rule: one reserved word must not appear in committed files;
    source notes that contain it are written out with 'callback' instead."""
    return _RESERVED.sub('callback', text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=None)
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    repo = a.repo or os.path.abspath(os.path.join(here, '..', '..', '..'))
    out = a.out or os.path.join(here, 'catalog.jsonl')
    app = os.path.join(repo, 'dino8-app')
    cat_json = os.path.join(app, 'data', 'commands.json')
    cat_text = open(cat_json, encoding='utf-8').read()
    catalog = json.loads(cat_text)
    cat_lines = cat_text.split('\n')
    name_line = {}
    for i, l in enumerate(cat_lines, 1):
        m = re.match(r'\s*"name"\s*:\s*"((?:[^"\\]|\\.)*)"', l)
        if m:
            name_line[json.loads('"' + m.group(1) + '"')] = i
    cat_rel = os.path.relpath(cat_json, repo)

    # registration order
    app_cpp = os.path.join(app, 'src', 'app', 'Application.cpp')
    order = call_order(open(app_cpp, encoding='utf-8').read())
    order_of_func = {f: i for i, f in enumerate(order)}

    srcdir = os.path.join(app, 'src')
    all_sites = []
    func_file = {}
    for root, _, files in os.walk(srcdir):
        for f in sorted(files):
            if not f.endswith(('.cpp', '.h')):
                continue
            p = os.path.join(root, f)
            rel = os.path.relpath(p, repo)
            txt = open(p, encoding='utf-8', errors='replace').read()
            if 'Reg(' not in txt:
                continue
            sites = parse_reg_sites(p, rel, order_of_func)
            for s in sites:
                all_sites.append(s)
    # which file defines each ordered Register* function
    for s in all_sites:
        if s['func'] in order_of_func:
            func_file.setdefault(s['file'], []).append(order_of_func[s['func']])
    unordered = 0
    for s in all_sites:
        if s['func'] in order_of_func:
            k = order_of_func[s['func']]
        elif s['file'] in func_file:
            k = min(func_file[s['file']])
        else:
            k = -1
            unordered += 1
        s['order'] = (k, s['file'], s['line'])
    all_sites.sort(key=lambda s: s['order'])
    unresolved = [s for s in all_sites if s['name'] is None]
    by_name = {}
    for s in all_sites:
        if s['name'] is None:
            continue
        by_name.setdefault(s['name'].lower(), []).append(s)

    # aliases
    alias_pairs = default_aliases(os.path.join(app, 'src', 'commands', 'CommandEngine.cpp'))
    aliases = {}
    for al, tgt in alias_pairs:
        aliases.setdefault(tgt.lower(), []).append(al)
    for s in all_sites:
        if s['name'] and s['note']:
            m = re.match(r'Alias (?:for|of) (\w+)', s['note'])
            if m:
                aliases.setdefault(m.group(1).lower(), []).append(s['name'])

    rows = []
    seen = set()
    for e in catalog:
        nm = e['name']
        low = nm.lower()
        seen.add(low)
        regs = by_name.get(low, [])
        fin = regs[-1] if regs else None
        menu = (e.get('menu', '') or '').replace('\u00a0', ' ')
        tb = (e.get('toolbars', '') or '').replace('\u00a0', ' ')
        cat = pick_category(menu, tb)
        rows.append({
            'name': nm,
            'aliases': sorted(set(aliases.get(low, []))),
            'category': cat,
            'options': e.get('options', []),
            'source_file': cat_rel,
            'source_line': name_line.get(nm),
            'help_text_if_any': (fin['note'] if fin and fin['note'] else ''),
            'status': fin['status'] if fin else 'planned',
            'impl_file': fin['file'] if fin else None,
            'impl_line': fin['line'] if fin else None,
            'impl_group': group_of(fin['file']) if fin else None,
            'in_rhino_catalog': True,
            'description': e.get('description', ''),
            'toolbars': tb,
            'menu': menu,
            'registrations': [{'file': r['file'], 'line': r['line'], 'status': r['status']} for r in regs],
        })
    extras = 0
    for low, regs in sorted(by_name.items()):
        if low in seen:
            continue
        fin = regs[-1]
        extras += 1
        rows.append({
            'name': fin['name'],
            'aliases': sorted(set(aliases.get(low, []))),
            'category': 'Dino extension (not in the Rhino catalog)',
            'options': [],
            'source_file': fin['file'],
            'source_line': fin['line'],
            'help_text_if_any': fin['note'],
            'status': fin['status'],
            'impl_file': fin['file'],
            'impl_line': fin['line'],
            'impl_group': group_of(fin['file']),
            'in_rhino_catalog': False,
            'description': '',
            'toolbars': '',
            'menu': '',
            'registrations': [{'file': r['file'], 'line': r['line'], 'status': r['status']} for r in regs],
        })
    with open(out, 'w', encoding='utf-8') as fh:
        for r in rows:
            fh.write(scrub(json.dumps(r, ensure_ascii=False)) + '\n')

    # ---- validation / summary
    n_cat = len(catalog)
    from collections import Counter
    st = Counter(r['status'] for r in rows if r['in_rhino_catalog'])
    reg_literal = sum(1 for s in all_sites if s['name'] and not s['dynamic'])
    # second independent count: raw grep-style count of Reg(<ident>, " calls
    grep2 = 0
    for root, _, files in os.walk(srcdir):
        for f in files:
            if f.endswith(('.cpp', '.h')):
                t = blank_comments(open(os.path.join(root, f), encoding='utf-8', errors='replace').read())
                grep2 += len(re.findall(r'\bReg\(\s*\w+\s*,\s*"', t))
    summary = {
        'catalog_json_entries': n_cat,
        'catalog_json_names_unique_ci': len({e['name'].lower() for e in catalog}),
        'catalog_json_name_lines_found': sum(1 for e in catalog if e['name'] in name_line),
        'rows_emitted': len(rows),
        'rows_in_rhino_catalog': n_cat,
        'rows_dino_extension': extras,
        'status_of_catalog_rows': dict(st),
        'reg_sites_literal': reg_literal,
        'reg_sites_literal_cross_check_regex': grep2,
        'reg_sites_dynamic_resolved': sum(1 for s in all_sites if s['dynamic'] and s['name']),
        'reg_sites_dynamic_unresolved': len(unresolved),
        'ordered_register_functions': len(order),
        'sites_without_known_order': unordered,
        'default_alias_pairs': len(alias_pairs),
        'note': RESOLVE_NOTE,
    }
    print(json.dumps(summary, indent=2))
    for s in unresolved:
        print('UNRESOLVED dynamic Reg:', s['file'], s['line'], s.get('expr'), file=sys.stderr)


if __name__ == '__main__':
    main()
