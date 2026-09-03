# Wildkey

A nature-identification web app scaffold, built toward parity with iNaturalist + Seek before any "exceed" feature. Full product/design spec: [`docs/wildkey-plan.md`](docs/wildkey-plan.md). Design for the remaining unbuilt systems (real CV model, map rendering, Projects/Journals/Guides, curator tools, i18n, production database): [`docs/remaining-systems-design.md`](docs/remaining-systems-design.md).

"Wildkey" is a placeholder name — swap the brand before production.

## Stack

Next.js (App Router) + TypeScript + Tailwind CSS v4. The backend lives in the same app as API routes, backed by a real SQLite database (`better-sqlite3`) — see below.

## What's built so far

This is a Phase 0/early-Phase-1 build, not a finished parity build, but every screen reachable from primary navigation is real, working functionality against a real backend — not a static mockup or a "planned features" stub.

- **One app, two modes**: a `Quick ID` / `Naturalist` toggle in the top bar (`src/lib/mode-context.tsx`), persisted to `localStorage`, driving mode-aware copy and data source across the app — the core structural decision in Part A.3 of the plan.
- **Design tokens** (`src/app/globals.css`): nature-toned neutral palette (soil/bark/moss/sky), a single accent color reserved for primary actions and confidence indicators, the 4/8/12/16/24/32/48/64px spacing scale, 8px/4px corner radii, and full dark-mode parity — per Part E. A custom taxon-group color system (`src/lib/taxon.ts`, `TaxonBadge`) is used consistently across Explore, Identify, and species pages.
- **Navigation shell**: a top bar (desktop nav + mode toggle) and a 5-tab bottom nav (Camera, Explore, Observations, Activity, Me) per Part B.2, plus a real app icon (`src/app/icon.svg`).
- **Quick ID capture flow** (`/camera`): photo upload/preview and a mock identify step producing a result card with a visible confidence % and an honest "not confident" fallback state instead of a guessed species dressed as fact (Part C.1). The mock model call is isolated in one place (`runMockIdentify`) so it's a clean swap-in point for a real on-device model.
- **Accounts on a real database** (`src/lib/server/db.ts` + `store.ts`, `src/app/api/**`): signup/login/logout with hashed passwords (`node:crypto` scrypt) and httpOnly session cookies, backed by SQLite (WAL mode, foreign-key cascades — deleting a user really does cascade-delete their sessions/observations/comments at the database level, not via hand-rolled JS filtering). `/login` and `/signup` are real pages against this API.
- **Full observation record** (Part C.2): photo, species, an optional place name, optional notes, and a wild-vs-captive Data Quality flag, captured on save in Naturalist Mode.
- **Observations, two ways**: Quick ID Mode always saves locally (`src/lib/observations.ts`, `localStorage`) with a simulated but visible sync state (Queued → Uploading → Confirmed/Failed, retryable) — no account required, matching Part C.1. Naturalist Mode, once signed in, saves through the real API (`src/lib/api-observations.ts`) instead, scoped per-account server-side.
- **Community ID** (`/observations/[id]`): any signed-in Naturalist Mode user can view another user's observation, comment on it, or agree with the ID — real cross-account threading, one agree per user.
- **Quality-grade pipeline**: an observation's grade (Needs ID vs. Research Grade) is computed server-side from independent community agreement — 2+ agrees from users other than the observer, matching Part C.2 and how iNaturalist itself excludes the observer's own ID from the count.
- **Sensitive-species location obscuring** (Part C.2): a species flagged `sensitive` in `src/lib/mock-species.ts` has its observations' location hidden from everyone but the owner, enforced server-side (not just in the UI) — verified against the API directly, not just the rendered page.
- **Explore** (`/explore`): working grid/list toggle with taxon-group filter chips over the mock species set.
- **Identify queue** (`/identify`): signed-in Naturalist Mode users get a real queue of other users' observations still below Research Grade — agreeing posts a real comment and feeds the same quality-grade pipeline. Everyone else (Quick ID Mode, or signed out) sees the same UI over clearly labeled sample data instead.
- **Species profile page** (`/species/[taxon]`): name, photo placeholder, description, seasonality, sighting count, and a danger/toxicity badge slot.
- **Me/profile** (`/me`): real account state (sign in/out) and live stats (observation/species/badge counts).
- **Activity** (`/activity`): real notifications — other users' comments/agrees on your own observations.
- **Lite Mode** (`/settings/lite-mode`): a real, persisted toggle; on, every observation photo across the app renders as a tap-to-load placeholder instead of an `<img>` until explicitly requested — verified in an actual browser, not just by reading the code.
- **Data export, account deletion & anonymization** (`/settings/data`): one-tap JSON export of everything an account owns; permanent, typed-confirmation account deletion with real cross-table cleanup; and anonymization as the alternative — keeps every observation/comment/journal post/etc. attributed and intact, but replaces the account's email/password with an unrecoverable placeholder and destroys all sessions, with the denormalized comment-author email kept in sync everywhere.
- **Journals** (`/journal`): full CRUD blog-style posts, public reads, author-scoped writes, real-account-cascade-safe.
- **Guides** (`/guides`): curated, server-validated species collections anyone can publish.
- **Projects** (`/projects`): Collection projects are a genuine live saved search over the observations table (no re-save needed when a new matching observation appears); Traditional projects have real opt-in membership.
- **Curator tools** (`/curator`, Part F): any user can flag an observation with a required reason; the first account on a fresh database bootstraps as curator (documented demo-only mechanism — no invite flow exists yet) and can resolve/dismiss flags, but only with a written reason, logged and auditable.
- **i18n** (`src/lib/locale-context.tsx`): a real, persisted locale switcher and dictionary-based `t()` function, proven end-to-end (including in an actual browser) against three hand-written languages — English, Spanish, French — covering nav/home/camera. Not the 20+ languages Part C.1 asks for; see below for why.
- **Rendered map** (`/explore`, "Map" view, `src/components/map-view.tsx`): a real MapLibre GL map over free OpenStreetMap raster tiles (no API key), wired to the bbox data layer — panning/zooming refetches real observations in view, plotted as taxon-colored pins, click-through to the observation detail page. Sensitive species show their grid-snapped location, same as everywhere else. **Caveat, checked not assumed**: this sandbox's network policy blocks `tile.openstreetmap.org` (confirmed via the outbound proxy status, not guessed), so map *tile* rendering itself couldn't be visually verified here — everything else was, in a real browser: the map mounts, its WebGL canvas and zoom controls render, and a marker for a real observation created through the API plots at the right place. A real user's browser fetches tiles directly and isn't behind this sandbox's proxy, so the feature should render normally in production; only local verification of tile pixels was out of reach here.
- **Map data layer** (`GET /api/observations/near`): a real bounding-box query over observation coordinates, with sensitive species snapped to a coarse ~11km grid cell instead of exact coordinates for non-owner viewers.
- **Automated test suite** (`tests/api.test.mjs`): 18 tests / 13 suites, Node's built-in test runner, against a real server and real database — see [Testing](#testing) below.
- **Accessibility pass** (Part H gate): every color pair actually measured against WCAG AA (two real failures found and fixed — a badge background that only hit 3.92:1 with white text, and text-muted-on-border badges at 4.39:1 — not just eyeballed), every placeholder-only text input given a real `aria-label`, and `<html lang>` now tracks the active i18n locale instead of staying hardcoded to `"en"`. Not a full audit (no screen-reader walkthrough performed), but the concrete, checkable parts of the Part H gate are done.

