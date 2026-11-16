# Forma register: verification pass (VERIFY)

Date of pass: 2026-11-14. Scope: the 136 rows in `parity/register/forma/*.jsonl` that existed before this pass, plus rows added in it.

## Result in one paragraph

Every row was checked against the public Autodesk pages that this environment could actually open. Only two Autodesk hosts were readable (`help.autodesk.com` and `aps.autodesk.com`), plus the npm registry for the public SDK package. Rows whose behaviour those pages describe were corrected and re-sourced; rows that they only partly describe were re-sourced to the page that does support the confirmed part; rows that depend on the blocked pages (the Site Design analysis, Street Design, Layout Explorer and Carbon Insights blog and tutorial sources) were **left unverified**, not rejected. Nothing was rejected, because an absent page is not proof that a feature does not exist.

## Counts

| Measure | Count |
|---|---|
| Rows present before the pass | 136 |
| Rows checked (every one attempted; source opened or shown unreachable) | 136 |
| Verified: feature confirmed on a reachable page, `source.doc` and `source.ref` replaced | 59 |
| Partly verified: part confirmed and re-sourced, rest marked unverified in notes | 39 |
| Left unverified: original source unreachable and no reachable page covers it (original URL kept) | 38 |
| Statements corrected (text changed to match the page) | 49 |
| Rejected (feature shown not to exist) | 0 |
| Rows added | 50 |
| Rows now in the register | 186 |
| Schema errors / duplicate ids | 0 / 0 |

Note on the "18 confirm rows" in the brief: `COVERAGE.md` lists 17 ids and a search of the notes for the words finds the same 17. The 18th could not be identified; if it exists it is not distinguishable by those words.

### The 17 rows that said unverified or confirm

| Row | Outcome |
|---|---|
| SITE-0010 | left unverified |
| SITE-0013 | resolved (verified) |
| SITE-0015 | left unverified |
| STREET-0001 | left unverified |
| STREET-0003 | left unverified |
| ANALYSIS-0007 | left unverified |
| ANALYSIS-0008 | left unverified |
| ANALYSIS-0014 | left unverified |
| ANALYSIS-0023 | left unverified |
| ANALYSIS-0026 | left unverified |
| ANALYSIS-0030 | partly resolved |
| ANALYSIS-0032 | left unverified |
| BOARD-0006 | resolved (verified) |
| EXT-0015 | partly resolved |
| BLDG-0006 | left unverified |
| BLDG-0008 | resolved (verified) |
| BLDG-0012 | left unverified |

## Row tags

Every pre-existing row has its notes start with one of three tags, so the state is greppable:

- `VERIFIED:` the feature is on a reachable page; `source.doc` is that page.
- `PARTIAL:` the notes say what was confirmed and what was not; `source.doc` is the page for the confirmed part.
- `UNVERIFIED:` the original URL is kept and the notes give the reason it could not be read. Nothing in these rows should be treated as confirmed.
- Old free-text notes were kept after `Prior note (not re-checked):` except where the pass resolved them. Several of those prior notes cite a "digest" that is not identified anywhere and could not be checked.

Verified (59): ANALYSIS-0013, ANALYSIS-0028, ANALYSIS-0031, ANALYSIS-0037, BOARD-0001, BOARD-0002, BOARD-0004, BOARD-0005, BOARD-0006, BOARD-0007, BLDG-0001, BLDG-0002, BLDG-0004, BLDG-0005, BLDG-0007, BLDG-0008, BLDG-0009, BLDG-0010, DATA-0001, DATA-0002, DATA-0003, DATA-0004, DATA-0005, DATA-0006, DATA-0007, DATA-0008, DATA-0009, DATA-0010, EXT-0001, EXT-0002, EXT-0003, EXT-0004, EXT-0005, EXT-0006, EXT-0007, EXT-0008, EXT-0009, EXT-0010, EXT-0011, EXT-0012, EXT-0013, EXT-0014, EXT-0016, EXT-0017, EXT-0018, EXT-0019, EXT-0020, EXT-0021, EXT-0022, EXT-0023, EXT-0024, EXT-0025, MARKET-0001, SITE-0001, SITE-0002, SITE-0005, SITE-0006, SITE-0009, SITE-0013

Partly verified (39): ANALYSIS-0001, ANALYSIS-0002, ANALYSIS-0003, ANALYSIS-0004, ANALYSIS-0006, ANALYSIS-0009, ANALYSIS-0011, ANALYSIS-0012, ANALYSIS-0015, ANALYSIS-0016, ANALYSIS-0017, ANALYSIS-0018, ANALYSIS-0019, ANALYSIS-0020, ANALYSIS-0022, ANALYSIS-0029, ANALYSIS-0030, ANALYSIS-0033, ANALYSIS-0035, ANALYSIS-0038, ANALYSIS-0039, BOARD-0003, BLDG-0003, BLDG-0011, BLDG-0013, BLDG-0014, CARBON-0001, CARBON-0002, EXT-0015, MARKET-0002, MARKET-0004, MARKET-0005, MARKET-0006, SITE-0003, SITE-0004, SITE-0007, SITE-0008, SITE-0011, SITE-0012

