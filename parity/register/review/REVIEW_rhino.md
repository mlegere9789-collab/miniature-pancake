# Independent review: Rhino register (`parity/register/rhino/*.jsonl`)

Reviewer: independent pass, not the author of the rows. Date of review: 2026-11-14. Rows in scope: 1,356.
Verdict: **ACCEPT WITH FIXES** (details and conditions at the end).

## 1. Sample and method

- Sample: `random.Random(8)` over the sorted ids, 150 rows, then every domain file topped up to at least 10 sampled rows (all rows for `LIC`, which has 2). Final sample **211 rows** across 19 domain files, **192 distinct `source.doc` pages**. Ids are in `sample_ids_rhino.txt`; `sample_method.py` regenerates them.
- For every sampled row the cited page was downloaded and read as text. Pages that are redirect stubs (a meta refresh to another page, very common on docs.mcneel.com) were followed and both the stub description and the target page were read. Statements were checked term by term against the page, and a longest-common-word-run check against the page text was used to detect copying.
- Judged per row: (a) page reachable and relevant, (b) statement matches the page, is paraphrased and is not copied, (c) `target` sensible, (d) `factory`/`domain`, (e) statement is a testable outcome rather than a label.
- Limits, stated honestly: one reviewer, one pass. Criterion (b) was checked against the single cited page, so a claim the author verified somewhere else is counted as a defect here; (c) and (d) contain judgment (marked in the fixes file where a fix is not page-confirmed). Rates carry a wide interval at this sample size (Wilson 95% interval given).

Sample composition:

| Domain | Rows in register | Rows in sample |
|---|---|---|
| ANALYZE | 54 | 10 |
| ANNOT | 50 | 10 |
| CURVE | 158 | 19 |
| DIGIT | 11 | 10 |
| FILE | 37 | 10 |
| LAYOUT | 38 | 10 |
| LIC | 2 | 2 |
| MESH | 114 | 10 |
| ORG | 118 | 10 |
| RENDER | 75 | 10 |
| SCRIPT | 50 | 10 |
| SELECT | 125 | 17 |
| SETTING | 61 | 10 |
| SOLID | 42 | 10 |
| SUBD | 46 | 10 |
| SURF | 144 | 20 |
| UI | 23 | 10 |
| VIEW | 150 | 13 |
| XFORM | 58 | 10 |

## 2. Results

Rows may appear under several criteria. "Content defect rate" counts rows with a defect in (a), (b), copying, (d) or (e); the "all criteria" rate also adds (c).

| Criterion | Defective rows | Ids |
|---|---|---|
| (a) page unreachable / wrong / does not document the claim | 5 | SELECT-0024, SCRIPT-0031, SCRIPT-0044, SCRIPT-0046, SCRIPT-0048 |
| (b) statement does not match the page | 10 | CURVE-0043, CURVE-0129, RENDER-0059, RENDER-0061, SETTING-0003, SETTING-0008, SETTING-0036, VIEW-0095, SCRIPT-0031, SCRIPT-0048 |
| (b) near-verbatim copy of page text (10+ consecutive words) | 3 | LAYOUT-0033, SUBD-0035, SCRIPT-0025 |
| (c) `target: Best` without row-level justification | 55 of 59 Best rows | CURVE-0015, CURVE-0025, CURVE-0026, CURVE-0028, CURVE-0040, CURVE-0043, CURVE-0061, CURVE-0071, CURVE-0073, CURVE-0080, CURVE-0091, CURVE-0095, CURVE-0096, CURVE-0115, CURVE-0117, CURVE-0129, CURVE-0135, CURVE-0144, CURVE-0155, SOLID-0007, SOLID-0014, SOLID-0019, SOLID-0021, SOLID-0036, SOLID-0042, SUBD-0006, SUBD-0007, SUBD-0025, SUBD-0029, SUBD-0034, SUBD-0035, SUBD-0037, SUBD-0039, SUBD-0041, SUBD-0044, SURF-0013, SURF-0018, SURF-0019, SURF-0020, SURF-0033, SURF-0039, SURF-0041, SURF-0051, SURF-0054, SURF-0057, SURF-0059, SURF-0064, SURF-0078, SURF-0085, SURF-0086, SURF-0099, SURF-0102, SURF-0109, SURF-0125, SURF-0136 |
| (d) wrong `factory` or `domain` | 4 | SCRIPT-0037, SURF-0020, ANALYZE-0045, VIEW-0026 |
| (e) label or vague "manage/configure" statement rather than a testable outcome | 8 | SETTING-0003, SETTING-0006, SETTING-0022, SETTING-0024, UI-0008, UI-0011, UI-0023, LAYOUT-0015 |

