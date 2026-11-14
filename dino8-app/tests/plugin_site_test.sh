#!/usr/bin/env bash
# Tests tools/site/build_site.py (the static plug-in marketplace / community site
# generator): it renders the same index JSON the app's Marketplace code reads, the
# output has no external requests, hostile index data is escaped, bad indexes are
# refused the same way the app refuses them, and the app can load the very
# plugin-index.json the site publishes.
# Prints "ok   psite_..." / "FAIL psite_..." lines; exits non-zero if any FAIL.
# Usage: bash plugin_site_test.sh <path to Dino8 binary>
trap '' PIPE
set -euo pipefail
echo() { builtin echo "$@" 2>/dev/null || true; }

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$HERE/.." && pwd)"
BIN="${1:?usage: plugin_site_test.sh <Dino8 binary>}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
has() { if grep -qF -- "$2" <<<"$1"; then ok "$3"; else bad "$3"; builtin echo "   wanted: $2" >&2; tail -8 <<<"$1" >&2 || true; fi; }
lacks() { if grep -qF -- "$2" <<<"$1"; then bad "$3"; else ok "$3"; fi; }
if ! command -v python3 >/dev/null 2>&1; then echo "skip psite_all (python3 not available)"; exit 0; fi
SITE="python3 -I $APP/tools/site/build_site.py"

run_app() {  # run_app <script>
  mkdir -p "$W/cfg"
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$W/cfg" HOME="$W/cfg" timeout 900 "$BIN" --smoke 20 --script "$1" 2>&1
  else
    XDG_CONFIG_HOME="$W/cfg" HOME="$W/cfg" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 20 --script "$1" 2>&1
  fi
}

# ---- the real reference index -------------------------------------------------
OUT="$W/public"
GEN="$($SITE --index "$APP/plugin-index/index.json" --out "$OUT" --site-url https://plugins.example.org/ --discussions-url https://github.com/example/dino8/discussions 2>&1)" || { builtin echo "$GEN"; bad psite_build; exit 1; }
NCARDS="$(grep -o 'class="card"' "$OUT/index.html" | grep -c . || true)"
NPAGES="$(ls "$OUT/plugin" | grep -c '\.html$' || true)"
if [ "$NCARDS" = 4 ] && [ "$NPAGES" = 4 ]; then ok "psite_renders_index: the reference index (4 plug-ins) became 4 cards on index.html and 4 plugin/<id>.html pages"; else bad "psite_renders_index: $NCARDS cards, $NPAGES pages"; fi
if cmp -s "$APP/plugin-index/index.json" "$OUT/plugin-index.json"; then ok "psite_index_verbatim: plugin-index.json is a byte-identical copy of the input index (what PluginMarketplaceIndex <url> should be pointed at)"; else bad psite_index_verbatim; fi
has "$(cat "$OUT/plugin/hellodino.html")" "PluginMarketplaceIndex https://plugins.example.org/plugin-index.json" "psite_install_instructions: a plugin page gives the exact PluginMarketplaceIndex <site-url>/plugin-index.json command"
has "$(cat "$OUT/plugin/hellodino.html")" "PluginMarketplaceInstall hellodino" "psite_install_command: ... and PluginMarketplaceInstall <id>"
has "$(cat "$OUT/community.html")" 'href="https://github.com/example/dino8/discussions"' "psite_community_link: community.html links the configured discussions URL"

# no external requests of any kind in anything the site emits
EXT="$(grep -rEn '<(script|link|img|iframe|source|video|audio|embed|object)[^>]*(src|href|data)="(https?:)?//|@import|url\((https?:)?//|<form|<iframe' "$OUT" || true)"
if [ -z "$EXT" ]; then ok "psite_no_external_requests: no script/link/img/iframe/form/@import points off-site in any generated file"; else bad "psite_no_external_requests: $EXT"; fi
SCRIPTS="$(grep -rhoE '<script[^>]*>' "$OUT" | sort -u | tr '\n' ' ')"
has "$SCRIPTS" '<script src="site.js">' "psite_one_local_script: the only script is the local site.js"
has "$(cat "$OUT/index.html")" "Content-Security-Policy" "psite_csp: pages carry a Content-Security-Policy that forbids other origins"
if grep -rqiE 'google-analytics|googletagmanager|gtag\(|plausible|matomo|fonts\.googleapis|cdn\.' "$OUT"; then bad "psite_no_tracker"; else ok "psite_no_tracker: no analytics, font CDN or tracker strings in the output"; fi

