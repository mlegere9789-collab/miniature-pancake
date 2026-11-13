# Autodesk Forma: Parity Register coverage

Rows: 127, ids D8-FORMA-<DOMAIN>-<nnnn>, one JSONL file per domain, each row validated against the Register row schema (draft-07). All rows start at tier None, status open.

| File | Domain code | Rows | Covers |
|---|---|---|---|
| site.jsonl | SITE | 17 | Site Design project setup, terrain, context, proposals, drawing tools, constraints, export |
| analysis.jsonl | ANALYSIS | 39 | Sun and shadow, daylight potential, wind (rapid and detailed, comfort scales), microclimate, noise, solar energy, operational energy, program and area, views, regulatory checks, cost, overlay, refresh, export, transparency, custom analysis |
| buildingdesign.jsonl | BLDG | 14 | Building Design massing, levels, facade, floor plan, LOD 200, Revit handoff, Connected Client |
| board.jsonl | BOARD | 7 | Forma Board |
| datamanagement.jsonl | DATA | 10 | Data Management (common data environment) |
| carbon.jsonl | CARBON | 8 | Carbon Insights, total/embodied/operational carbon |
| streetdesign.jsonl | STREET | 5 | Street Design (beta) |
| layoutexplorer.jsonl | LAYOUT | 5 | Building Layout Explorer |
| marketplace.jsonl | MARKET | 6 | Data Marketplace and context data |
| extensions.jsonl | EXT | 16 | Extensions, embedded view SDK, HTTP API, authentication, MCP and AI agents, bridges |

Factories used: analysis-site 64, collab-cloud 34, simulation 16, interop-migration 13.

## Method and limits (clean room)

- Public sources only: Autodesk Forma learning pages and blog posts, the Autodesk Platform Services Forma v1 documentation index and its published SDK package page, the public developer forum, trade press, plus open standards and datasets named in the acceptance sketches. No competitor code or non-public material was read. Statements are in our own words.
- In this session direct page fetches were blocked (hosts such as www.autodesk.com and aps.autodesk.com were not resolvable), so facts were taken from web-search result summaries of those public pages, and the URL in each row is the page the summary pointed to. Pages were not read in full. Rows whose behavior could not be confirmed carry the words "unverified" or "confirm" in notes. Those rows need a pass with a trial account or a page read before acceptance tests are frozen: D8-FORMA-ANALYSIS-0007, D8-FORMA-ANALYSIS-0008, D8-FORMA-ANALYSIS-0013, D8-FORMA-ANALYSIS-0014, D8-FORMA-ANALYSIS-0023, D8-FORMA-ANALYSIS-0026, D8-FORMA-ANALYSIS-0030, D8-FORMA-ANALYSIS-0032, D8-FORMA-BOARD-0006, D8-FORMA-BLDG-0006, D8-FORMA-BLDG-0008, D8-FORMA-BLDG-0012, D8-FORMA-EXT-0015, D8-FORMA-SITE-0010, D8-FORMA-SITE-0013, D8-FORMA-SITE-0015, D8-FORMA-STREET-0001, D8-FORMA-STREET-0003.
- Forma's internal solver settings (mesh, turbulence model, ML model details) are not public. Rows are defined by outcome and measurable accuracy against public references, never by copying a method.
- Where a row has "key=<name>" in notes, it is a stable label used by the generator, not product wording.

## Validation-dataset families used in acceptance sketches

| Analysis | Oracle (public) |
|---|---|
| Sun and shadow | NREL Solar Position Algorithm published sample (Reda and Andreas, NREL/TP-560-34302), NOAA solar equations, box-shadow projective closed form |
| Daylight potential | Closed-form vertical sky component under the CIE overcast sky, BRE 209 worked examples, CIE 171:2006 validation cases, split-flux daylight factor |
| Wind | AIJ pedestrian-wind benchmark data (Tominaga et al.), COST Action 732 Michelstadt and CEDVAL data, surface-mounted cube experiments (Martinuzzi and Tropea), log-law channel closed form, Lawson and NEN 8100 threshold tables |
| Microclimate | UTCI reference procedure and tables (Brode et al.), ISO 7726 mean radiant temperature formulas |
| Noise | ISO 9613-2 divergence and screen formulas, CNOSSOS-EU (Directive 2015/996) emission and propagation, ISO/TR 17534-3 test cases |
| Solar energy | Clear-sky models, NREL TMY3 and EnergyPlus EPW weather, transposition models, PVWatts method |
| Operational energy | ANSI/ASHRAE 140 (BESTEST) cases, steady-state UA closed form, published grid carbon factors |
| Embodied carbon | EN 15978 stage structure, University of Bath ICE v3 factors, EC3 open data (check terms) |
| Program and area | Shoelace and extrusion closed forms, IPMS definitions |
| Views | Isovist closed forms (Benedikt), solid-angle closed form |
| Regulatory | Envelope and height-plane constructions with known violators, rule-pack schema validation |
| Interop and data | buildingSMART IFC4 samples, glTF Khronos samples, OGC CityGML samples, RFC 7946, RFC 6749 and 7636 vectors, OpenAPI contract tests |

Standards that are sold (ISO, EN, CIE, ASHRAE) cannot be copied into the repository; tests use the closed forms or small hand-computed tables, and the licensed text is only a cross-check by the owner.

## Not covered and why

- Autodesk construction products (Docs, Build, BIM Collaborate, Takeoff) are outside the Forma design scope of this job.
- Pricing, tiers and account administration are commercial packaging, represented only by feature-flag rows.
- The owner's trial-account walk-through (plan 4.3 completeness check) has not been done; that is the next step for the unverified rows.