- **Content defect rate: 27/211 = 12.8%** (Wilson 95%: 8.9% to 18.0%).
- **All-criteria defect rate: 78/211 = 37.0%** (30.7% to 43.7%), driven almost entirely by (c).
- Unreachable pages: **0 of 192** distinct source pages returned an error. Details below for index pages that were refused to the review proxy.

Confirmed defects (checked on the page; proposed fixes are in `fixes_rhino.ndjson`):

- **CURVE-0043 ExtractPipedCurve**: the row says it recovers the underlying curve. The page says it copies the piping display mesh out as a separate mesh object. Wrong outcome.
- **RENDER-0061 UVEditor** and **RENDER-0059 Unwrap**: the statements follow the one-line stub descriptions, which McNeel evidently copied between commands. The texture-mapping page says UVEditor opens the UV Editor and Unwrap projects onto the world XY plane. SELECT-0052 (SelExtrusion) has the same stub-description error on the vendor side, but there the row is right and the page body confirms it.
- **SELECT-0024 SelClosedSubD** cites the SelOpenSubD page (same URL as SELECT-0085), so the cited page is for the wrong command.
- **SCRIPT-0031** cites a .NET 10 migration guide that is marked under construction and never mentions CPython 3.13. **SCRIPT-0044** cites the Yak landing page where the Package Restore guide is the real source. **SCRIPT-0046** cites the Rhino 8 "new" page, which has no script-editor details. **SCRIPT-0048** claims managed and native SDKs, panels and file formats but cites only the first C/C++ plug-in guide.
- **CURVE-0129 ChangeDegree**: adds "deviation reported when lowering", which the page does not say (it says the result is an approximation).
- **SETTING-0003, -0008, -0036**: claim a "theme", "referenced files", and a "control polygon" setting; none appear on the cited option pages.
- **Copying**: LAYOUT-0033 (16-word run), SUBD-0035 (21 words) and SCRIPT-0025 (10 words) reuse page sentences. Three more rows (LAYOUT-0027, RENDER-0010, SURF-0078) share 9-word runs; I did not count those.
- **Factory/domain**: SCRIPT-0037 (shut lining, a display-mesh object property) sits in `SCRIPT`/`scripting-api-ai`; SURF-0020 (ExtractCustomMappingObject, a texture-mapping command) sits in `SURF`/`kernel-geometry`; ANALYZE-0045 (SystemInfo) is filed under `analysis-site`; VIEW-0026 (Help) sits in `VIEW` although `UI` has the other help commands.

## 3. Systematic problems

1. **`target: Best` is assigned by domain, not by row.** All 390 rows in `CURVE`, `SURF`, `SOLID` and `SUBD` are `Best` (28.8% of the register; 59 of 211 = 28% of the sample), and every other domain is `Strong` with zero exceptions. The brief says Best should be rare and justified. It includes AddGuide, Plane, Box, Polygon, SubDSphere and SubDTorus. In the sample only 4 `Best` rows (Boolean2Objects, BooleanDifference, Cap, UnjoinEdge) fit the SOLID domain claim ("never fail silently on coincident or tangent faces"); the CURVE, SURF and SUBD text is "reproduce/match Rhino" plus "live preview", which is parity, not superiority.
2. **`superiority` and `acceptance` are domain boilerplate.** Each domain has one superiority sentence and one corpus/tolerance block repeated on every row (one block per domain; only SCRIPT has two). `test_id` is unique per row, but the corpus and tolerance are not row-specific; for example `degree_knots: identical or equivalent exact NURBS` is attached to AddGuide, which has no NURBS output, and DIGIT rows need a digitizer protocol log that the row never defines. A tester cannot derive a pass/fail check from the row alone.
3. **Redirect-stub sources.** 96 of 211 sampled rows (45%) cite a docs.mcneel.com page that only redirects to a shared page with an anchor. They resolve in a browser, but the page that actually documents the command is a different file, and two stub descriptions (UVEditor, SelExtrusion) disagree with the real page. `source.doc` should be the redirect target.
4. **`ux-access-i18n` is a catch-all.** `SELECT`, `VIEW`, `SETTING` and `UI` (359 rows, 26% of the register) all use it, though accessibility and localization are a small part of it; in `ledger/waves.json` it is wave 3, which defers selection and view commands behind everything else. Likewise `ORG` (layers, blocks, groups, worksessions, 118 rows) is all `direct-modeling`, and `DIGIT` (hardware arms) is `interop-migration`. I did not count these as row defects because the factory list has no better fit, but the mapping should be decided on purpose.
5. **Thin statements.** 58 of 211 sampled statements are under 45 characters (for example "Open the render panel."). They are checkable but say nothing about options or edge cases the page documents.
6. **Rhino 9 beta rows (198 rows)** cite a changelog page (113 rows share `newinrhino9.htm`) or the marketing page. They are traceable by the command name but several multi-claim rows cannot be tied to a single changelog line (for example FILE-0031 bundles five DWG/DXF export items). I spot-checked about 19 such rows against the changelog text; the claims were present, and the only problems were the copying cases listed above.
7. **Near-duplicate pairs** exist but are mostly legitimate variants (DimDiameter/DimRadius, the animation-frame commands RENDER-0063 to -0065). LAYOUT-0031 and SETTING-0061 both describe section styles; the pair should be given `alias_of` or one row dropped.
8. `COVERAGE.md` cites `commandlist/commandlist_command_list.htm`, which refuses my requests (403); the readable index is `commandlist/command_list.htm`.

