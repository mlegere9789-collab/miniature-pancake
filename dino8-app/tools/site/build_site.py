#!/usr/bin/env python3
"""Static marketplace / community site generator for Dino 8 plug-ins.

    python3 build_site.py --index ../../plugin-index/index.json --out ./public \
        [--title "My Plug-in Index"] [--site-url https://plugins.example.org/] \
        [--discussions-url https://github.com/<owner>/<repo>/discussions] [--check-only]

Reads the SAME plug-in index JSON the app's Marketplace code reads (schema in
plugin-index/SCHEMA.md; parser in src/plugins/MarketplaceIndex.cpp) and writes a
self-contained static site: index.html (searchable list), plugin/<id>.html (one
page per plug-in with install instructions), community.html, style.css, site.js
and plugin-index.json (a verbatim copy of the input - this is the file to give to
`PluginMarketplaceIndex <url>`).

Self-hostable by design: plain HTML + one small JS file + one CSS file, no
external CDN, no fonts, no analytics, no tracker, no forms; a Content-Security-
Policy meta tag forbids loading anything from another origin. Every value from
the index is HTML-escaped (the index is third-party data), links are only
rendered for http(s) URLs, and ids are restricted so they cannot escape --out.
Standard library only.
"""
import argparse
import html
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
SHA_RE = re.compile(r"^[0-9a-fA-F]{64}$")
CSP = ("default-src 'none'; style-src 'self'; script-src 'self'; img-src 'self' data:; "
       "base-uri 'none'; form-action 'none'")


class IndexError_(Exception):
    pass


def validate(root):
    """Mirror plugins::ParseIndex (hard errors) and add site-level lint. Returns (plugins, warnings)."""
    if not isinstance(root, dict):
        raise IndexError_("index root is not a JSON object")
    sv = root.get("schema_version")
    if isinstance(sv, bool) or not isinstance(sv, (int, float)) or int(sv) != 1:
        raise IndexError_("unsupported schema_version %r (this site reads schema_version 1 - see plugin-index/SCHEMA.md)" % (sv,))
    plugins = root.get("plugins")
    if not isinstance(plugins, list):
        raise IndexError_('index has no "plugins" array')
    warnings, seen = [], set()
    for i, p in enumerate(plugins):
        if not isinstance(p, dict):
            raise IndexError_("plugins[%d] is not an object" % i)
        pid = p.get("id") if isinstance(p.get("id"), str) else ""
        tag = "plugins[%d]%s" % (i, " (%s)" % pid if pid else "")
        for k in ("id", "name", "version"):
            if not isinstance(p.get(k), str) or not p.get(k):
                raise IndexError_("%s is missing a required field (id/name/version)" % tag)
        has_url = isinstance(p.get("download_url"), str) and p["download_url"] != ""
        has_bundle = isinstance(p.get("bundled_path"), str) and p["bundled_path"] != ""
        if not has_url and not has_bundle:
            raise IndexError_("%s has neither download_url nor bundled_path" % tag)
        if has_url and has_bundle:
            raise IndexError_("%s has both download_url and bundled_path - exactly one is required" % tag)
        # site-level rules (stricter than the app, because ids become file names and URLs)
        if not ID_RE.match(pid):
            raise IndexError_("%s: id must match %s so it is safe as a file name" % (tag, ID_RE.pattern))
        if pid.lower() in seen:
            raise IndexError_("%s: duplicate id (ids are compared case-insensitively here)" % tag)
        seen.add(pid.lower())
        if has_url and not re.match(r"^https?://", p["download_url"]):
            raise IndexError_("%s: download_url must be http:// or https://" % tag)
        if has_url and p["download_url"].startswith("http://"):
            warnings.append("%s: download_url is plain http" % tag)
        if has_url and not p.get("sha256"):
            warnings.append("%s: no sha256 - the app installs it without an integrity check" % tag)
        if p.get("sha256") and not SHA_RE.match(str(p["sha256"])):
            raise IndexError_("%s: sha256 is not 64 hex digits" % tag)
    ids = {p["id"].lower() for p in plugins}
    for p in plugins:
        for d in p.get("dependencies", []) if isinstance(p.get("dependencies"), list) else []:
            if isinstance(d, str) and d.split("@", 1)[0].lower() not in ids:
                warnings.append("%s depends on %r which is not in this index" % (p["id"], d))
    return plugins, warnings