Unverified (38): ANALYSIS-0005, ANALYSIS-0007, ANALYSIS-0008, ANALYSIS-0010, ANALYSIS-0014, ANALYSIS-0021, ANALYSIS-0023, ANALYSIS-0024, ANALYSIS-0025, ANALYSIS-0026, ANALYSIS-0027, ANALYSIS-0032, ANALYSIS-0034, ANALYSIS-0036, BLDG-0006, BLDG-0012, CARBON-0003, CARBON-0004, CARBON-0005, CARBON-0006, CARBON-0007, CARBON-0008, LAYOUT-0001, LAYOUT-0002, LAYOUT-0003, LAYOUT-0004, LAYOUT-0005, MARKET-0003, SITE-0010, SITE-0014, SITE-0015, SITE-0016, SITE-0017, STREET-0001, STREET-0002, STREET-0003, STREET-0004, STREET-0005

Statements corrected (49): ANALYSIS-0001, ANALYSIS-0013, ANALYSIS-0028, ANALYSIS-0031, ANALYSIS-0037, ANALYSIS-0039, BLDG-0001, BLDG-0002, BLDG-0004, BLDG-0005, BLDG-0007, BLDG-0008, BLDG-0009, BLDG-0010, BOARD-0001, BOARD-0002, BOARD-0004, BOARD-0005, BOARD-0006, BOARD-0007, DATA-0001, DATA-0002, DATA-0003, DATA-0004, DATA-0005, DATA-0006, DATA-0007, DATA-0008, DATA-0009, DATA-0010, EXT-0001, EXT-0002, EXT-0005, EXT-0006, EXT-0008, EXT-0010, EXT-0011, EXT-0013, EXT-0014, EXT-0015, EXT-0016, EXT-0020, MARKET-0001, SITE-0001, SITE-0002, SITE-0005, SITE-0006, SITE-0009, SITE-0013

Rows whose statement was corrected keep their old acceptance sketch; those sketches were not reviewed and should be re-read before tests are frozen (known cases: ANALYSIS-0039, ANALYSIS-0028, SITE-0001, SITE-0002, SITE-0005, EXT-0006).

## Rejected

None. No reachable page shows that any row's feature does not exist. Where reachable pages are silent about a feature (for example Street Design, Building Layout Explorer, detailed wind, operational energy, cost analysis) the row stays and is tagged `UNVERIFIED`. Two rows came closest to rejection and were instead narrowed: EXT-0015 (the reachable pages show an assistant using MCP tools for project administration, not Forma exposing its design model to external agents) and DATA-0010 (offline work is documented for the mobile apps only).

## Added rows (50)

All added rows come from a page that was read in this pass, and each cites that page. Ids: BOARD-0008, BOARD-0009, BOARD-0010, BOARD-0011, BOARD-0012, BOARD-0013, BOARD-0014, BOARD-0015, BOARD-0016, SITE-0018, SITE-0019, SITE-0020, SITE-0021, BLDG-0015, BLDG-0016, BLDG-0017, INTEROP-0001, INTEROP-0002, INTEROP-0003, INTEROP-0004, INTEROP-0005, INTEROP-0006, INTEROP-0007, INTEROP-0008, DATA-0011, DATA-0012, DATA-0013, DATA-0014, DATA-0015, DATA-0016, DATA-0017, DATA-0018, DATA-0019, DATA-0020, DATA-0021, DATA-0022, EXT-0026, EXT-0027, EXT-0028, EXT-0029, EXT-0030, EXT-0031, EXT-0032, EXT-0033, EXT-0034, EXT-0035, EXT-0036, EXT-0037, ANALYSIS-0040, ANALYSIS-0041.

A new file `interop.jsonl` (domain `interop`, code INTEROP) holds Data Exchange, connector, dataset, viewer-format, Revit handoff and BCF rows. Other additions extend Board, Site, Building Design, Data Management, Extensions and Analysis.

Areas the brief asked for that stay thin because no reachable page covers them: wind microclimate and noise detail, solar energy and PV, operational energy, embodied-carbon databases, Street Design, Layout Explorer, Grasshopper link behaviour (only an announced "coming soon" connector is documented), the extensions marketplace beyond the publishing process, and Forma Data Exchange SDK details.

## Unreachable pages

Probed from this session with `curl` through the pre-configured proxy. Nothing was bypassed: the bot-challenge and proxy denials were respected and no alternative route was tried.