## 4. Pages not reachable or not useful from the review environment

- `source.doc` pages: none failed (192 of 192 returned content after following redirects).
- Index pages that returned 403 through the proxy although they are real McNeel pages: `options/options.htm`, `documentproperties/documentproperties.htm`, `panels/panels.htm`, `sidebar/sidebar.htm`, `toolbarmap/toolbarmap.htm`, `commandlist/commandlist_command_list.htm`.
- rhino3d.com and developer.rhino3d.com pages loaded, but `Moving to .NET 10` is flagged under construction by the vendor.

## 5. Missing coverage

Command index: every entry in the Rhino 8 command list (`commandlist/command_list.htm`, 1,107 distinct names) has a row; 33 names did not match a row reference exactly, and all 33 turned out to be fragments of in-text links rather than commands (I found no listed command without a row). All 53 commands listed under "New commands" on the Rhino 9 page have rows. The gaps are therefore in non-command documentation. These pages exist (HTTP 200) and no row cites them or mentions their subject:

| # | Feature (page) | Note |
|---|---|---|
| 1 | STL import/export options, `fileio/stereolithography_stl_import_export.htm` | no row mentions STL |
| 2 | OBJ import/export, `fileio/wavefront_obj_import_export.htm` | no row mentions OBJ |
| 3 | Block instance information, `information/blockinstanceinfo.htm` | |
| 4 | Administrator-enforced settings, `information/admin-enforced_settings.htm` | |
| 5 | Naming conventions, `information/namingconventions.htm` | |
| 6 | Text fields (formula fields in text), `information/text_fields.htm` | |
| 7 | Focal blur object property, `properties/focalblur.htm` | |
| 8 | Layout user text, `properties/layoutusertext.htm` | |
| 9 | Edge softening property, `properties/edgesoftening.htm` | |
| 10 | Thickening property, `properties/thickening.htm` | |
| 11 | Shut lining property (only a Rhino 9 delta row, SCRIPT-0037), `properties/shutlining.htm` | |
| 12 | Curve piping property (only ExtractPipedCurve is covered), `properties/curvepiping.htm` | thin |
| 13 | Displacement property (only a Rhino 9 extract row), `properties/displacement.htm` | thin |
| 14 | SVG Editor, `toolbarsandmenus/svg_editor.htm` | |
| 15 | Toolbar button editor, `toolbarsandmenus/toolbar_button_editor.htm` | |
| 16 | Toolbar sizes and styles options, `options/toolbars_sizes_and_styles.htm` | |
| 17 | Drag and drop a file into the Rhino window, `user_interface/drag_and_drop_file.htm` | |
| 18 | Select color dialog, `user_interface/select_color.htm` | |
| 19 | Document properties, Rhino Render page, `documentproperties/render.htm` | |
| 20 | Object types description, `information/rhinoobjects.htm` | only referenced by two selection rows |

That is 20 gaps, not 40: I stopped when the index sweeps stopped finding pages with no row. The curve piping, displacement, edge softening, thickening and shut lining properties are one family (display-mesh modifiers) and would be one row group.

## 6. Verdict: ACCEPT WITH FIXES

Reasons for not accepting outright: a content defect rate of 13% (about 1 row in 8), a tiering rule that makes 29% of the register Best by domain label, and acceptance blocks that are not row-specific. Reasons it is not a rejection: all 192 cited pages exist, 87% of sampled rows had no content defect, the command index is fully covered, and the defects found are fixable mechanically.

Conditions before the rows are used to measure parity:

