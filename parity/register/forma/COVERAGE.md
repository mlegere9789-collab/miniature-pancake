# Autodesk Forma: Parity Register coverage

Rows: 186 (136 original plus 50 added in the verification pass; see VERIFY.md), ids D8-FORMA-<DOMAIN>-<nnnn>, one JSONL file per domain, each row validated against the Register row schema (draft-07). All rows start at tier None, status open.

| File | Domain code | Rows | Covers |
|---|---|---|---|
| site.jsonl | SITE | 21 | Site Design project setup, terrain, context, proposals, drawing tools, constraints, export |
| analysis.jsonl | ANALYSIS | 41 | Sun and shadow, daylight potential, wind (rapid and detailed, comfort scales), microclimate, noise, solar energy, operational energy, program and area, views, regulatory checks, cost, overlay, refresh, export, transparency, custom analysis |
| buildingdesign.jsonl | BLDG | 17 | Building Design massing, levels, facade, floor plan, LOD 200, Revit handoff, Connected Client |
| board.jsonl | BOARD | 16 | Forma Board |
| datamanagement.jsonl | DATA | 22 | Data Management (common data environment) |
| carbon.jsonl | CARBON | 8 | Carbon Insights, total/embodied/operational carbon |
| streetdesign.jsonl | STREET | 5 | Street Design (beta) |
| interop.jsonl | INTEROP | 8 | Data Exchanges, connectors, datasets, viewer formats, Revit handoff, BCF |
| layoutexplorer.jsonl | LAYOUT | 5 | Building Layout Explorer |
| marketplace.jsonl | MARKET | 6 | Data Marketplace and context data |
| extensions.jsonl | EXT | 37 | Extensions, embedded view SDK, HTTP API, authentication, MCP and AI agents, bridges |

Factories used: collab-cloud 74, analysis-site 73, interop-migration 22, simulation 17.

## Method and limits (clean room)

- Verification pass of 2026-11-14 (details and counts in VERIFY.md): rows were checked against Autodesk Forma help pages on help.autodesk.com (Forma for Design and Forma Data Management help), the APS Forma v1 developer documentation, and the type declarations of the public Apache-2.0 npm package forma-embedded-view-sdk 0.96.0 read from the registry. Every row note now starts with VERIFIED, PARTIAL or UNVERIFIED; added rows carry no tag and cite the page they were written from.
- Several original source hosts (www.autodesk.com, forums.autodesk.com, www.npmjs.com, blogs.autodesk.com, adsknews.autodesk.com, trade press) could not be read from the verification environment, so rows that depended on them are marked UNVERIFIED with the original URL kept. The Site Design analysis, Street Design, Layout Explorer and Carbon Insights rows are mostly in this state.
- Statements are in our own words. No competitor code or non-public material was read.
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
