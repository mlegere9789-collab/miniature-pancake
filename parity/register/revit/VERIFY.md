# Revit register verification (2026-11-14)

Scope: every row in `parity/register/revit/*.jsonl` was checked against the public Autodesk Revit 2027 help (contents tree, topic pages, API developer guide and API reference served from help.autodesk.com). This file is the honest record of what was and was not verified.

## Counts

| Measure | Rows |
|---|---|
| Original rows checked | 904 |
| source.doc and source.ref replaced with a specific page URL and title | 859 |
| Confirmed as stated (statement unchanged; page located) | 499 |
| Corrected (statement rewritten because it differed from the documentation, or was too broad, narrow or misattributed) | 360 |
| Rejected (feature not found in Revit 2027 help or API; row kept, `REJECTED:` in notes) | 12 |
| Unverified (feature plausible but no page could be read; `UNVERIFIED` in notes, old help-root doc retained) | 33 |
| Rows added (documented features missing from the register, plus release rows) | 130 |
| Total rows now | 1034 |

The two release placeholders (D8-REVIT-REL-0017, -0018) were filled from the official 2027.1 and 2027.3 release notes; 2027.2, 2027.4 and the 2027.0.x fix updates got rows REL-0019 to REL-0021. Release rows REL-0001 to REL-0016 were re-pointed to the matching What’s New or release-notes topics and several statements corrected (for example "sliced rebar" is documented as presentation options for spliced rebar sets).

Added rows by file: analyze 4, annotate 4, api 6, architecture 11, collaborate 5, dynamo 3, family-editor 5, forma-connected 3, ifc 2, insert 4, interop 7, manage 4, massing-site 2, modify 1, parameters 2, release-2027x 3, schedules-tags 4, structure 22, systems 11, ui-performance 5, view 11, view-control 8, worksharing 3.

## Rejected rows

These were kept, never deleted. "Not found" means not found in the Revit 2027 help or API reference after multiple searches; several describe goals for the new program (superiority or quality targets) rather than a Revit feature. I re-ran my own searches on the system/MEP, IDS, coordination space and merge-surface items and reached the same conclusion.

| Id | Feature | Reason |
|---|---|---|
| D8-REVIT-COLLAB-0014 | Collaborate: Coordination Space (Cloud Collaboration) | No 'Coordination Space' feature found in Revit 2027 help after queries for coordination space, design collaboration, shared package/team space, external stakeholders; closest documented behaviours (Publish Cloud Models, Shared Views) are covered by other rows. |
| D8-REVIT-IFC-0028 | IFC Round Trip Fidelity | Revit docs say IFC exchange standards cannot give an accurate 1:1 translation back into Revit (Using IFC Files, Open an IFC File); no round-trip fidelity measurement feature exists. This is a quality goal for the new program, not a Revit feature. |
| D8-REVIT-IFC-0030 | IFC Property Set Definitions (IDS) | Searched IFC, IDS, Information Delivery Specification and buildingSMART validation across help including What's New; Revit 2027 help documents no IDS-based validation of IFC files. |
| D8-REVIT-INTEROP-0013 | Point Cloud Registration | Revit does not register scans: About Point Clouds says scans are registered in scanner/ReCap software before the indexed RCP/RCS is linked; no control-point registration tool exists in Revit help (searched point cloud, ReCap, registration). |
| D8-REVIT-MASS-0023 | Merge Surface | No Merge Surface tool in 2027 help or API docs; Toposurfaces topic says legacy toposurfaces only allow visibility/graphics edits, contour display and conversion to a toposolid, and the toposolid topics list no merge command. |
| D8-REVIT-SYS-0063 | Sanitary Slope Rules | No size-based default slope rule or slope-violation warning exists in the docs; the Slopes pane only holds a project list of slope values (see Specify Slope Values), queries on slope/minimum slope/warning in help and API found nothing. |
| D8-REVIT-SYS-0075 | MEP Clearance Zones | No equipment clearance zone feature exists in the help or API; searches for clearance, clearance zone, maintenance and service access returned only unrelated topics. |
| D8-REVIT-SYS-0077 | MEP Opening and Sleeves | No MEP sleeve or automatic penetration/opening placement tool found in help or API; searches for sleeve, penetration, mep opening and opening duct wall only returned generic wall/shaft opening topics. |
| D8-REVIT-SYS-0079 | Cable Tray Fill and Capacity | No cable tray or conduit fill calculation exists in the help or API (searched cable tray fill, cable tray fill ratio, conduit fill, API CableTray fill); cable tray settings only define sizes, labels, rise/drop and angles. |
| D8-REVIT-SYS-0084 | Equipment Clearance Space Parameter | No clearance or service-space parameter exists for equipment families in the help or API (queries clearance, service clearance, maintenance access); clearances are modelled by users as ordinary geometry. |
| D8-REVIT-SYS-0088 | Hydraulic Gradeline Check | No fixture residual-pressure or hydraulic-gradeline shortfall check exists in the help or API (searched hydraulic grade, required pressure fixture, residual pressure, shortfall); Revit only reports pressure drop and system inspector worst-path data. |
| D8-REVIT-WSHARE-0016 | Worksets: Sync Time Budget | Searched sync, incremental save, compact, background processes; Revit documents only a Compact Central Model option that lengthens sync and a Background Processes list that excludes synchronize. Incremental Model Open speeds opening, not sync. The statement de |

