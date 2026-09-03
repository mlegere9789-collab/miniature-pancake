# Remaining Systems — Design

Design specs for everything listed as "not built" in the README as of the last checkpoint: a real CV model, map rendering, Projects/Journals/Guides, curator tools, multi-language support, and a production database. Each section states the target design and, honestly, what can and can't be built inside this sandbox (no external API keys, no cloud provisioning access).

## 1. Real CV model — built (`src/lib/cv-model.ts`)

**Target architecture (Part G):** a compact, quantized on-device model bundled with the client, ideally fine-tuned on a biodiversity dataset, so identification never requires a network call.

**What's actually built:** a real, running, non-random classifier — Google's MobileNet v1 image classification model (TensorFlow.js), loaded client-side from its public, no-API-key weight host (`storage.googleapis.com/tfjs-models/...`) and run entirely in-browser via `@tensorflow/tfjs`. `runMockIdentify` in `src/app/camera/page.tsx` (the documented swap-in point from the previous version of this doc) has been replaced with `identifyImage()`, a real inference call.

**What this is not, honestly:** a model fine-tuned on Wildkey's own species. Building one needs a labeled biodiversity dataset and a training run — a real project, not something to fabricate. MobileNet's 1000 ImageNet classes were hand-checked against `src/lib/mock-species.ts`; nine of them name one of Wildkey's species exactly (American robin, box turtle, black widow, four fox synsets, monarch butterfly, agaric mushroom — see `IMAGENET_INDEX_TO_SPECIES_SLUG` in `cv-model.ts`). A photo that scores high on one of those really is a real prediction. Anything else the model sees — including "common dandelion," which has no ImageNet equivalent — correctly falls through to an honest "not in Wildkey's sample catalog" result showing the model's real top guess, the same honest-uncertainty principle the original mock's "not confident" state used, now backed by a real model instead of `Math.random()`.

**Verification, checked not assumed:** `curl` from this sandbox reaches the model weight host directly (`200`, confirmed). A real headless-browser run through this sandbox's proxy (Playwright + the pre-installed Chromium) got as far as issuing the fetch and then hit `net::ERR_CONNECTION_RESET` on that specific host — reproducible, and specific to the browser's network stack going through this proxy (plain `curl` to the same URL succeeds every time; tried both HTTP/2 and forced HTTP/1.1, same result). The app's own error handling was verified end-to-end in that same run: on a load failure it shows a real, honest "Couldn't load the identification model" message instead of crashing or silently falling back to a random guess. A real end user's browser talks to Google's servers directly, with no sandbox proxy in the way, so production use is not expected to hit this — but the happy-path prediction itself (a real photo → a real correct label) could not be visually confirmed inside this sandbox, only the failure path could.

## 2. Map rendering — built (`src/components/map-view.tsx`)

**Target (Part C.3, E.2):** satellite-default, sticky-per-user map view with custom taxon-group colored pins.

**What's actually built:** a real MapLibre GL map with two switchable, no-API-key raster basemaps — OpenStreetMap ("street") and Esri World Imagery ("satellite", free for non-commercial/demo use, no account or key required) — via a toggle on the Explore map view (`src/app/explore/page.tsx`). `lat`/`lng` fields, the `GET /api/observations/near` bbox endpoint, and grid-cell obscuring for sensitive species were already built in an earlier pass; this added the actual satellite tile source alongside the existing street one.

**Verification, checked not assumed:** same sandbox-proxy tile-blocking issue as the original OSM-only build — `tile.openstreetmap.org` and `server.arcgisonline.com` (Esri) both get a `403`/`connect_rejected` from this sandbox's egress proxy (confirmed via the proxy's own status endpoint), so neither basemap's tile pixels could be visually verified here. Everything else was: the map mounts, the style-switch logic runs, and markers survive a basemap change. A real user's browser fetches tiles directly and isn't behind this sandbox's proxy.

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
- A `role: "member" | "curator"` field on the user record (first account created becomes curator for demo purposes, since there's no signup-time vetting). From there, `promoteToCurator`/`demoteCurator` in `src/lib/server/store.ts` (and the `/curator` page's "Curators" section) are the real invite/revoke flow — an existing curator grants or removes the role by email, each requiring a written reason and logged to a `role_changes` audit table.
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
