# Independent review: Archicad register (`parity/register/archicad/*.jsonl`)

Reviewer: independent pass, not the author of the rows. Date of review: 2026-11-14. Rows in scope: 924.
Verdict: **ACCEPT WITH FIXES** (conditions at the end; two of them are blocking for any scoring that counts these rows).

## 1. Sample and method

- Sample: `random.Random(8)` over the sorted ids, 150 rows, then every domain file topped up to at least 10 sampled rows. Final sample **224 rows** across 21 domain files, **193 distinct `source.doc` pages**. Ids are in `sample_ids_archicad.txt`; `sample_method.py` regenerates them.
- Every cited page was downloaded and read. Statements were checked term by term against the page, then the doubtful ones were opened and read in context. The pypi page is JavaScript-gated for my client, so its package JSON endpoint was used instead.
- Criteria are the same as for Rhino: (a) page reachable and relevant, (b) statement matches the page without copying, (c) target, (d) factory/domain, (e) testable outcome.
- Limits: one reviewer, one pass; (b) is judged against the single cited page; (c) and (d) contain judgment. Hosts `archicadapi.graphisoft.com`, `gdl.graphisoft.com` and `github.com` refused my requests (403 from the proxy), so rows citing them could not be checked by me.

Sample composition:

| Domain | Rows in register | Rows in sample |
|---|---|---|
| analysis | 32 | 10 |
| api | 42 | 10 |
| attrib | 53 | 10 |
| cad2d | 21 | 10 |
| edit | 51 | 10 |
| gdl | 47 | 10 |
| graphic | 45 | 10 |
| hotlink | 28 | 10 |
| ifc | 62 | 10 |
| mep | 34 | 10 |
| nav | 54 | 12 |
| new2930 | 26 | 10 |
| props | 46 | 11 |
| reno | 26 | 10 |
| sched | 36 | 10 |
| seo | 28 | 10 |
| struct | 22 | 10 |
| team | 56 | 10 |
| toolbox | 168 | 21 |
| ui | 23 | 10 |
| viz | 24 | 10 |

## 2. Results