1. Re-tier `Best`: keep it only where a row-specific exceed-Rhino claim is written (the SOLID Boolean class is a fair starting set). Apply `fixes_rhino.ndjson` for the sampled rows and repeat the rule on the remaining Best rows (the 4 SOLID Boolean-class rows listed above are the only sampled ones I would keep).
2. Replace the per-domain `acceptance` block with per-row corpora and tolerances, at least for rows that will be credited.
3. Repoint redirect-stub `source.doc` values to their targets and apply the doc and statement fixes in the fixes file; re-run the sampling after fixes.
4. Decide the factory mapping for SELECT/VIEW/SETTING/ORG/DIGIT explicitly, because it sets their wave.
5. Add rows for the 20 gaps above.

## 7. Second-session verification and additions (2026-11-15)

A second independent session continued after the first was cut off by a usage limit. It did not redo the sample; it checked the first pass instead.

- The sample is reproducible: re-running the sampling over the current register gives exactly the 211 ids in `sample_ids_rhino.txt` (1,356 rows). The strict validator passes on the untouched register (10,660 rows, 0 errors, 0 warnings).
- Re-checked on the live pages: ExtractPipedCurve (the page says the command copies the piping display mesh into a separate mesh object, so CURVE-0043 is a real defect); ChangeDegree (the page has no mention of any deviation report, so CURVE-0129 is real); SelClosedSubD (the stub redirects to the selection-commands page, which fixes SELECT-0024). Not re-checked: the other fixes, which I leave as proposals.
- Gap claims re-checked against the rows: no row mentions STL, OBJ, text fields, focal blur, edge softening, thickening, the SVG editor, the button editor, drag and drop of files, the select-color dialog, or naming conventions. The gap table in section 5 holds.
- Deviation from the brief: the brief asks for `fixes_*.jsonl`; the first session used `.ndjson` because `validate_register.py` globs every `parity/register/**/*.jsonl` and does not skip `review/` (no mention of "review" in the tool). I kept `.ndjson` so the register validator stays green.
- The first pass stopped at 20 gaps and said the sweep had run dry. It had not: the file-format section of the help (reached by following links from the cited pages) lists far more formats than the 37 FILE rows cover. Rows exist only for DWG/DXF, IGES options, STEP, SketchUp and the Rhino 9 list in FILE-0034 (E57, glTF, FBX, PLY, USD). Gaps 21 to 40 below all have a reachable page (HTTP 200 on `https://docs.mcneel.com/rhino/8/help/en-us/fileio/`) and no row that names the format.

| # | Feature (page under `fileio/`) |
|---|---|
| 21 | 3D Studio import/export, `3dstudio_3dsimportexport.htm` |
| 22 | 3MF export, `3mf.htm` |
| 23 | Adobe Illustrator import/export, `ai_ai_import_export.htm` |
| 24 | Microstation DGN import, `microstation_dgn_import.htm` |
| 25 | DirectX import/export, `directx_x_import_export.htm` |
| 26 | Encapsulated PostScript import, `encapsulated_postscript_eps_import.htm` |
| 27 | LightWave import/export, `lightwave_lwo_import_export.htm` |
| 28 | Geomview OFF import, `geomview_off_import.htm` |
| 29 | Point file import (asc, csv, xyz, pts), `points_file_asc_csv_txt_xyz_cgo_import.htm` |
| 30 | SOLIDWORKS part and assembly import, `solidworks_sldprt_sldasm_import.htm` |
| 31 | Slice (SLC) import/export, `slice_slc_import_export.htm` |
| 32 | VRML import/export, `vrml_vrml_import_export.htm` |
| 33 | ACIS SAT export, `acis_sat_export.htm` |
| 34 | Parasolid export, `parasolid_x_t_export.htm` |
| 35 | COLLADA export, `collada_dae_export.htm` |
| 36 | Google Earth KMZ export, `google_earth_kml_kmz_export.htm` |
| 37 | Object properties CSV export, `object_properties_csv_export.htm` |
| 38 | POV-Ray and RenderMan RIB export, `pov-ray_pov_export.htm`, `renderman_rib_export.htm` |
| 39 | X3D and XAML export, `x3d_x3dv_export.htm`, `xaml_xaml_export.htm` |
| 40 | Windows metafile (WMF/EMF) export, `windows_metafile_wmf_emf_export.htm` |

Consequence for the verdict: the "command index is fully covered" statement in section 6 is true for commands only. Import and export formats are a real hole in FILE (about 40 formats on the index, roughly a quarter with any row). The verdict stays **ACCEPT WITH FIXES**, with condition 5 widened to "add rows for the file formats above and for the 20 gaps in section 5".
