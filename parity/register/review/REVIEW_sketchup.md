# Independent review: SketchUp Register rows

Reviewer job `rvsketchup` (did not author the rows). Base: branch `w0-register2` (635 rows in `parity/register/sketchup/*.jsonl`). Date of review: 2026-11-14. Every `source.doc` in the sample was opened (WebFetch, plus curl for the Ruby API pages); page text was compared to the row statement.

## Verdict: ACCEPT WITH FIXES (conditional)

As delivered, the Register is not fit to drive acceptance tests: **69/171 = 40.4% of sampled rows have at least one major defect** (page does not support the statement, wrong or empty source page, unjustified target or wrong factory). Roughly 95% CI on the whole file: 33% to 47%. The defects are mostly over-specified statements and generic source pages, which are cheap to correct, and the targets (Strong/Best), IDs, schema fields and one-test-per-row structure are sound. Accept only after (1) the 64 row corrections in `fixes_sketchup.jsonl` are applied, (2) the 43 rows that cite generic pages (see Systematic problem 1) are re-sourced, and (3) a second spot check of the 4 worst domains (c-api-sdk, ai-tools, extension-platform, top-extensions) shows major defects below 15%. If (3) fails, REJECT those domain files.

## Sample

- Method: `random.Random(8).sample(rows sorted by id, 150)`. A 150-row sample cannot give 8 rows per domain (19 domains x 8 + the 7-row style-builder file = 159), so the draw was topped up per domain (using the same RNG) and ruby-api/top-extensions/layout-documentation were capped at 16/10/10 rows. **Final sample: 171 rows** (57 top-up rows plus 114 from the 150 draw; style-builder has only 7 rows so all 7 were taken). Per domain: ruby-api 16, top-extensions 10, layout-documentation 10, style-builder 7, all other 16 domains 8.
- Sample list with the facts found per row: kept in the session scratchpad (`sk_notes.txt`); each defect below carries the row id.
- Full-file checks done in addition: validator (`--strict`: 10,660 rows, 0 errors before this review file was added); method counts of all 134 `ruby-api` rows against the live Ruby reference (all agree once deprecated methods are excluded; see Systematic problem 6); host, statement-length, duplicate and target statistics over all 635 rows.
- Two sampled pages could not be fetched (host not on the allowed list, not bypassed): EXT-0031 (support.mindsightstudios.com), EXT-0033 (www.toolfarm.com). Counted separately as unverifiable, not as defects.

## Defect counts per criterion (sample 171)

| Criterion | Rows | Rate | Row ids (prefix D8-SKETCHUP-) |
|---|---|---|---|
| (a) page not reachable/relevant (shell, generic index, wrong article) | 17 | 9.9% | AI-0006, ANALYS-0010, CAPI-0002, CAPI-0004, CAPI-0007, CAPI-0010, CAPI-0011, CAPI-0012, CAPI-0015, ECO-0013, EDIT-0037, EXT-0021, EXT-0044, EXT-0051, EXT-0058, ORG-0011, RUBY-0133 |
| (a2) unverifiable, host not allowed | 2 | 1.2% | EXT-0031, EXT-0033 |
| (b) statement not supported or contradicted by the page (major) | 54 | 31.6% | AI-0002, AI-0005, AI-0006, AI-0012, AI-0013, ANALYS-0010, ANALYS-0011, ANALYS-0012, COLLAB-0014, COLLAB-0017, DRAW-0002, DRAW-0020, DRAW-0025, DRAW-0026, DYNCOMP-0012, DYNCOMP-0019, ECO-0002, ECO-0010, ECO-0012, ECO-0013, ECO-0014, EDIT-0009, EDIT-0032, EDIT-0037, EXT-0017, EXT-0059, INFER-0017, INFER-0018, INFER-0022, INFER-0024, IO-0018, IO-0020, IO-0021, IO-0022, LAYOUT-0008, LAYOUT-0011, LAYOUT-0022, LAYOUT-0027, ORG-0010, ORG-0030, RUBY-0001, SCAN-0007, SCAN-0013, SCAN-0014, SCENE-0025, SCENE-0033, SITE-0012, SITE-0018, SOLID-0001, SOLID-0013, STYLEB-0007, UX-0004, UX-0009, UX-0010 |
| (b-minor) wording/ref nuance, partly supported | 29 | 17.0% | AI-0001, COLLAB-0002, DRAW-0011, DRAW-0037, DYNCOMP-0002, DYNCOMP-0007, DYNCOMP-0011, ECO-0001, ECO-0008, EDIT-0022, EXT-0015, EXT-0016, INFER-0006, INFER-0008, IO-0002, IO-0008, LAYOUT-0001, LAYOUT-0056, LAYOUT-0058, ORG-0031, RUBY-0010, RUBY-0079, SCAN-0015, SCENE-0007, SCENE-0010, SITE-0014, SOLID-0012, STYLEB-0001, UX-0015 |
| (c) target Strong/Best not sensible | 3 | 1.8% | ECO-0013, INFER-0024, RUBY-0111 |
| (d) wrong factory/domain | 3 | 1.8% | EXT-0059, INFER-0024, UX-0014 |
| (e) label, not an outcome statement | 21 | 12.3% | AI-0010, ANALYS-0006, ANALYS-0012, COLLAB-0015, COLLAB-0018, DYNCOMP-0009, ECO-0008, ECO-0012, EXT-0058, EXT-0059, IO-0009, SCAN-0013, SITE-0015, SITE-0018, STYLEB-0004, STYLEB-0005, STYLEB-0006, UX-0004, UX-0006, UX-0010, UX-0015 |