# the app loads the exact file the site publishes
cat > "$W/app1.txt" <<EOS
PluginMarketplaceIndex $OUT/plugin-index.json
PluginMarketplaceList
EOS
O1="$(run_app "$W/app1.txt")" || true
has "$O1" "PluginMarketplaceList: 4 plug-in(s) in the loaded index" "psite_app_reads_site_index: the app lists 4 plug-ins from the site's plugin-index.json - same count as the site's 4 cards"

# the filter box (site.js) works: run it under node against the real generated cards with a minimal DOM stub
if command -v node >/dev/null 2>&1; then
  JSOUT="$(node - "$OUT/index.html" "$OUT/site.js" <<'JSEOF' 2>&1 || true
const fs = require('fs');
const html = fs.readFileSync(process.argv[2], 'utf8');
const cards = [...html.matchAll(/class="card" data-search="([^"]*)"/g)].map(m => ({ hidden: false, s: m[1], getAttribute(k) { return k === 'data-search' ? this.s : null; } }));
let handler = null;
const q = { value: '', addEventListener(ev, fn) { if (ev === 'input') handler = fn; } };
const count = { textContent: '' };
global.document = { getElementById: id => (id === 'q' ? q : count), querySelectorAll: () => ({ length: cards.length, forEach: f => cards.forEach(f) }) };
(0, eval)(fs.readFileSync(process.argv[3], 'utf8'));
q.value = 'mesh'; handler();
const meshShown = cards.filter(c => !c.hidden).length;
q.value = 'sample'; handler();
const sampleShown = cards.filter(c => !c.hidden).length;
q.value = 'doesnotexist123'; handler();
const noneShown = cards.filter(c => !c.hidden).length;
q.value = ''; handler();
console.log('FILTER cards=' + cards.length + ' mesh=' + meshShown + ' sample=' + sampleShown + ' none=' + noneShown + ' cleared=' + cards.filter(c => !c.hidden).length + ' label=' + count.textContent);
JSEOF
)"
  has "$JSOUT" "FILTER cards=4 mesh=1 sample=4 none=0 cleared=4 label=4 of 4 plug-in(s)" "psite_js_filter: site.js (run under node on the generated cards) filters 'mesh'->1, 'sample'->4, nonsense->0 and restores 4 - the same answers the app's PluginMarketplaceList filter gives"
else
  echo "skip psite_js_filter (node not available)"
fi

# ---- hostile data is escaped, ids cannot escape --out ----------------------------
python3 -I - "$W" <<'PYEOF'
import json, sys
w = sys.argv[1]
evil = {"schema_version": 1, "index_name": "<img src=x onerror=alert(1)>", "plugins": [{
    "id": "evil", "name": "<script>alert(1)</script>", "version": "1.0", "author": "\"><script>x</script>",
    "description": "<b onmouseover=alert(2)>hi</b>", "tags": ["<i>t</i>"], "homepage": "javascript:alert(3)",
    "download_url": "https://example.org/x.so", "sha256": "a" * 64, "api_version": 2}]}
json.dump(evil, open(w + "/evil.json", "w"))
def mk(name, mut):
    p = {"id": "ok", "name": "Ok", "version": "1.0", "download_url": "https://example.org/ok.so", "sha256": "b" * 64}
    p.update(mut)
    json.dump({"schema_version": 1, "plugins": [p]}, open(w + "/" + name + ".json", "w"))