## Unverified rows

| Id | Feature | Why |
|---|---|---|
| D8-REVIT-AI-0004 | Autocomplete Room Elements | No help page found describing AI suggestion/placement of room elements. Tried: autocomplete, room layout generative, place furniture suggestions rooms, AI generated suggest, project intelligence, --api autocomplete room (only Dynamo Node Autocomplete). |
| D8-REVIT-AI-0005 | Optimized Annotations | No help page found for AI-optimized tag/dimension placement. Tried: optimized annotations, tag overlap automatic placement, annotation clutter, project intelligence; only generic Tag on Placement and rebar annotation auto-placement exist. |
| D8-REVIT-API-0024 | Autodesk Platform Services Design Automation | Design Automation for Revit is an APS cloud service. Searched Revit 2027 help (find --all 'Design Automation', 'Platform Services', 'APS'), nothing relevant; aps.autodesk.com returns a JavaScript shell for every URL, so page content cannot be read or verified. |
| D8-REVIT-API-0025 | Autodesk Platform Services Model Derivative | Model Derivative is an APS cloud service not documented in Revit help (searched 'Model Derivative', 'APS', 'Platform Services'); aps.autodesk.com pages are a JS shell with no readable content via curl. |
| D8-REVIT-API-0026 | APS Data Management and Viewer | APS Data Management/Viewer are not in Revit help; nearest in-product topics are Forma Data Management cloud models and 'Collaborate on a Shared View in the Autodesk Viewer'. aps.autodesk.com is a JS shell, unverifiable. |
| D8-REVIT-API-0028 | Python and pyRevit Ecosystem | pyRevit and similar Python frameworks are third-party and not documented in Autodesk Revit help; searched --api and --all for Python, pyRevit, IronPython. |
| D8-REVIT-API-0029 | Rhino.Inside.Revit Style Embedding | Rhino.Inside.Revit is a third-party McNeel project; no Autodesk Revit help topic. Only Rhino mentions are the Data Exchange connector list and the 3DM import option. |
| D8-REVIT-ARCH-0134 | Casework and Furniture Categories | Tried casework, furniture, family category queries; only generic Specify Family Category and Parameters and Modify the Room Calculation Point (room-aware families) surfaced; no page documents separate Casework and Furniture categories. |
| D8-REVIT-DYNAMO-0003 | Dynamo: Lists and Lacing | Searched RevitDynamo topics (lacing, list, Dynamo, dynamo graph) and Revit help; lists/lacing are documented only in the Dynamo Primer (external, not reachable). |
| D8-REVIT-DYNAMO-0004 | Dynamo: Python Nodes | Searched RevitDynamo topics for python/script; only Steel Connections debug examples appear, no page documenting Python script nodes (Dynamo Primer is external and unreachable). |
| D8-REVIT-DYNAMO-0005 | Dynamo: Code Blocks and DesignScript | Searched RevitDynamo topics and Revit help for code block/DesignScript; no page found (documented in external Dynamo Primer/Dictionary). |
| D8-REVIT-DYNAMO-0006 | Dynamo: Custom Nodes | Searched RevitDynamo topics for custom node; only Graph Dependencies mentions custom nodes as dependencies. Creation of custom nodes is documented externally. |
| D8-REVIT-DYNAMO-0009 | Dynamo: Automation and Headless Run | Searched Revit help and RevitDynamo topics (command line, headless, work item); only Revit install/server command-line pages and Player pages found. Design Automation docs on aps.autodesk.com load as a JavaScript shell without text content. |
| D8-REVIT-DYNAMO-0010 | Dynamo: Geometry Library | Searched RevitDynamo topics and Revit help for the Dynamo geometry library; only Revit-specific node pages (GeometryReferences, T-Spline, etc.) exist, the core geometry library is documented in the external Dynamo Dictionary. |
| D8-REVIT-DYNAMO-0011 | Dynamo: Revit Element Binding | Searched RevitDynamo and Revit help (binding, element tracking); 'Binding' hits are the Revit API shared-parameter binding, unrelated. Dynamo-Revit element binding is documented outside the indexed help. |
| D8-REVIT-DYNAMO-0012 | Dynamo: Watch and Debug | Searched RevitDynamo topics; only the Player Issue Viewer (errors/warnings list with CSV export) and Steel Connections debug pages exist. Watch node and preview inspection are documented externally. |
| D8-REVIT-DYNAMO-0014 | Dynamo to Rhino and Grasshopper Bridges | Searched Revit and RevitDynamo help for Rhino and Grasshopper; no Autodesk page found. Any Rhino/Grasshopper bridge is third-party or package-based, so not a documented Revit feature. |
| D8-REVIT-DYNAMO-0015 | Dynamo: Graph Profiling | Searched RevitDynamo topics for profiling and execution time; nothing found. Possibly an external Dynamo feature. |
| D8-REVIT-IFC-0009 | IFC Export: Classification References | Searched IFC classification, OmniClass/Uniclass/Assembly Code, IFC export setup tabs and mapping pages; no 2027 topic documents export of IfcClassificationReference. |
| D8-REVIT-IFC-0010 | IFC Export: Materials and Layers | Searched IFC export material layers/composite and the IFC setup, category and parameter mapping topics; material layer set export is not described in the 2027 help. |
| D8-REVIT-IFC-0013 | IFC Export: MEP Systems and Ports | Searched IFC export of MEP systems and ports; the default category mapping and setup pages do not list IfcSystem or IfcDistributionPort. Feature plausibly exists in the exporter but is not documented. |
| D8-REVIT-IFC-0015 | IFC Export: Large Model Performance | Non-functional performance goal; no Revit help topic on IFC export time or memory limits (searched IFC export large model, performance). Related generic topics are not IFC-specific. |
| D8-REVIT-IFC-0023 | IFC Import: Spatial Hierarchy | Searched Using IFC Files, Link/Open an IFC File and mapping pages; none describe how site/building/storey/space map to levels and rooms. |
| D8-REVIT-IFC-0024 | IFC Import: Relationships | Searched IFC import pages; aggregation, type assignment and containment handling is not described in 2027 help. |
| D8-REVIT-IFC-0032 | IFC Export: Openings and Voids | Default category mapping lists OST_Reveals and OST_MassOpening as IfcOpeningElement, but no page describes IfcRelVoidsElement or opening export for doors, windows and cuts. |
| D8-REVIT-INSERT-0026 | Bind Link | Bind Link exists in Revit (mentioned on Troubleshooting 'Missing Dimensions in Linked Files') but no dedicated topic was found after searching bind, Linked Models pages and a scan of 58 Linked Models topics. |
| D8-REVIT-INSERT-0030 | Cut Geometry with Links | Cut Geometry page lists cuttable host elements within a project and does not mention linked models; searched cut/join geometry with links and openings across links, nothing found. |
| D8-REVIT-PARAM-0033 | Parameter: Formula Dependencies and Cycles | Searched formula, circular, cyclic, self-referencing and error topics in help and API; no page documents circular-reference detection in formulas (Troubleshooting: Formulas for Dimensions covers only unit errors). Revit very likely reports such errors but no d |
| D8-REVIT-SCHED-0018 | Schedule: Import from Spreadsheet (ecosystem) | Searched help (find.py --all) for schedule import, Excel and spreadsheet topics plus RevitDynamo topics; no Revit 2027 page documents importing schedule values from a spreadsheet, and Dynamo Excel nodes are not in the indexed RevitDynamo pages. Likely an add-i |
| D8-REVIT-STRUCT-0068 | Rebar: Clash Check | Interference Checking is generic across categories; no page mentions rebar-to-rebar or rebar-to-host-edge checks. Queries: rebar clash, rebar interference, --api rebar interference. |
| D8-REVIT-SYS-0076 | Piping Insulation Schedules | Pipe Insulations is a schedulable category in Revit, but no help topic or API entry explaining scheduling insulation by size, thickness and length was found; tried pipe insulation schedule, insulation schedule, schedule duct pipe quantities and the API index. |
| D8-REVIT-UI-0018 | Performance: Regeneration | No help page documents regeneration timing or interactivity targets; closest is Model Efficiently/Constraints and Performance guidance, which are advice, not a feature. |
| D8-REVIT-VIEW-0024 | Windows: Replicate | Searched help for replicate/replicate window/window tile/windows panel and API index; the Drawing area page lists Switch Windows, Tab, Tile, Close Inactive and User Interface but no Replicate command, though the feature plausibly exists in the Windows panel. |