Rows with at least one major defect (a, b, c or d): **69/171 = 40.4%**. Rows with any defect incl. minor, unverifiable and label: 84/171 plus the minor-only rows. "Testable" and "own words" were judged inside (b)/(e): statements are generally in own words (one near-verbatim phrase: AI-0001 reuses the page's "combine ... viewport with a text prompt or preset style").

Major-defect rate per domain (defective/sampled): c-api-sdk 7/8, ai-tools 5/8, extension-platform 5/8, top-extensions 6/10, drawing-tools 4/8, inference-engine 4/8, import-export 4/8, interface-preferences 4/8, layout-documentation 4/10, analysis-hub 3/8, edit-tools 3/8, groups-components-tags 3/8, scan-essentials 3/8, collaboration-cloud 2/8, dynamic-components 2/8, scenes-styles-materials 2/8, sandbox-terrain-geolocation 2/8, solid-tools-procedures 2/8, ruby-api 3/16, style-builder 1/7.

## Systematic problems

1. **Generic source pages (43 rows of 635).** `https://extensions.sketchup.com/` (20 rows), `https://extensions.sketchup.com/sketchup-sdk` (16) and `https://sketchucation.com/pluginstore` (7) render only a header / store index; they cannot support any specific claim (all 7 sampled CAPI rows and 3 sampled EXT rows fail on this). The C API rows need the C API reference (not reachable under the allowed host list from the cited URL) or a developer.sketchup.com page; EXT rows need each extension's own page.
2. **Statements embellished beyond the page.** The author added plausible SketchUp behaviour (typed slope, Place Axes, Paste in Place, View-menu hide, "cuts affect all geometry", ANIMATE across scenes, tags like On Axis) that the cited page does not state; several contradict it (DRAW-0026, SCAN-0014 phone vs Meta Quest headset, ANALYS-0011 "retired" vs standalone subscription, AI-0013/SCENE-0025 material generation vs texture-map generation). Each sampled one is in `fixes_sketchup.jsonl`; the rest of the file very likely has the same issue at about the same rate.
3. **Release-note rows pointing at the wrong article** (ECO-0010, IO-0020, COLLAB-0014, DYNCOMP-0019, CAPI-0017 and ref text in UX rows): a claim from one release is attributed to a page about another version or a neighbouring article. Statements about versions (2026.2, 2027.0, "since 2026.1") must cite the release-notes page that states them.
4. **Best is not rare inside inference-engine.** All 24 INFER rows are `Best` (24 of 24, the only Best rows in the register). Superiority statements such as "holds 60 fps up to 100 million triangles" (INFER-0024) or "infers intersections with NURBS faces" are not justified by the sources and are not what makes a feature Best. Recommend Strong for INFER-0024 now and a per-row justification line for the rest before keeping Best.
5. **Boilerplate `superiority`.** 131 rows reuse five identical sentences (76 rows "Keeps API names and semantics ...", 30 "Keeps the LayOut automation surface ...", 10, 8 and 7 others), so the field gives no row-specific bar to beat. Not a defect against any single criterion, but the acceptance tests cannot be derived from it.
6. **Ruby method counts.** "Public reference lists N methods" equals the number of non-deprecated methods on each class page (e.g. AutoTextDefinition 29 of 31 listed, View 46 of 47); the statement should say "non-deprecated" or give the page total, and deprecated classes (Sketchup::Set, RUBY-0111) must say so. Module pages (Sketchup 62 vs 63 listed, UI 30 vs 31) are off by one; re-check RUBY-0041 and RUBY-0126.
7. **Third-party hosts.** 26 rows cite pages on hosts outside the allowed list (mindsightstudios 10, toolfarm reseller page 3, community.sketchucation 4, forums.sketchup 3, blog.sketchup 2, sketchup.trimble 2, dummies 1, github 1). Where only a reseller or a forum post backs a claim, the statement should be limited to what the post says.
8. **Label-style statements** (about 12% of the sample; 47 rows of 635 are shorter than 45 characters, e.g. "Preferences dialog.", "Revit import and exchange.", "Freehand draws smoothed paths."). They name a feature and cannot be tested; rewrite as an observable behaviour.
9. **Validator interaction.** `parity/register/review/fixes_sketchup.jsonl` sits under `parity/register/`, so `validate_register.py --strict` globs it as rows and reports duplicate ids and schema errors. Either exclude `parity/register/review/` in the validator or move review outputs before merging.

## Fixes

`parity/register/review/fixes_sketchup.jsonl`: 72 corrections over 64 rows ({id, field, old, new, reason}), all confirmed on the cited page. Rows I could not fix because no correct public page was found within the allowed hosts (need re-sourcing): CAPI-0002/0004/0007/0010/0011/0012/0015, EXT-0021, EXT-0031, EXT-0033, EXT-0044, EXT-0051, EXT-0058, ECO-0013 (internal test plan, not a SketchUp feature), ORG-0011, EDIT-0032 (Nx array not on cited page), EDIT-0037 (Model Info), RUBY-0133. The rows were not edited.

## Documented features with no row (28)

Ruby reference classes in the class list with no row (checked against all 164 class pages, `ruby.sketchup.com/class_list.html`):
1. Geom::Point3d
2. Geom::Vector3d
3. Geom::Point2d
4. Geom::Vector2d
5. Geom::Bounds2d
6. Geom::OrientedBounds2d
7. Geom::Transformation2d
8. Layout::LockedEntityError
9. Layout::LockedLayerError
10. Layout (module: constants and version helpers)
11. Sketchup::Http (module)
12. Each observer class individually (DefinitionObserver, DefinitionsObserver, DimensionObserver, EntitiesObserver, EnvironmentsObserver, FrameChangeObserver, InstanceObserver, LayersObserver, MaterialsObserver, ModelObserver, OptionsProviderObserver, PagesObserver, RenderingOptionsObserver, SelectionObserver, ShadowInfoObserver, ToolsObserver) is only folded into three grouped rows (RUBY-0132..0134) whose cited pages cover one class each.

Help-centre articles in the navigation with no matching row (keyword search of statement/ref/notes found no hit):
13. LayOut: Extracting Source Content from a LayOut File
14. LayOut: Rendering Models (viewport render modes) as its own behaviour (only LAYOUT-0042 touches raster output)
15. LayOut: Managing Changes and Updates to SketchUp Files (update-reference behaviour is only implied in LAYOUT-0046)
16. SketchUp Viewer (desktop/mobile viewer product pages; only the Quest page is cited)
17. Desktop: Editing All Instances of a Component (only ORG-0004 mentions all instances; no dedicated row for the article)
18. Desktop: Using the Credits feature (Model Info Credits; only mentioned in EDIT-0037)
19. Desktop: Importing, Exporting and Deleting Classifications (only CAPI/IFC rows)
20. Desktop: Orienting Faces and Minimizing Geometry (3D Warehouse modelling guidance: Axes and Origin Points, Purging Unused Entities, Avoiding Stray Geometry)
21. Desktop: Measuring Angles as a separate behaviour (DRAW-0021 covers the protractor tool, not the angle read-out)
22. 3D Warehouse: Managing Your 3D Warehouse Profile
23. 3D Warehouse: Editing an Uploaded Model / Editing an Uploaded Material
24. 3D Warehouse: Uploading from a Browser (only upload from SketchUp is covered)
25. Dynamic Components: Testing a Dynamic Component
26. Dynamic Components: Creating a Dynamic Component that Snaps to a Specific Size
27. Dynamic Components: Constraining Attributes of a Dynamic Component
28. Dynamic Components: Adding Attributes / Defining Attribute Values (authoring workflow)

Items 13-28 are lower confidence than 1-11: the keyword search can miss a row that uses different wording; confirm before adding.

## Reproducibility

Sample: `python3 -I` script with `random.Random(8)` over ids sorted, caps as stated above; notes in `$S/sk_notes.txt`; generator scripts `$S/mkfixes.py`, `$S/mkreview.py`, `$S/sk_sample.txt`.