mk("trav", {"id": "../escape"})
mk("both", {"bundled_path": "plugins/x"})
mk("neither", {"download_url": ""})
mk("noname", {"name": ""})
mk("badsha", {"sha256": "REPLACE-ME"})
mk("ftp", {"download_url": "ftp://example.org/x.so"})
json.dump({"schema_version": 2, "plugins": []}, open(w + "/schema2.json", "w"))
json.dump({"schema_version": 1, "plugins": [
    {"id": "A", "name": "A", "version": "1", "download_url": "https://e.org/a.so"},
    {"id": "a", "name": "a", "version": "1", "download_url": "https://e.org/b.so"}]}, open(w + "/dupe.json", "w"))
PYEOF
EV="$($SITE --index "$W/evil.json" --out "$W/evil-site" 2>&1)" || bad "psite_evil_builds"
ALLEV="$(cat "$W"/evil-site/*.html "$W"/evil-site/plugin/*.html)"
lacks "$ALLEV" "<script>alert" "psite_escapes_script: a <script> in name/author is rendered inert (escaped)"
lacks "$ALLEV" "<b onmouseover" "psite_escapes_markup: markup in description is escaped"
lacks "$ALLEV" "<img src=x" "psite_escapes_index_name: markup in index_name is escaped"
lacks "$ALLEV" 'href="javascript:' "psite_no_javascript_url: a javascript: homepage is never turned into a link"
has "$ALLEV" "&lt;script&gt;alert(1)&lt;/script&gt;" "psite_escaped_text_present: the hostile name is still shown, as text"

# bad indexes: site refuses; the app refuses the same ones for the same reasons
cat > "$W/app2.txt" <<EOS
PluginMarketplaceIndex $W/schema2.json
PluginMarketplaceIndex $W/both.json
PluginMarketplaceIndex $W/neither.json
PluginMarketplaceIndex $W/noname.json
EOS
O2="$(run_app "$W/app2.txt")" || true
for f in schema2 both neither noname; do
  if $SITE --index "$W/$f.json" --out "$W/rej-$f" >/dev/null 2>"$W/rej-$f.err"; then bad "psite_rejects_$f: site accepted it"; else
    if [ -e "$W/rej-$f" ]; then bad "psite_rejects_$f: wrote output for an invalid index"; else ok "psite_rejects_$f: site refuses it (exit non-zero, nothing written)"; fi; fi
done
has "$O2" "unsupported schema_version 2" "psite_app_parity_schema: the app refuses schema_version 2 too"
has "$O2" "has both download_url and bundled_path" "psite_app_parity_both: the app refuses an entry with both sources too"
has "$O2" "has neither download_url nor bundled_path" "psite_app_parity_neither: ... and with neither"
has "$O2" "is missing a required field (id/name/version)" "psite_app_parity_noname: ... and a missing name"
for f in trav dupe badsha ftp; do
  if $SITE --index "$W/$f.json" --out "$W/rej-$f" >/dev/null 2>&1; then bad "psite_stricter_$f: site accepted it"; else ok "psite_stricter_$f: site refuses it (stricter than the app: ids are file names, sha256 must be real, http(s) only)"; fi
done
if [ -e "$W/escape.html" ] || [ -e "$W/rej-trav" ]; then bad "psite_traversal_nothing_written"; else ok "psite_traversal_nothing_written: the ../escape id wrote nothing anywhere"; fi