## Unreachable or unusable pages

- forums.autodesk.com: HTTP 403, Cloudflare challenge page. Not read.
- www.autodesk.com (for example the Revit product overview and the keyboard-shortcut PDF it hosts): HTTP 403. The default shortcut letters for rows UI-0001 to UI-0003 are therefore unconfirmed; those rows were rewritten at concept level.
- aps.autodesk.com: the host answers, but every documentation URL tried returns the same script-rendered shell of about 9.7 KB with no article text, so APS service rows (API-0024 to API-0026) could not be read.
- Dynamo Primer / Dictionary hosts (primer2.dynamobim.org, dynamobim.org): connection refused by the proxy; core Dynamo rows (Dynamo-0003 to 0015 as listed above) therefore rest on nothing readable. Only the Revit-specific Dynamo pages inside the Revit help were read.
- www.revitapidocs.com: refused by the proxy; not needed because the API reference is inside the help.
- Third-party tools (pyRevit, Rhino.Inside) have no Autodesk documentation, so API-0028 and API-0029 are unverified rather than rejected.

## Method

1. Fetched the Revit 2027 contents tree (data/toctree.json, about 35,800 topics, of which about 29,500 are API reference entries) from help.autodesk.com and built a title and breadcrumb index.
2. For each row, retrieved the six best-matching candidate topics (ranking by title and breadcrumb term overlap) and fetched excerpts from each page.
3. Ten reviewers (AI sub-agents, each with a batch of roughly 60 to 117 rows) searched the index, read the pages and returned a verdict per row: confirmed, corrected, rejected or unresolved, with a reason for anything other than confirmed. Reviewers could read page text as extracted by a script; many confirmations rest on the first 1,000 to 2,500 characters of a page, and a few on a search excerpt only (flagged in the reviewers’ reasons and, where material, in the row notes).
4. I merged the verdicts, then checked that every cited URL resolves to a topic in the contents tree (75 URLs came back from a reviewer without the `GUID-` prefix and were repaired; all then matched), re-ran searches for the rejected rows, spot-checked reviewer claims against the pages (this found one wrong claim, about Tag by Example and 2027.2, which I corrected from the 2027.2 release notes), and compared every statement with its cited page for runs of identical words (nine statements with long runs were reworded).
5. The gap pass used reviewer-reported additions (verified to come from pages they read, de-duplicated against existing rows by page), the Revit 2027 What’s New topics (keyword coverage test, then manual page reads for the misses) and area checks for rendering, visualization export, the API guide, phasing, design options and interop.
6. Validation: no `parity/schema/row.schema.json` exists on this branch, so rows were validated with Python jsonschema against an inline schema built from the field list in the task (required id pattern, program, statement, source with kind, ref and doc, target Strong or Best, factory, domain, status; optional fields typed). Result: 0 errors, ids unique, 1034 rows.

## Caveats

- `status` stays `open` and `tier_now` stays `None` on every row: verification of the requirement text is not implementation progress.
- The Best targets and `superiority` text on rows are Dino 8 aims, not Revit facts, and were not changed unless the row’s feature was corrected.
- Reviewers were told to keep statements in their own words; an automated check for identical runs of six or more words against the cited page was run, and the longest remaining overlap is a short run of technical terms.
- This was a documentation check only: nothing was run in Revit, so behaviours that the help describes ambiguously were corrected to what the page states, not to what the product may do.

