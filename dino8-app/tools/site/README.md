# tools/site - static plug-in marketplace and community site

```sh
python3 build_site.py --index ../../plugin-index/index.json --out public \
    [--title T] [--site-url https://host/] [--discussions-url https://...] [--check-only]
```

Input: the plug-in index JSON the app reads (`../../plugin-index/SCHEMA.md`). Output (`public/`):
`index.html` (search box, one card per plug-in), `plugin/<id>.html`, `community.html`, `style.css`,
`site.js` (the card filter, ~20 lines) and `plugin-index.json` (a verbatim copy of the input - the URL to
give to `PluginMarketplaceIndex`).

Design constraints (all checked by `tests/plugin_site_test.sh`): Python standard library only; no CDN, font,
analytics or tracker; no forms; a `Content-Security-Policy` meta tag allowing only same-origin CSS/JS; every
value from the index HTML-escaped and only `http(s)` URLs turned into links; ids restricted to
`[A-Za-z0-9._-]` so they cannot escape `--out`; invalid indexes (same rules as `plugins::ParseIndex`, plus
duplicate ids, non-http(s) downloads and malformed `sha256`; plain http and a missing `sha256` only warn) are refused before anything is written.
Hosting is up to you: the output is plain static files.
