# Review of the Forma register (`parity/register/forma`)

Reviewer: independent, did not write the rows. Date: 2026-11-14. Branch: `w0-review-b`.

**Verdict: ACCEPT WITH FIXES.** The register is honest about what it could not read (every row note starts VERIFIED, PARTIAL or UNVERIFIED and my sample agrees with those tags), but 20% of rows rest on pages nobody could open, and the extension rows sit in the wrong factory.

## Sample

- Random sample: 60 of 186 rows. Top-up: 42 more so each of the 11 domain files has at least 10 (script checks only). 102 rows, 49 distinct pages; 76 of 102 rows had a readable page.

How the pages were read. The help site is a script-rendered app whose page-lookup service is blocked from this environment, so the `?guid=` links open "Page Not Found" here. I read the static files instead: `/cloudhelp/<release>/ENU/<book>/files/<id>.htm`, with the book folder taken from each product's `data/toctree.json` (which also gives the page title, so I could check `source.ref` against the page title for every row that is in a table of contents). Pages were fetched with a plain browser User-Agent; `www.autodesk.com`, `blogs.autodesk.com` and `forums.autodesk.com` still refused (HTTP 403 or no connection). Sampling: `random.Random(8).sample` over the sorted ids, then topped up to at least 10 rows from every domain file ("top-up" rows). Rates below use the random sample only; top-up rows were checked by script only (reachable, copy overlap, factory rule, Best rule) and are reported separately.

Criteria (a row can fail several):
- **A** page reachable and relevant (the cited page opens, and its title matches `source.ref`).
- **B** statement describes what the page documents, in a testable way, without copying (content not on the cited page, or a run of 8+ identical words, counts as a defect).
- **C** `target` sensible: Best must be rare and justified by a `superiority` text.
- **D** `factory` or `domain` right.
- **E** statement is a testable outcome, not just a label or a note about a release.
- **F** duplicate of another row (same behaviour, same page).

Extra reading tools for this program: `aps.autodesk.com` pages are script-rendered, so I read the eight sampled ones through a page-summary reader and asked yes/no questions (a small model summarises, so treat these as good but not perfect evidence). Rows citing the npm registry URL were checked against the package tarball (type declarations), which is public.

## Results on the random sample (n = 60)

| criterion | base-sample count | rate | row ids |
|---|---|---|---|
| A page unreachable/irrelevant | 10 | 10/60 = 16.7% (95% CI 9-28%) | `ANALYSIS-0008`, `ANALYSIS-0010`, `ANALYSIS-0023`, `ANALYSIS-0025`, `ANALYSIS-0036`, `LAYOUT-0002`, `SITE-0010`, `SITE-0015`, `STREET-0001`, `STREET-0003` |
| B statement not supported / copied | 3 | 3/60 = 5.0% (95% CI 2-14%) | `BLDG-0009`, `EXT-0001`, `EXT-0010` |
| C Best without justification | 0 | 0/60 = 0.0% (95% CI -0-6%) | none |
| D factory or domain wrong | 11 | 11/60 = 18.3% (95% CI 11-30%) | `EXT-0001`, `EXT-0006`, `EXT-0010`, `EXT-0013`, `EXT-0014`, `EXT-0021`, `EXT-0023`, `EXT-0024`, `EXT-0026`, `EXT-0029`, `EXT-0030` |
| E label or not testable | 0 | 0/60 = 0.0% (95% CI -0-6%) | none |
| F duplicate | 2 | 2/60 = 3.3% (95% CI 1-11%) | `ANALYSIS-0028`, `EXT-0006` |

Rows with at least one defect: 23/60 = 38.3%. Top-up rows (script checks only, n = 42): unreadable page 16 (38.1%), copy overlap 5 (11.9%).

Extra findings:
- 14 of the 60 rows (40 of 186 in the register, 21.5%) cite `https://registry.npmjs.org/forma-embedded-view-sdk/0.96.0`. That URL is package metadata, not the declaration file. I downloaded the tarball and checked four claims (500 by 500 height rasters at 1.5 m and 200 by 200 results in `EXT-0018`, the validity mask in `ANALYSIS-0041`, parking statistics in `SITE-0013`, the comfort scale list for `ANALYSIS-0012`): all held. So the statements are right; the citation does not let a reader verify them. Cite the `.d.ts` path inside the package.
- 12 of 60 sampled rows are tagged PARTIAL and 10 UNVERIFIED by the register itself (full register: 109 VERIFIED, 39 PARTIAL, 38 UNVERIFIED). The 10 sampled UNVERIFIED rows are exactly the 10 whose page I could not read. PARTIAL rows deliberately say more than the page (for example `ANALYSIS-0017`, `-0022`, `-0030` cite the Site Design overview, which only lists capability names; the kWh per square metre, comfort bands and share-of-view outputs come from elsewhere). That is flagged, so I do not count it as an untagged B defect.
- Two Best rows carry UNVERIFIED notes (`ANALYSIS-0010`, `ANALYSIS-0036`): Best should not sit on a row with no readable source.
- B defects (VERIFIED rows whose page does not support the statement): `EXT-0010` (the client-credentials flow is not on the cited page, which describes the user authorisation flow only), `EXT-0001` (no extension management page on the cited page), `BLDG-0009` (a 10-word run copied).