| Host | Result | Rows it was the cited source for |
|---|---|---|
| www.autodesk.com (products, learn, campaigns, blog) | HTTP 403, Akamai bot block | 15 original citations |
| forums.autodesk.com | HTTP 403, Cloudflare challenge | EXT-0005, EXT-0006 original citations (replaced) |
| www.npmjs.com | HTTP 403, Cloudflare challenge | SDK rows (replaced by the registry JSON) |
| blogs.autodesk.com | proxy denies the CONNECT (403) | original citations for site, carbon, market rows |
| adsknews.autodesk.com | proxy denies the CONNECT (403) | original citations for board, data, layout rows |
| aecmag.com, architosh.com, resources.imaginit.com | proxy denies the CONNECT | Building Design and Street Design original citations |
| help.autodeskforma.com, app.autodeskforma.com (SDK reference docs), forma.autodesk.com, unpkg.com, github.com | proxy denial or no route | pages that the APS docs and SDK comments link to; not read |
| help.autodesk.com/view/FORMA, /view/FRMA, /view/FORMA-SITE | HTTP 404 | do not exist; the real product code is SPCMKR (Forma for Design) and DOCS (Forma Data Management) |

The brief said the environment could reach `help.autodesk.com`, `aps.autodesk.com`, `forums.autodesk.com` and `www.autodesk.com`. In practice the first two were reachable and the last two were not.

Final original URLs still cited by unverified rows (none of them could be opened):

- https://adsknews.autodesk.com/en/news/autodesk-design-and-make-intelligence/
- https://aecmag.com/civil-engineering/autodesk-pushes-forma-upstream-and-connects-civil-3d/
- https://architosh.com/2026/04/autodesk-intros-new-forma-building-design/
- https://blogs.autodesk.com/forma/2023/12/11/site-context-in-revit-via-forma
- https://blogs.autodesk.com/forma/2026/04/07/total-carbon-analysis-in-forma-building-design-a-step-by-step-guide/
- https://blogs.autodesk.com/forma/2026/07/17/introducing-total-carbon-analysis-in-forma-site-design-understand-embodied-operational-and-total-carbon-from-the-earliest-stage-of-design/
- https://blogs.autodesk.com/forma/2026/08/28/forma-site-design-tools-overview/
- https://www.autodesk.com/campaigns/forma-street-design
- https://www.autodesk.com/learn/ondemand/tutorial/analysis-in-forma
- https://www.autodesk.com/learn/ondemand/tutorial/sun-wind-and-temperature-analysis-for-residential-areas
- https://www.autodesk.com/learn/ondemand/tutorial/wind-analysis-in-forma
- https://www.autodesk.com/products/forma-site-design/environmental-impact-analysis
- https://www.autodesk.com/products/forma-site-design/overview

## Method

1. Located the live help structure. `help.autodesk.com/view/SPCMKR/ENU/` (Forma for Design: Site Design, Board, Building Design, Assistant) and `/view/DOCS/ENU/` (Forma Data Management, about 650 topics) are script-driven sites; their `data/toctree.json` files list the topic pages, which are plain HTML under `/cloudhelp/ENU/...`. All SPCMKR and DOCS topics (about 1,400 including the FDME and COLLAB trees) were downloaded and reduced to text for searching.
2. Downloaded the APS Forma v1 documentation (23 pages, via the markdown alternate the site advertises), listed in full from the overview, embedded-view, HTTP reference and element-system sections.
3. Read the public Apache-2.0 npm package `forma-embedded-view-sdk` 0.96.0 from the registry tarball and used its type declarations (modules for analysis, predictive analysis, area metrics, elements, geometry, terrain, proposal, library, storage, geodata, camera, sun, render, selection, settings, project and generators) as the oracle for SDK rows. The row citation is `https://registry.npmjs.org/forma-embedded-view-sdk/0.96.0` because the SDK reference site is not reachable.
4. Matched each row to the pages by feature, rewrote the statement where the page said something different, and replaced `source.doc` and `source.ref` only with pages that were opened and contain the claim. Every replacement URL was then fetched again; all 59 distinct replacement URLs returned HTTP 200.
5. Wrote new rows only from pages read in step 1 to 3, in my own words, one sentence each, with an acceptance sketch using public data or contract tests.
6. Validated with Python `jsonschema` (Draft 7). `parity/schema/row.schema.json` does not exist in this branch, so the fallback in the brief was used: id pattern `^D8-[A-Z0-9]{2,8}-[A-Z0-9]{2,8}-[0-9]{4}$`, required `id`, `program`, `statement`, `source{kind,ref,doc}`, `target` (Strong or Best), `factory`, `domain`, `status` (open, in_progress or verified); optional fields free. Result: 186 rows, 0 schema errors, 0 duplicate ids. Also checked: no banned word, one sentence per statement, JSONL round trips.

## Limits that readers must keep in mind

- A "VERIFIED" tag means the cited page describes the feature, not that it was seen running. No trial account was used.
- Statements were read from the help text; several details are descriptions of the application as documented on 2026-11-14 and Building Design is documented as a closed beta.
- The reachable Site Design help is thin (three articles). Most of the analysis behaviour in the register is therefore only known through the SDK declarations or the unreachable sources, which is why 77 rows stay unverified or partial.