def e(s):
    return html.escape(str(s), quote=True)


def safe_link(url, label=None):
    if isinstance(url, str) and re.match(r"^https?://[^\s\"'<>]+$", url):
        return '<a href="%s" rel="noopener noreferrer nofollow">%s</a>' % (e(url), e(label or url))
    return e(url) if url else ""


def page(title, body, root_rel, active):
    nav = [("index.html", "Plug-ins"), ("community.html", "Community")]
    links = "".join(
        '<a href="%s%s"%s>%s</a>' % (root_rel, href, ' aria-current="page"' if href == active else "", e(label))
        for href, label in nav)
    return """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="%s">
<meta name="referrer" content="no-referrer">
<title>%s</title>
<link rel="stylesheet" href="%sstyle.css">
</head><body>
<header><strong>Dino 8</strong> plug-ins <nav>%s</nav></header>
<main>
%s
</main>
<footer>Static page generated by tools/site/build_site.py. No cookies, no trackers, no external requests.</footer>
<script src="%ssite.js"></script>
</body></html>
""" % (CSP, e(title), root_rel, links, body, root_rel)


def card(p):
    tags = "".join('<span class="tag">%s</span>' % e(t) for t in p.get("tags", []) if isinstance(t, str))
    haystack = " ".join(str(x) for x in [p["id"], p["name"], p.get("author", ""), p.get("description", "")] +
                        [t for t in p.get("tags", []) if isinstance(t, str)]).lower()
    api = p.get("api_version")
    return ('<article class="card" data-search="%s">'
            '<h2><a href="plugin/%s.html">%s</a> <small>%s</small></h2>'
            '<p>%s</p><p class="meta">%s%s</p><p>%s</p></article>') % (
        e(haystack), e(p["id"]), e(p["name"]), e(p["version"]), e(p.get("description", "")),
        ("by " + e(p["author"]) + " &middot; ") if p.get("author") else "",
        ("plug-in API v%s" % e(api)) if api else "plug-in API unknown", tags)


def plugin_page(p, index_url_hint):
    rows = []
    def row(k, v):
        if v not in (None, "", []):
            rows.append("<tr><th>%s</th><td>%s</td></tr>" % (e(k), v))
    row("Version", e(p["version"]))
    row("Author", e(p.get("author", "")))
    row("Plug-in API", ("v%s" % e(p["api_version"])) if p.get("api_version") else "")
    row("Needs Dino 8", e(p.get("min_app_version", "")))
    row("Homepage", safe_link(p.get("homepage", "")))
    row("Depends on", ", ".join(e(d) for d in p.get("dependencies", []) if isinstance(d, str)))
    if p.get("download_url"):
        row("Download", safe_link(p["download_url"]))
        row("SHA-256", "<code>%s</code>" % e(p.get("sha256", "")) if p.get("sha256") else "<em>not published</em>")
    else:
        row("Ships with", "Dino 8 (<code>%s</code>)" % e(p["bundled_path"]))
    body = """<p><a href="../index.html">&larr; all plug-ins</a></p>
<h1>%s <small>%s</small></h1>
<p>%s</p>
<table class="facts">%s</table>
<h2>Install in Dino 8</h2>
<pre>PluginMarketplaceIndex %s
PluginMarketplaceInstall %s</pre>
<p>Or open <em>Window &gt; Plug-in Marketplace</em>, load the index URL above, and click Install.
Check the copy on disk any time with <code>PluginMarketplaceVerify %s</code>.</p>
""" % (e(p["name"]), e(p["version"]), e(p.get("description", "")), "".join(rows), e(index_url_hint), e(p["id"]), e(p["id"]))
    return page(p["name"] + " - Dino 8 plug-ins", body, "../", "")