## Systematic problems

1. **Sources that could not be opened.** 26 of the 102 sampled rows (10 of 60 random) cite `blogs.autodesk.com` (9 rows), `www.autodesk.com` (8), `adsknews.autodesk.com` (6), `aecmag.com` (2) or `architosh.com` (1). In this environment `www.autodesk.com` answered 403 Access Denied (even with a browser User-Agent), `blogs.autodesk.com` did not connect, and `forums.autodesk.com` served a challenge page. The task brief says these are reachable; they were not. Site Design analysis, Street Design, Layout Explorer and carbon rows rest on these pages.
2. **Extension rows are in collab-cloud.** 33 of 37 `extensions` rows (SDK calls, HTTP API, authentication, configuration, sharing) are `collab-cloud`; they are developer-API rows (scripting-api-ai). Eleven sampled rows affected.
3. **Duplicate:** `ANALYSIS-0028` and `EXT-0006` (same `areaMetrics.calculate`), `BLDG-0010`/`INTEROP-0004` overlap on connected data.
4. **Landing-page citations.** Several rows cite a product overview page for specific numeric behaviour; the PARTIAL tag covers this but it means those rows are product knowledge, not page-backed requirements.
5. Best is rare (8 of 186, 4.3%) and all have a superiority text, but two are on UNVERIFIED rows.

What is good: page identity is right for every readable page; statements are specific; the tags are honest; SDK claims check out against the package.

## Unreachable pages

- `blogs.autodesk.com/forma/2026/07/17/introducing-total-carbon-analysis-in-forma-site-design...`, `.../2026/04/07/total-carbon-analysis-in-forma-building-design...`, `.../2026/08/28/forma-site-design-tools-overview/`, `.../2023/12/11/site-context-in-revit-via-forma`
- `www.autodesk.com/campaigns/forma-street-design`, `/learn/ondemand/tutorial/analysis-in-forma`, `/learn/ondemand/tutorial/wind-analysis-in-forma`, `/products/forma-site-design/environmental-impact-analysis`, `/products/forma-site-design/overview`
- `adsknews.autodesk.com/en/news/autodesk-design-and-make-intelligence/`, `aecmag.com/civil-engineering/autodesk-pushes-forma-upstream-and-connects-civil-3d/`, `architosh.com/2026/04/autodesk-intros-new-forma-building-design/`

## Fix file

`parity/register/review/fixes_forma.jsonl` (15 proposals): 3 narrowed statements, 11 factory moves, 1 alias link. Same note about the validator scanning `review/`.

## Missing coverage (35 documented topics with no row)

The Forma Data Management help (the Autodesk Docs table of contents) has 680 pages; 606 are the source of no row. The Forma Site Design/Board/Building help books are small and mostly covered; the gaps are in data management and the assistant. Items:

1. About Issues (Issues > Getting Started with Issues) - https://help.autodesk.com/cloudhelp/ENU/Build-Issues/files/getting-started-with-issues/Issues_About.html
2. Issue Categories and Types (Issues > Issues Settings) - https://help.autodesk.com/cloudhelp/ENU/Build-Issues/files/configure-issues/Issues_Types_Categories.html
3. Issue Permissions (Issues > Issues Settings) - https://help.autodesk.com/cloudhelp/ENU/Build-Issues/files/configure-issues/Issues_Permissions.html
4. Import Issues (Issues > Create Issues) - https://help.autodesk.com/cloudhelp/ENU/Build-Issues/files/create-issues/Issues_Import.html
5. Add Specifications (Specifications > Add and Publish Specifications) - https://help.autodesk.com/cloudhelp/ENU/Build-Specs/files/add-and-publish-specifications/Add_Specifications.html
6. About Submittal Log Generation (Specifications > Submittal Log Generation) - https://help.autodesk.com/cloudhelp/ENU/Build-Specs/files/submittal-log-generation/About_Submittal_Log_Generation.html
7. Generate Submittals (Specifications > Submittal Log Generation) - https://help.autodesk.com/cloudhelp/ENU/Build-Specs/files/submittal-log-generation/Generate_Submittals.html
8. Assets Reports (Reports > Report Types) - https://help.autodesk.com/cloudhelp/ENU/Docs-Reports/files/report-types/Assets_Reports.html
9. Cost Management Reports (Reports > Report Types) - https://help.autodesk.com/cloudhelp/ENU/Docs-Reports/files/report-types/Cost_Management_Reports.html
10. Insight Builder Set Up (Insight > Insight Builder) - https://help.autodesk.com/cloudhelp/ENU/Docs-Insight/files/insight-builder/getting-started-with-insight-builder/Insight_Builder_Set_Up.html
11. Connect to Power BI (Insight > Data Connector) - https://help.autodesk.com/cloudhelp/ENU/Docs-Insight/files/data-connector/Connect_PowerBi.html
12. Power BI Templates (Insight > Data Connector) - https://help.autodesk.com/cloudhelp/ENU/Docs-Insight/files/data-connector/PowerBI_Templates_Data_Connector.html
13. Share Data Exchanges using Bridge (Bridge > Getting Started with Bridge) - https://help.autodesk.com/cloudhelp/ENU/FDM-Bridge/files/getting-started/bridge-workflows/Bridge_Workflows_Data_Exchanges.html
14. Create Bridges to Other Projects (Bridge) - https://help.autodesk.com/cloudhelp/ENU/FDM-Bridge/files/Create_Bridges.html
15. About Bridge Automations (Bridge > Manage Bridged Projects) - https://help.autodesk.com/cloudhelp/ENU/FDM-Bridge/files/manage-bridged-projects/Bridge_Automations.html
16. Naming Standards Settings (Files > File Naming Standards) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-naming-standard/Access_Naming_Standard_Settings.html
17. ISO 19650 Template (Files > File Naming Standards) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-naming-standard/ns-set-up/ns-import/Naming_Standard_Template.html
18. Create Smart Controls (Files > Smart Controls) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-smart-controls/Create_Smart_Controls.html
19. Create Transmittal (Files > File and Folder Actions) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/file-folder-actions/Create_Transmittal_Files.html
20. Submit for Review (Files > File and Folder Actions) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/file-folder-actions/Reviews_Files.html
21. Extract Pages and Attributes Workflow (Files > Automated Drawing Extraction) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-automated-drawing-extraction/Automated_Drawing_Extraction_Extract_Pages.html
22. Create and Edit Title Block Templates (Files > Automated Drawing Extraction) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-automated-drawing-extraction/About_Title_Block_Templates.html
23. Sectioning Tools (Files > View Files on Web) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-view/files-viewer-toolbar/Section_Models_Files.html
24. Viewpoints (Files > View Files on Web) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-view/files-viewer-navigation-toolbar/Viewpoints.html
25. Scan for Duplicate Files (Files > Files Settings) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-settings/Scan_Duplicate_Files_Docs.html
26. Project Templates (Administration > Hub Administration) - https://help.autodesk.com/cloudhelp/ENU/Docs-Admin/files/hub-administration/Account_Admin_Project_Templates.html
27. Roles (Administration > Hub Administration) - https://help.autodesk.com/cloudhelp/ENU/Docs-Admin/files/hub-administration/Account_Admin_Roles.html
28. Sync and Download Files (Files > Files on Autodesk Forma Mobile App) - https://help.autodesk.com/cloudhelp/ENU/Docs-Files/files/files-on-mobile/Sync_Download_Files_Mobile.html
29. Quick AI issue creation on Android (Issues > Issues on Autodesk Forma Mobile App) - https://help.autodesk.com/cloudhelp/ENU/Build-Issues/files/issues-on-autodesk-construction-cloud-mobile-app/issues-on-mobile-android/Issues_Android_AI_Quick_Create.html
30. Creating boards (Forma Board) - https://help.autodesk.com/cloudhelp/ENU/Forma-SD-Board/files/FormaBoard_CreateBoard.html
31. Printing boards (Forma Board) - https://help.autodesk.com/cloudhelp/ENU/Forma-SD-Board/files/FormaBoard_Print.html
32. Get help and contact support in Forma Building Design (Forma Building Design) - https://help.autodesk.com/cloudhelp/ENU/Forma-SD-Building/files/Support_BuildingDesign.html
33. What can Autodesk Assistant do? (Autodesk Assistant > About Autodesk Assistant) - https://help.autodesk.com/cloudhelp/ENU/Assistant-User-Guide/files/assistant-topics/AA_Capabilities.html
34. Autodesk Assistant limitations and best practices (Autodesk Assistant > About Autodesk Assistant) - https://help.autodesk.com/cloudhelp/ENU/Assistant-User-Guide/files/assistant-topics/AA_Limitations.html
35. Trust, privacy, and your data (Autodesk Assistant > About Autodesk Assistant) - https://help.autodesk.com/cloudhelp/ENU/Assistant-User-Guide/files/assistant-topics/AA_Trust_Privacy.html
