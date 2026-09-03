# Remaining Systems — Design

Design specs for everything listed as "not built" in the README as of the last checkpoint: a real CV model, map rendering, Projects/Journals/Guides, curator tools, multi-language support, and a production database. Each section states the target design and, honestly, what can and can't be built inside this sandbox (no external API keys, no cloud provisioning access).

## 1. Real CV model

**Target architecture (Part G):** a compact, quantized on-device model (e.g. a MobileNetV3-class classifier fine-tuned on a biodiversity dataset, exported to TensorFlow Lite / ONNX Runtime Web) bundled with the client, so identification never requires a network call — the non-negotiable requirement from Part C.1/G.

**Pipeline:**
1. Cold-start corpus: license/derive from GBIF occurrence data + iNaturalist's own open taxon photos (CC-licensed subset) or a pretrained iNat-2021 checkpoint, fine-tuned toward the taxa in our launch region.
2. Export to a web-runnable format (ONNX Runtime Web / TF.js) for the browser client, and to TFLite for native mobile.
3. Client inference: capture → resize/normalize → run model → top-k softmax → map to taxon records → confidence-gated display (the honest-uncertainty UX already built stays the same; only the source of `confidence` changes).
4. `runMockIdentify` in `src/app/camera/page.tsx` is the exact swap-in point — it already isolates "given a photo, produce {species, confidence}" from everything downstream (result card, save flow, quality grade). Replacing its body with a real inference call requires no changes elsewhere.

**Why not built here:** a real model needs a training/fine-tuning run against a licensed dataset and a hosting or bundling decision (model file size vs. app size budget) — both are offline, resource-intensive steps outside what this sandbox can produce or download (no dataset/model-weight hosting reachable here). This is genuinely gated on a resourcing decision, not more scaffolding.

## 2. Map rendering

**Target (Part C.3, E.2):** satellite-default, sticky-per-user map view on Explore, Species, and Place pages, with custom taxon-group colored pins (`src/lib/taxon.ts` already defines the color system to reuse).

**Design:**
- Tile provider: Mapbox GL JS or MapLibre GL (open-source, no vendor lock, works with any tile source) — MapLibre preferred so no API key is required for the open-tile fallback (e.g. OpenStreetMap raster tiles), with Mapbox satellite tiles as the paid upgrade path per the "satellite default" spec.
- `src/components/map/` would hold: `MapView` (MapLibre wrapper, view-state persisted to `localStorage` under `wildkey.mapStyle` mirroring the `mode`/`liteMode` context pattern already in this codebase), `TaxonPin` (SVG marker colored via `taxonColor()`), and a `useObservationsInBounds` hook backed by a new `GET /api/observations/near?bbox=` endpoint added to the store.
- Data model change needed first: observations currently store `locationName` (free text) only — real map pins need `lat`/`lng`. Add optional `lat`/`lng` fields alongside `locationName`, populated from the browser Geolocation API or manual pin-drop, obscured server-side for sensitive species (extending the same `obscureLocationIfSensitive` helper already built) by snapping to a coarse grid cell instead of hiding entirely.

**Why not built here:** rendering an actual basemap requires either a live tile-fetching network call to a provider this sandbox may not have unrestricted egress to, or a bundled offline tile set (large binary asset, not something to fabricate). The pin/data-layer half (lat/lng fields, the bbox query endpoint, the obscuring-by-grid-cell logic) is plain application code with no such dependency and is the next concrete slice worth building without a real map underneath it yet.

## 3. Projects, Journals, Guides

**Data model (extends the existing file store the same way comments/activity did):**
```
Project { id, ownerId, type: "collection" | "traditional", title, description,
          searchFilter (for collection type: taxonSlug[]/place/dateRange),
          memberIds (for traditional type), createdAt }
JournalPost { id, authorId, title, body, observationIds[], createdAt, updatedAt }
Guide { id, curatorId, title, description, taxonSlugs[], createdAt }
```
**Routes:** `/projects`, `/projects/[id]`, `/journal`, `/journal/[id]`, `/guides`, `/guides/[id]`, mirroring the CRUD + list pattern already established for observations (owner-scoped writes, public reads, comment-style activity where relevant).

**This is fully buildable now** — no external dependency, same file-store pattern as everything shipped so far. Building next.

## 4. Curator / moderation tools

**Design (Part C.2, F):**
- A `role: "member" | "curator"` field on the user record (first account created, or a seeded flag, becomes curator for demo purposes — real deployment would need an invite/vetting flow, out of scope here).
- `POST /api/observations/:id/flag` — any signed-in user can flag an observation (reason + optional note), visible only to curators.
- `/curator` route: a queue of flagged observations with resolve/dismiss actions and a required written reason (Part F: "any account action includes a clear, appealable written reason"), stored as a new `curatorAction` record so it's auditable, not just a state flip.
- Taxonomy correction (retargeting an observation's `taxonSlug`) is a curator-only action logged the same way.

**This is fully buildable now** — no external dependency. Building next, after Projects/Journals/Guides.

## 5. Multi-language support

**Target (Part C.1):** 20+ languages at launch, matching Seek.

**Honest scope for this sandbox:** shipping 20+ real translations requires either professional translation or an MT pipeline against a translation API this sandbox doesn't have credentials for — faking 20 languages with untranslated or machine-mistranslated strings would be worse than not claiming it. What **is** honestly buildable: the actual i18n *mechanism* — message extraction, a locale switcher, and 2–3 real, hand-written translations (e.g. English + Spanish + French, covering the highest-traffic screens: home, camera, nav) — proving the plumbing works end to end so adding language #4 through #20 later is purely a translation-content problem, not an engineering one. Building this next using `next-intl` (or a minimal hand-rolled dictionary if a translation library adds too much surface for the remaining time) is realistic.

## 6. Production database

**Target (Part G):** swap the file-based JSON store for a real database before any multi-instance deploy — documented as the explicit gap since the store was first introduced.

**What's honestly buildable here without external provisioning:** SQLite via `better-sqlite3` (or Next-compatible driver) as a real embedded SQL database — ACID transactions, proper concurrent-write safety (the file store's documented weakness), real indices — with the exact same function surface (`src/lib/server/store.ts`'s exported functions) so no API route or page needs to change, only the implementation underneath. This is *not* the final answer for a horizontally-scaled deploy (SQLite is single-file/single-writer-friendly, not multi-region), but it is a genuine, correctness-improving step up from hand-rolled JSON-file read/write, and the migration to a hosted Postgres later is a driver swap behind the same function signatures, not a rewrite. Building this next — first in the queue, since every other feature above depends on the store being sound.

---

## Build order taken from here

1. **SQLite swap** — foundation everything else writes through.
2. **Projects, Journals, Guides** — pure application code, no missing infra.
3. **Curator tools** — pure application code, no missing infra.
4. **i18n mechanism + 2–3 real languages** — pure application code, no missing infra.
5. Map data-layer groundwork (lat/lng fields, bbox query, grid-cell obscuring) **without** a rendered basemap, since a live tile provider isn't reachable/fundable from here — left as the one item still gated on an external resourcing decision, same as the CV model.
