# Review of the Revit register (`parity/register/revit`)

Reviewer: independent, did not write the rows. Date: 2026-11-14. Branch: `w0-review-b`.

**Verdict: ACCEPT WITH FIXES.** Targets are sensible (25 Best of 1,034, each with a superiority text) and statements that have a page are mostly faithful. The blocking problem is that 55 rows (5.3%) cite no usable page.

## Sample

- Random sample: 150 of 1,034 rows. Top-up: 107 more so each of the 24 domain files has at least 10 (script checks only). 257 rows, 236 pages; 252 pages read, 5 not (see below).

How the pages were read. The help site is a script-rendered app whose page-lookup service is blocked from this environment, so the `?guid=` links open "Page Not Found" here. I read the static files instead: `/cloudhelp/<release>/ENU/<book>/files/<id>.htm`, with the book folder taken from each product's `data/toctree.json` (which also gives the page title, so I could check `source.ref` against the page title for every row that is in a table of contents). Pages were fetched with a plain browser User-Agent; `www.autodesk.com`, `blogs.autodesk.com` and `forums.autodesk.com` still refused (HTTP 403 or no connection). Sampling: `random.Random(8).sample` over the sorted ids, then topped up to at least 10 rows from every domain file ("top-up" rows). Rates below use the random sample only; top-up rows were checked by script only (reachable, copy overlap, factory rule, Best rule) and are reported separately.

Criteria (a row can fail several):
- **A** page reachable and relevant (the cited page opens, and its title matches `source.ref`).
- **B** statement describes what the page documents, in a testable way, without copying (content not on the cited page, or a run of 8+ identical words, counts as a defect).
- **C** `target` sensible: Best must be rare and justified by a `superiority` text.
- **D** `factory` or `domain` right.
- **E** statement is a testable outcome, not just a label or a note about a release.
- **F** duplicate of another row (same behaviour, same page).


## Results on the random sample (n = 150)

| criterion | base-sample count | rate | row ids |
|---|---|---|---|
| A page unreachable/irrelevant | 6 | 6/150 = 4.0% (95% CI 2-8%) | `COLLAB-0014`, `IFC-0015`, `INSERT-0036`, `MASS-0023`, `REL-0021`, `WSHARE-0016` |
| B statement not supported / copied | 9 | 9/150 = 6.0% (95% CI 3-11%) | `ANALYZE-0019`, `API-0016`, `ARCH-0037`, `FORMA-0010`, `MANAGE-0028`, `MASS-0015`, `MODIFY-0027`, `STRUCT-0080`, `SYS-0078` |
| C Best without justification | 0 | 0/150 = 0.0% (95% CI 0-2%) | none |
| D factory or domain wrong | 7 | 7/150 = 4.7% (95% CI 2-9%) | `REL-0019`, `REL-0021`, `UI-0006`, `UI-0007`, `UI-0009`, `UI-0025`, `UI-0026` |
| E label or not testable | 7 | 7/150 = 4.7% (95% CI 2-9%) | `ARCH-0097`, `ARCH-0131`, `IFC-0015`, `MANAGE-0035`, `REL-0019`, `REL-0021`, `WSHARE-0016` |
| F duplicate | 5 | 5/150 = 3.3% (95% CI 1-8%) | `ARCH-0122`, `ARCH-0150`, `INSERT-0005`, `MANAGE-0025`, `WSHARE-0016` |

Rows with at least one defect: 29/150 = 19.3%. Top-up rows (script checks only, n = 107): page not usable 10 (9.3%), copy overlap 1, factory rule hits 13 (12.1%), duplicate 2; no Best-without-superiority.

B detail (statement content not on the cited page): `STRUCT-0080` (structural framing schedule fields on a generic schedule page), `MODIFY-0027` (edit group in its own file and reload), `MANAGE-0028` (callout head and bubble radius on a Section Head page), `API-0016` (pick-box prompts), `SYS-0078` (placeholder ducts on a detail-level page), `ARCH-0037` (slope arrows), `ANALYZE-0019` (results explorer), `FORMA-0010` (toposolid), `MASS-0015` (an 8-word run copied from the page). B is a lower bound: I read every sampled statement against the page opening and checked specific claims on about 35 rows.

## Systematic problems