def community_page(discussions_url, site_url):
    if discussions_url:
        d = "<p>Discussion board: %s</p>" % safe_link(discussions_url)
    else:
        d = ("<p>No discussion board is linked yet. The site owner can pass <code>--discussions-url</code> "
             "when generating this site; see docs/COMMUNITY.md.</p>")
    body = """<h1>Community</h1>
%s
<h2>Publishing a plug-in</h2>
<ol>
<li>Start from a template: <code>python3 plugin-templates/new_plugin.py command --out .</code></li>
<li>Build it against the SDK, package it with <code>plugin-templates/package_plugin.py</code>.</li>
<li>Host the library, add the generated entry to a plug-in index, and open a listing request (see CONTRIBUTING-PLUGINS.md).</li>
</ol>
<p>Everything on this site is generated from one JSON file, <a href="plugin-index.json">plugin-index.json</a>%s.</p>
""" % (d, (" (" + e(site_url + "plugin-index.json") + ")") if site_url else "")
    return page("Community - Dino 8 plug-ins", body, "", "community.html")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--index", required=True)
    ap.add_argument("--out")
    ap.add_argument("--title", default=None)
    ap.add_argument("--site-url", default="", help="public base URL of the hosted site (http(s)://..., used in install instructions)")
    ap.add_argument("--discussions-url", default="")
    ap.add_argument("--check-only", action="store_true", help="validate the index and exit")
    args = ap.parse_args(argv)
    if not args.check_only and not args.out:
        ap.error("--out is required unless --check-only")
    if args.site_url and not re.match(r"^https?://", args.site_url):
        ap.error("--site-url must be http:// or https://")
    if args.site_url and not args.site_url.endswith("/"):
        args.site_url += "/"
    if args.discussions_url and not re.match(r"^https?://", args.discussions_url):
        ap.error("--discussions-url must be http:// or https://")

    with open(args.index, "rb") as f:
        raw = f.read()
    try:
        root = json.loads(raw.decode("utf-8"))
        plugins, warnings = validate(root)
    except (ValueError, IndexError_) as ex:
        sys.stderr.write("build_site: invalid index %s: %s\n" % (args.index, ex))
        return 2
    for w in warnings:
        sys.stderr.write("build_site: warning: %s\n" % w)
    if args.check_only:
        print("build_site: %s is valid (%d plug-in(s), %d warning(s))" % (args.index, len(plugins), len(warnings)))
        return 0

    title = args.title or root.get("index_name") or "Dino 8 plug-ins"
    hint = (args.site_url + "plugin-index.json") if args.site_url else "<where you host this site>/plugin-index.json"
    out = args.out
    os.makedirs(os.path.join(out, "plugin"), exist_ok=True)
    with open(os.path.join(out, "plugin-index.json"), "wb") as f:
        f.write(raw)
    for name in ("style.css", "site.js"):
        shutil.copyfile(os.path.join(HERE, "static", name), os.path.join(out, name))
    updated = root.get("updated")
    body = ("<h1>%s</h1>%s<p><input id=\"q\" type=\"search\" placeholder=\"Filter by name, author, tag...\" aria-label=\"Filter plug-ins\"> "
            "<span id=\"count\">%d plug-in(s)</span></p>\n<section id=\"list\">%s</section>"
            "<p>Load this index in Dino 8: <code>PluginMarketplaceIndex %s</code></p>") % (
        e(title), ("<p>Index updated %s.</p>" % e(updated)) if isinstance(updated, str) and updated else "",
        len(plugins), "\n".join(card(p) for p in plugins), e(hint))
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(page(title, body, "", "index.html"))
    with open(os.path.join(out, "community.html"), "w", encoding="utf-8") as f:
        f.write(community_page(args.discussions_url, args.site_url))
    for p in plugins:
        with open(os.path.join(out, "plugin", p["id"] + ".html"), "w", encoding="utf-8") as f:
            f.write(plugin_page(p, hint))
    print("build_site: wrote %d plug-in page(s) + index/community/plugin-index.json to %s" % (len(plugins), out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