## What's deliberately not built yet

One thing remains genuinely gated on a real resourcing decision this environment can't make on its own: a trained CV model (identification is still a random mock over `src/lib/mock-species.ts`) — needs either a licensed/fine-tuned model or a hosted inference API, neither of which this sandbox has credentials for. Multi-language support beyond the 3 proven languages is a translation-content problem now, not an engineering one. Satellite imagery (vs. the current OSM street map) needs a paid tile provider and an API key. Full designs for all of this: [`docs/remaining-systems-design.md`](docs/remaining-systems-design.md). See `docs/wildkey-plan.md` Part H for the gate that must pass before Phase 1 work starts, and Part K for the full parity QA checklist.

## Development

```bash
npm install
npm run dev      # http://localhost:3000
npm run lint
npm run build
npm test          # builds, then runs tests/api.test.mjs against a real server + fresh SQLite db
```

Signup/login writes to `.data/wildkey.sqlite3` in the project root (gitignored). Delete `.data/` to reset local accounts/observations.

## Testing

`tests/api.test.mjs` (Node's built-in test runner, no extra framework dependency) boots a real `next start` server on a dedicated port against a fresh, disposable SQLite database and drives it entirely through `fetch` — no mocking of routes, the store, or the database. It covers the backend logic most at risk of silent regression: the quality-grade threshold (independent agrees only, self-agree excluded), sensitive-species obscuring (location text and grid-snapped coordinates) for both the API and the map bbox endpoint, ownership/role enforcement on every write path, the Collection-project live saved search, and account-deletion cascade behavior. `tests/server.mjs` is the shared server lifecycle + auth helper; every `it()` gets its own signed-up account.