1. **55 rows cite a page that cannot be opened** (5.3% of the register, checked on all 1,034 rows, not only the sample):
   - 40 rows have `doc` = the help home plus the text "Revit 2027 Help topic: <name>"; none of the 40 names is a topic title in the 2027 table of contents (and none has a close match): Dynamo (10 rows, e.g. `DYNAMO-0004`), IFC (9, e.g. `IFC-0015`), MEP systems (7, e.g. `SYS-0088`), plus `MASS-0023` Merge Surface, `UI-0018`, `VIEW-0024`, `WSHARE-0016`. Their statements may be plausible but they have no source; treat as UNVERIFIED.
   - 5 API rows (`API-0024`, `-0025`, `-0026`, `-0028`, `-0029`) cite the developer-guide root plus a free-text topic ("API topic: ...").
   - 9 rows have a page id without the `GUID-` prefix, so the link opens Page Not Found (`IFC-0033`, `IFC-0034`, `INSERT-0035`, `INSERT-0036`, `INTEROP-0019` to `-0023`). With the prefix all 9 exist and match `source.ref`. Fixed in the fix file.
   - 1 link is 404 (`REL-0021`, folder `2027updates` missing). Fixed in the fix file.
2. **Statements about Revit instead of requirements.** "Revit 2027 added walls hosted on other walls..." (`ARCH-0122`), "Viewer mode is removed from Revit 2027" (`MANAGE-0035`), "Track the 2027.4 update" (`REL-0019`, `REL-0021`): the whole `release-2027x` file (21 rows) is release-note tracking; those rows have no testable outcome. 7 of 150 sampled rows fail E; `WSHARE-0016` and `IFC-0015` give no measure ("reduce sync time", "bounded time and memory").
3. **Thin statements.** Median length 101 characters, 29 of 150 sampled statements are 12 words or fewer (`ARCH-0032`, `ARCH-0060`, `ARCH-0070`). They are testable at a basic level, but acceptance depends on the row's sketch.
4. **Factory is one constant per file.** `ui-performance` (28 rows: browser, palette, search, undo, plus six speed/memory targets) and `release-2027x` (21) are `bim-arch`; speed rows belong in performance (wave 3), palette rows in ux-access-i18n (wave 3). `massing-site` (36) is `analysis-site` although masses and toposolids are modelling.
5. **Duplicates** (12 groups): `ANALYZE-0016`/`ANALYZE-0017`/`SYS-0011` (pressure loss report, three rows), `ARCH-0030`/`MASS-0033`, `ARCH-0021`/`STRUCT-0007`, `INSERT-0005`/`INTEROP-0018`, `COLLAB-0018`/`INTEROP-0015`, `ANNOT-0046`/`MANAGE-0009`, `MANAGE-0026`/`REL-0005`, `REL-0010`/`STRUCT-0022`, `API-0022`/`MANAGE-0027` (.NET recompile), `UI-0020`/`WSHARE-0016` (same superiority text), `UI-0019`/`MANAGE-0025` (same GPU target), `ARCH-0122`/`ARCH-0150`.
6. Best rows (25): well justified overall, but five performance Best rows share two superiority texts ("incremental sync independent of model size", GPU at 100 million triangles), so they are probably one requirement each, not five.

What is good: for the 935 rows whose page is in the 2027 table of contents, `source.ref` matches the page title in every case (0 mismatches). Copying is rare (3 rows with a 8-10 word run in 257, the longest 10 words). Superiority texts are specific.

## Unreachable pages

Not read: `REL-0021` (404), the 5 API pseudo-citations, and 40 non-pages (above). `www.autodesk.com` and `forums.autodesk.com` were not needed.

## Fix file

`parity/register/review/fixes_revit.jsonl` (28 proposals): 10 link repairs, 2 row removals (release tracking), 1 reworded statement, 13 factory moves, 2 alias links. Same note as for AutoCAD about the validator scanning `review/`.

## Missing coverage (40 documented topics with no row)

Of about 4,180 non-API, non-troubleshooting help topics in the Revit 2027 table of contents, 3,465 (83%) are not the source of any row. Many are task steps or property tables, but whole features are absent. Items:

1. Generative Design for Revit - https://help.autodesk.com/view/RVT/2027/ENU/?guid=Revit-GDiR
2. Manage the Revit Server Accelerator Connection - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-4BDF44F8-94CB-4E34-B7AE-FA96E9CA0E46
3. Start Worksharing Monitor - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-1C2DD46A-E07B-40FC-997B-6F2320430261
4. Interference Checking Categories - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-24A99D40-A2B5-4FC9-AE18-9D3413258FB4
5. Best Practices: Copy/Monitor - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-C71DDCA8-2AFC-4759-AFA6-714420872661
6. Reinforcement - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-A620B8CD-8F8B-45F0-924E-199FBE982183
7. Detailed Steel Modeling - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-F4AA1E38-F253-41BF-8FB2-AD98A1C994D1
8. Trusses - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-21CF2324-6459-4B3C-AB31-20A3FAE1FADB
9. Structural Precast - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-3AA696FA-8CE6-43CC-AB60-D0BC305351EC
10. Supported Structural Steel Shapes and Families for Steel Fabrication - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-6244E741-5D14-4DAD-AE25-5069F71B69F3
11. P&ID Modeler for Revit - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-E00F8454-655A-4874-BF37-1F77E0E15820
12. Heating and Cooling Loads Analysis - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-0B0730E6-967F-4311-AB05-71E96026F56C
13. Lighting and Solar Analysis - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-464A5692-512C-44C3-BC8A-B8003377CF7C
14. Lighting Analysis - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-A1FD7410-32F3-4FC2-8B78-4A843A3B4E52
15. Place a Path of Travel - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-F9943131-87F1-4D88-AC69-80BEB05E6A69
16. People Flow Toolkit - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-1E9954A1-8FA1-43C7-982F-D5E1BB1FF8DA
17. Roombook, Areabook, and Buildingbook Extensions for Revit - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-1282780A-9701-4B06-B6DF-C8C23B7EB9DA
18. Areabook Extension for Revit - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-74C70F0F-2089-4588-A8EA-4FC59967C3CC
19. Parts - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-22D24055-61A2-40BB-A2F7-A37990300B2B
20. Copy a Room Between Phases - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-6ABAF5EA-A48E-4CB1-A4ED-33B565BD3CC9
21. Add or Change an Assembly Code - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-5900EA95-9702-4054-A849-C0A70EABB2D1
22. Rendering - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-4046977A-9323-4535-9AC0-4EF9A138A5A6
23. Add a Revision Schedule to a Title Block - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-1F5AA5CB-28C0-4083-95C1-57DE2C53F0DE
24. Parameters Service Settings in Revit.ini - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-B056271D-AE7D-4DF8-99C4-7E603A49E548
25. Add Keyboard Shortcuts - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-81EF5A21-F562-43A2-A816-109915C82B01
26. Shared Parameters - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-E7D12B71-C50D-46D8-886B-8E0C2B285988
27. Remove a Material from the Face of an Element - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-BD2969AD-FCC4-4A91-92BF-44E4D409CC40
28. Create a Basic Fill Pattern - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-A75F6411-3D09-43F7-8302-B5AAA2A04D2D
29. Using Point Cloud Files in a Project - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-D179BB6C-5528-498F-9413-00237092C2FA
30. Sharing Topography between Revit and Civil 3D - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-3419F2BF-98F5-4988-BA51-9C23FE23D37D
31. Generate a Toposolid from a Toposurface - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-E4F66BE4-88B7-46D3-8BDB-F2BFBBA3A912
32. Specify Coordinates Before Linking Site Data - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-4B2BA313-5792-44F4-B0EF-814812DDB028
33. Property Lines - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-44A38002-3CD5-42A1-B80F-0DDDF398DFE1
34. Link a DWG File and Use Shared Coordinates - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-0C6D8890-73D9-4DAC-A0A6-84C45E54C6A8
35. Control Visibility of Datums Using Scope Boxes - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-57084384-4D1E-498A-A6F9-CE5C7260A088
36. Design Options - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-D48B1E7E-BC34-414E-85BD-790F199BB2C0
37. Groups of Elements - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-52612B0F-43AA-47AF-A76C-BB0E3DD24E34
38. Arrays of Elements - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-4029D78F-BE9D-4802-88E9-2881561CDFBD
39. Split Walls Horizontally - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-719EDC0D-BB98-4B25-B7A6-35AF208970D7
40. Copy an Array - https://help.autodesk.com/view/RVT/2027/ENU/?guid=GUID-D0D3D594-7430-409A-9E76-6F4F450725FE
