# SketchUp Parity Register coverage

Rows: 635 (all tier_now None, status open; no free credit). Schema: schema/row.schema.json. Ids: D8-SKETCHUP-<DOMAIN>-<nnnn>.

Method: clean-room. Public vendor documentation only (help.sketchup.com sitemap harvested 13 Nov 2026 and each cited page fetched; ruby.sketchup.com class pages, 154 classes / 2052 methods; developer.sketchup.com; extension vendor and community listing pages). Rows are written in our own words; every row names its source URL in source.doc. Each acceptance entry carries a test_id, a corpus path and a sketch of the check; each row has a superiority note.

Release basis: SketchUp 2026.0 (7 Oct 2025), 2026.1 (9 Dec 2025, AI), 2026.2 (19 May 2026, Analysis Hub), 2027.0 (21 Oct 2026, Procedures, unified UI, multi-document). The 2027.0 items go beyond the report and are included because they shipped before the delivery date.

## Rows by domain

| domain | rows |
|---|---|
| ai-tools | 15 |
| analysis-hub | 13 |
| c-api-sdk | 17 |
| collaboration-cloud | 18 |
| drawing-tools | 45 |
| dynamic-components | 21 |
| edit-tools | 39 |
| extension-platform | 14 |
| groups-components-tags | 31 |
| import-export | 23 |
| inference-engine | 24 |
| interface-preferences | 18 |
| layout-documentation | 60 |
| ruby-api | 134 |
| sandbox-terrain-geolocation | 21 |
| scan-essentials | 15 |
| scenes-styles-materials | 34 |
| solid-tools-procedures | 20 |
| style-builder | 7 |
| top-extensions | 66 |

## Rows by factory

| factory | rows |
|---|---|
| analysis-site | 37 |
| collab-cloud | 19 |
| direct-modeling | 235 |
| drafting-docs | 60 |
| interop-migration | 24 |
| render-anim | 49 |
| scripting-api-ai | 180 |
| simulation | 13 |
| ux-access-i18n | 18 |

## Rows by source kind

| kind | rows |
|---|---|
| component | 151 |
| feature | 208 |
| setting | 18 |
| tool | 258 |

## Source hosts

| host | rows |
|---|---|
| help.sketchup.com | 418 |
| ruby.sketchup.com | 136 |
| extensions.sketchup.com | 36 |
| sketchucation.com | 15 |
| mindsightstudios.com | 5 |
| support.mindsightstudios.com | 5 |
| developer.sketchup.com | 4 |
| community.sketchucation.com | 4 |
| forums.sketchup.com | 3 |
| www.toolfarm.com | 3 |
| blog.sketchup.com | 2 |
| sketchup.trimble.com | 2 |
| www.dummies.com | 1 |
| github.com | 1 |

## Known gaps and honesty notes

- Source access: help.sketchup.com (sitemap of 774 English pages, 434 relevant pages fetched), ruby.sketchup.com (154 classes, 2052 methods read) and developer.sketchup.com were reachable. Extension Warehouse pages and the C API reference are dynamic sites that cannot be read as text, so extension rows cite vendor, community and press pages found by public search, and C API rows carry a note to confirm per-call coverage against the downloaded SDK documentation.
- Top-100 extensions: about 55 distinct extensions are covered with function-level rows (Profile Builder, CleanUp, Solid Inspector, Curviloft, Joint Push Pull and the Fredo6 family, Artisan, SUbD, Skatter, Skelion, Eneroth tools, Medeek, Skalp, renderer and XR bridges and others). Ranks of the remaining extensions must be pinned from the live Extension Warehouse download ranking during the licensed-copy walkthrough; the register has no placeholder rows for them.
- Not verified on a licensed copy: tolerances in acceptance sketches are targets proposed by the researcher; they must be confirmed against black-box runs on the licensed program.
- Release basis includes 2027.0 (21 Oct 2026) and the AI credit renaming to Trimble Credits (9 Nov 2026), which postdate the report.
- Style Builder is out of support since the 2026 line; rows keep it because legacy .style files must still work.
- Rows for the Ruby API are one per class (150 of 154 classes; the module-only entries Layout, Layout::LockedEntityError, Layout::LockedLayerError and Sketchup::Http are covered through their member classes). Observer classes are grouped in three family rows.