| Criterion | Defective rows | Ids |
|---|---|---|
| (a) page unreachable / empty / does not exist | 12 | API-0003, API-0013, GDL-0004, GDL-0014, GRAPHIC-0011, NEW2930-0017, RENO-0009, RENO-0017, RENO-0019, UI-0009, VIZ-0013, PROPS-0028 |
| (b) statement not supported by the page (or the row's own note says the feature is not documented) | 25 | API-0027, GDL-0034, HOTLINK-0012, HOTLINK-0018, IFC-0033, MEP-0009, NEW2930-0003, PROPS-0025, RENO-0001, RENO-0009, RENO-0017, RENO-0019, SCHED-0036, SEO-0017, STRUCT-0006, STRUCT-0007, STRUCT-0012, TEAM-0024, TOOLBOX-0013, TOOLBOX-0026, TOOLBOX-0111, UI-0007, UI-0009, VIZ-0001, VIZ-0003 |
| (b) copied text | 0 | no verbatim run of 9 or more words against the cited page |
| (c) `target: Best` not justified | 3 of 6 Best rows | MEP-0004, NAV-0002, SEO-0004 |
| (d) wrong `factory` or `domain` | 8 | ATTRIB-0037, HOTLINK-0026, NAV-0041, NAV-0042, NEW2930-0007, NEW2930-0008, NEW2930-0017, NEW2930-0018 |
| (e) label or fragment, not a testable outcome | 10 | API-0003, API-0013, API-0027, GRAPHIC-0011, IFC-0022, IFC-0025, NEW2930-0017, VIZ-0003, HOTLINK-0012, NAV-0019 |
| extra: duplicate or overlapping rows | 10 | HOTLINK-0012, HOTLINK-0018, HOTLINK-0007, HOTLINK-0015, IFC-0047, HOTLINK-0026, CAD2D-0002, CAD2D-0014, CAD2D-0015, UI-0009 |

- **Content defect rate: 43/224 = 19.2%** (Wilson 95%: 14.6% to 24.9%).
- **All-criteria defect rate: 46/224 = 20.5%** (15.8% to 26.3%).
- Sampled rows self-labelled VERIFIED, CORRECTED or ADDED+VERIFIED: 205. Of those, 14 (6.8%) contain a claim I could not find on the cited page or cite an empty page: GDL-0034, HOTLINK-0012, HOTLINK-0018, IFC-0033, PROPS-0028, RENO-0001, SCHED-0036, SEO-0017, STRUCT-0007, TOOLBOX-0013, TOOLBOX-0026, UI-0007, VIZ-0001, VIZ-0003. So the second-pass verification in `VERIFY.md` is real but not complete.
- Unreachable (a): 4 rows cite hosts my client could not reach; 7 rows cite `help.graphisoft.com/AC/29/INT/_AC29_Help/`, which returns 404; PROPS-0028 cites a real page whose body is empty apart from a one-line note.

Selected confirmed defects (page read; proposed fixes in `fixes_archicad.jsonl`):

- **HOTLINK-0012** says missing files are reported and a cached copy is kept; its cited page (About Hotlink Modules) has no such text, and HOTLINK-0018 repeats the idea from the Module Manager page, which only says a missing source blocks updates until relinked. The "cached copy" behavior is not stated on either page.
- **IFC-0033** claims issue views store camera position, cutting planes and a snapshot; the cited page describes name, status, priority, type, description and highlighting only.
- **TOOLBOX-0026** adds curved hole edges, edge angle and surface; the page says only "draw a contour in the slab". **STRUCT-0007** adds contour editing (offset, turn, drag, rotate, mirror) that lives on another page.
- **SEO-0017** adds junction order, not on the cited page. **SCHED-0036** adds BIMx output, not on the cited page. **VIZ-0003** (GI algorithm, sample counts, cache) and **UI-0007** (command layout schemes) describe settings the cited pages do not contain.
- **MEP-0004 (Best)**: the statement is about arrow-key stepping through connection paths, the superiority sentence is about cost-optimal routing, so the Best claim is for a different feature.

## 3. Systematic problems

1. **Rows that the register's own notes reject are still rows.** 36 rows (3.9%) have notes starting `REJECTED:`, saying the Archicad 30 Help does not document the feature (dormers, timber/CLT, rebar, bSDD, scheduled publishing, global command search, MEP pressure-drop, etc.). Their statement, `target: Strong` and `status: open` are unchanged, so a scorer would count them as requirements for features the vendor does not offer. 11 of them are in the sample. Another 52 rows (5.6%) are `UNVERIFIED`. Together 88 rows (9.5%) carry no page evidence by their own admission.
2. **Dead or non-page sources.** 28 rows cite `help.graphisoft.com/AC/29/INT/_AC29_Help/` (404), 18 the JSON interface landing page, 12 `gdl.graphisoft.com`, 12 the pypi project page, 3 a github.io page and 1 an AEC Magazine article. 74 rows (8%) therefore point at a landing page, a news article, an unreachable site or a package page rather than a help topic page.
3. **`new2930` is a release grouping, not a function.** All 26 rows are `bim-arch`, but they include MCP/AI access (scripting-api-ai), the Redshift renderer (render-anim), library-loading speed (performance) and live links to visualisation tools (interop). The domain also duplicates content that exists in functional domains (viewpoint folders overlap NAV rows; NEW2930-0012 overlaps NEW2930-0001 on one page).
4. **Duplicates and overlaps kept in.** The notes admit them: UI-0015 (dup of UI-0005), UI-0009 (dup of UI-0003), CAD2D-0014 and CAD2D-0015 ("duplicate as written"), HOTLINK-0007/-0015, -0010/-0016, -0012/-0018, IFC-0023 vs HOTLINK-0017. TEAM-0022 and TEAM-0035 are the same sentence reworded (token overlap 0.90). IFC-0047 and HOTLINK-0026 both describe point-cloud import from the same page. Without `alias_of` these will be scored twice.
5. **`cad-2d` and `flow` are never used.** The schema has a `cad-2d` factory (wave 1) but all 21 `cad2d` rows (splines, polylines, text editing, dimensions) are `drafting-docs` (wave 2). Several are pure 2D drafting (CAD2D-0002, -0003, -0014).
6. **Terse fragments.** 93 of 924 statements (10%) are under 75 characters; 10 in the sample are fragments such as "Query by type and the current selection." or "Direct live links to visualisation tools." with no checkable outcome (see (e) list).
7. **Best is rare (24 of 924, 2.6%) and mostly plausible**, which is the right shape; 3 of the 6 sampled Best rows are not justified by their own superiority text. SEO has 6 Best rows out of 28 including one single boolean variant (SEO-0004) while the sibling operations are Strong.
8. **Acceptance is a one-line sketch inside `notes`**, and `acceptance[0]` has only a `test_id`; the schema allows `corpus` and `tolerance` but they are never filled. Figures in the sketches are definitional, as `COVERAGE.md` itself says.
9. **Some superiority sentences name Dino and state a plan rather than an exceed-Archicad outcome** (ATTRIB-0043, ATTRIB-0046).

## 4. Pages not reachable from the review environment

| Page | Result | Rows in sample | Rows in register |
|---|---|---|---|
| `https://archicadapi.graphisoft.com/JSONInterfaceDocumentation/` | proxy 403 (host not allowed) | API-0003, API-0013 | 18 |
| `https://gdl.graphisoft.com/` | proxy 403 | GDL-0004, GDL-0014 | 12 |
| `https://help.graphisoft.com/AC/29/INT/_AC29_Help/` | HTTP 404 | GRAPHIC-0011, NEW2930-0017, RENO-0009, RENO-0017, RENO-0019, UI-0009, VIZ-0013 | 28 |
| `https://pypi.org/project/archicad/` | page needs JavaScript; `pypi.org/pypi/archicad/json` worked and supported API-0007, -0040, -0041 but not API-0027 (Jupyter) | API-0007, -0027, -0040, -0041 | 12 |
| `https://github.com/GRAPHISOFT/archicad-api-devkit` | proxy 403 | not cited in sample | n/a |

## 5. Missing coverage

`help.graphisoft.com` publishes a sitemap: 2,078 topic pages under `AC/30/INT/Topics`, of which 604 are cited by a row. I read the titles of a stratified set of uncited pages (all of the small sections, 16 random pages from each large one) and searched the 924 rows for each subject. These pages describe a documented feature and no row mentions the subject (URLs are relative to `https://help.graphisoft.com/AC/30/INT/Topics/`):

| # | Feature | Page |
|---|---|---|
| 1 | PARAM-O manager | `065_PARAM-O/065_PARAM-O-4.html` |
| 2 | PARAM-O: creating an object | `065_PARAM-O/065_PARAM-O-6.html` |
| 3 | PARAM-O: getting started (menu commands) | `065_PARAM-O/065_PARAM-O-3.html` |
| 4 | PARAM-O node library: solid operations | `065_PARAM-O/065_PARAM-O-90.html` |
| 5 | PARAM-O node library: attribute inputs | `065_PARAM-O/065_PARAM-O-36.html` |
| 6 | PARAM-O node library: GDL object and shape viewer | `065_PARAM-O/065_PARAM-O-94.html` |
| 7 | ProKit Transformer | `960_Prokit/ProKit_Core/Transformer/Transformer_Intro.html` |
| 8 | ProKit Annotate: font replacement manager | `960_Prokit/ProKit_Core/Annotate/AnnotateReplacingFonts.html` |
| 9 | ProKit Annotate: attribute replacement manager | `960_Prokit/ProKit_Core/Annotate/AnnotateReplaceAttributes.html` |
| 10 | ProKit detail elements: add nails | `960_Prokit/ProKit_Core/DetailElements/DetailAddNails.html` |
| 11 | Model Compare: compare two views of one project | `081_ModelCompare/081_ModelCompare-4.html` |
| 12 | Model Compare: structural analytical models | `081_ModelCompare/081_ModelCompare-6.html` |
| 13 | Model Compare: filter changes in the palette | `081_ModelCompare/081_ModelCompare-8.html` |
| 14 | Licensing: sign in with Graphisoft ID | `022_Licensing/022_Licensing-3.html` |
| 15 | Licensing: license types | `022_Licensing/022_Licensing-2.html` |
| 16 | Licensing: working on multiple devices | `022_Licensing/022_Licensing-10.html` |
| 17 | Issue handling: show containing issue | `082_IssueHandling/082_IssueHandling-3.html` |
| 18 | Issue handling: create issue from a Model Check report | `082_IssueHandling/082_IssueHandling-12.html` |
| 19 | Issue handling: create issue from a Model Compare change list | `082_IssueHandling/082_IssueHandling-13.html` |
| 20 | Teamwork: reserve list scheme dialogs | `080_Collaboration/080_Collaboration-41.html` |
| 21 | Documentation: drawing titles | `070_Documentation/070_Documentation-91.html` |
| 22 | Documentation: elliptical arcs and full ellipses | `070_Documentation/070_Documentation-18.html` |
| 23 | Documentation: display vector fills in project views | `070_Documentation/070_Documentation-7.html` |
| 24 | Documentation: area text in a fill | `070_Documentation/070_Documentation-6.html` |
| 25 | Documentation: label multiple selected elements | `070_Documentation/070_Documentation-75.html` |
| 26 | Documentation: symbol labels with element information | `070_Documentation/070_Documentation-73.html` |
| 27 | Documentation: layout workflow scheme for smaller projects | `070_Documentation/070_Documentation-95.html` |
| 28 | Attributes: override surfaces with Surface Painter | `025_Attributes/025_Attributes-50.html` |
| 29 | Attributes: Profile Manager uniform contours panel | `025_Attributes/025_Attributes-59.html` |
| 30 | Configuration: Home screen | `020_Configuration/020_Configuration-3.html` |
| 31 | Configuration: tab bar | `020_Configuration/020_Configuration-18.html` |
| 32 | Configuration: Goodies (add-on source) | `020_Configuration/020_Configuration-34.html` |
| 33 | Interaction: Navigator preview palette (2D) | `030_Interaction/030_Interaction-12.html` |
| 34 | Cineware surface shaders: procedural | `141_CineRenderSurfaces/141_CineRenderSurfaces-21.html` |
| 35 | Cineware surface shaders: sketch | `141_CineRenderSurfaces/141_CineRenderSurfaces-23.html` |
| 36 | Cineware surface channels (fog, glow, luminance, bump, normal, alpha) | `141_CineRenderSurfaces/141_CineRenderSurfaces-9.html` and siblings |
| 37 | Autodesk Forma connection (publish to Forma) | `120_Interoperability/open_forma_connection.html` |
| 38 | FRILO connection | `120_Interoperability/120_Interoperability-50.html` |
| 39 | Export to Artlantis | `120_Interoperability/120_Interoperability-51.html` |
| 40 | MEP: flexible systems with distribution systems; custom symbol placement | `005_NewFeatures/system_setup_and_distribution_systems.html`, `005_NewFeatures/expanded_options_for_mep_custom_symbols.html` |

The sitemap sweep was a sample, not a census: the true gap list is longer (the `065_PARAM-O` section alone has 97 pages and one row; `040_ElementsVB` has 200 uncited pages that I only sampled). Note also that `COVERAGE.md` lists "topics not found in the Help" (reinforcement, timber, dormers, daylight analysis, LandXML terrain, cost workflow, Twinmotion/Enscape links); I did not test that claim.

## 6. Verdict: ACCEPT WITH FIXES

Reasons: the rows are well paraphrased (no copying), mostly tied to real topic pages (190 of 193 distinct pages were readable; 3 failed), and the author's own second-pass notes are honest about 88 weak rows. But the content defect rate is 19% (about 1 row in 5), 36 rejected rows are still live requirements, and 8% of rows cite landing pages or dead URLs.

Blocking conditions (before any row counts toward parity scoring):

1. Remove or withdraw the 36 `REJECTED` rows (the sampled 11 are in `fixes_archicad.jsonl` as `field: row`).
2. Do not credit or count the 52 `UNVERIFIED` rows until each has a page; repoint the 28 `_AC29_Help/` rows to a real topic page or drop them.

Other fixes: apply `fixes_archicad.jsonl`; add `alias_of` for the duplicate pairs listed in section 3; give `new2930` rows their functional factory; move pure 2D rows to `cad-2d` or document why not; add rows for the gaps above; expand fragment statements into checkable outcomes. A re-sample after fixes is advised because this review's rate was measured on the sample only.
