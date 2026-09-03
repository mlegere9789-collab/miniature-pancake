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
- **Data export & account deletion** (`/settings/data`): one-tap JSON export of everything an account owns, and permanent, typed-confirmation account deletion with real cross-table cleanup.

## What's deliberately not built yet

A real CV model, map rendering, Projects/Journals/Guides, curator/moderation tools, and multi-language support are all still open — each has a concrete design in [`docs/remaining-systems-design.md`](docs/remaining-systems-design.md), including which parts are genuinely buildable here (no external credentials needed) versus gated on a real resourcing decision (a trained model, a map tile provider, professional translation). See `docs/wildkey-plan.md` Part H for the gate that must pass before Phase 1 work starts, and Part K for the full parity QA checklist.

## Development

```bash
npm install
npm run dev      # http://localhost:3000
npm run lint
npm run build
```

Signup/login writes to `.data/wildkey.sqlite3` in the project root (gitignored). Delete `.data/` to reset local accounts/observations.