# a template's untouched index-entry.json placeholder is refused; a packaged entry is accepted
python3 -I "$APP/plugin-templates/new_plugin.py" command --out "$W/tgen" >/dev/null
python3 -I - "$W" <<'PYEOF'
import json, sys
w = sys.argv[1]
e = json.load(open(w + "/tgen/my_command_plugin/index-entry.json"))
json.dump({"schema_version": 1, "plugins": [e]}, open(w + "/placeholder.json", "w"))
PYEOF
if $SITE --index "$W/placeholder.json" --out "$W/rej-placeholder" >/dev/null 2>&1; then bad "psite_placeholder_entry_refused"; else ok "psite_placeholder_entry_refused: the template's untouched index-entry.json (REPLACE-ME sha256) cannot be published by accident"; fi
head -c 4096 /dev/urandom > "$W/my_command_plugin.so"
python3 -I "$APP/plugin-templates/package_plugin.py" --lib "$W/my_command_plugin.so" --manifest "$W/tgen/my_command_plugin/manifest.json" --url https://example.org/my_command_plugin.so --out "$W/dist" >/dev/null
python3 -I - "$W" <<'PYEOF'
import json, sys
w = sys.argv[1]
e = json.load(open(w + "/dist/my_command_plugin-1.0.0.index-entry.json"))
json.dump({"schema_version": 1, "index_name": "Packaged", "plugins": [e]}, open(w + "/packaged.json", "w"))
PYEOF
PK="$($SITE --index "$W/packaged.json" --out "$W/pk-site" 2>&1)" && ok "psite_packaged_entry_accepted: an entry written by package_plugin.py renders ($(ls "$W/pk-site/plugin"))" || { builtin echo "$PK"; bad psite_packaged_entry_accepted; }
WARN="$($SITE --index "$W/ftp.json" --check-only 2>&1 || true)"
NOSHA='{"schema_version":1,"plugins":[{"id":"x","name":"X","version":"1","download_url":"https://e.org/x.so"}]}'
printf '%s' "$NOSHA" > "$W/nosha.json"
WARN="$($SITE --index "$W/nosha.json" --check-only 2>&1)"
has "$WARN" "no sha256 - the app installs it without an integrity check" "psite_warns_missing_sha: an entry without sha256 gets a warning (the app would install it unverified)"

# ---- community scaffolding: checked-in GitHub forms are well-formed and match docs/COMMUNITY.md ----
REPO_ROOT="$(cd "$APP/.." && pwd)"
FORMS="$(python3 -I - "$REPO_ROOT" "$APP/docs/COMMUNITY.md" <<'PYEOF' 2>&1 || true
import glob, os, re, sys
root, community = sys.argv[1:3]
doc = open(community, encoding="utf-8").read()
try:
    import yaml
except ImportError:
    yaml = None
disc = sorted(glob.glob(os.path.join(root, ".github", "DISCUSSION_TEMPLATE", "*.yml")))
issue = sorted(glob.glob(os.path.join(root, ".github", "ISSUE_TEMPLATE", "*.yml")))
problems = []
if not disc: problems.append("no discussion templates")
if not issue: problems.append("no issue forms")
for f in disc:
    slug = os.path.basename(f)[:-4]
    if "`%s`" % slug not in doc:
        problems.append("%s is not documented in COMMUNITY.md" % os.path.basename(f))
if yaml:
    for f in disc + issue:
        d = yaml.safe_load(open(f, encoding="utf-8"))
        name = os.path.relpath(f, root)
        if not isinstance(d, dict) or not isinstance(d.get("body"), list) or not d["body"]:
            problems.append(name + ": no body list"); continue
        if f in issue and not (d.get("name") and d.get("description")):
            problems.append(name + ": issue form needs name and description")
        ids = []
        for el in d["body"]:
            t = el.get("type")
            if t not in ("markdown", "input", "textarea", "dropdown", "checkboxes"):
                problems.append("%s: bad element type %r" % (name, t)); continue
            a = el.get("attributes") or {}
            if t == "markdown":
                if not a.get("value"): problems.append(name + ": empty markdown")
                continue
            if not a.get("label"): problems.append(name + ": element without label")
            if t in ("dropdown",) and not a.get("options"): problems.append(name + ": dropdown without options")
            if t == "checkboxes" and not a.get("options"): problems.append(name + ": checkboxes without options")
            if "id" in el: ids.append(el["id"])
        if len(ids) != len(set(ids)): problems.append(name + ": duplicate element ids")
print("FORMS files=%d parsed=%s problems=%s" % (len(disc) + len(issue), "yes" if yaml else "no(PyYAML missing)", problems or "none"))
PYEOF
)"
has "$FORMS" "problems=none" "psite_github_forms: every .github/DISCUSSION_TEMPLATE and ISSUE_TEMPLATE form is well-formed and each discussion form is documented in COMMUNITY.md ($(grep -o 'files=[0-9]* parsed=[a-zA-Z()]*' <<<"$FORMS" || true))"

exit $fail
