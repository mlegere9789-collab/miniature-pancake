# Wildkey

A nature-identification web app scaffold, built toward parity with iNaturalist + Seek before any "exceed" feature. Full product/design spec: [`docs/wildkey-plan.md`](docs/wildkey-plan.md).

"Wildkey" is a placeholder name — swap the brand before production.

## Stack

Next.js (App Router) + TypeScript + Tailwind CSS v4. A minimal Node backend lives in the same app as API routes — see below.

## What's built so far

This is a Phase 0 starting point, not a finished parity build, but the core screens are real (working against mock species/CV data and a real accounts backend), not static mockups.

- **One app, two modes**: a `Quick ID` / `Naturalist` toggle in the top bar (`src/lib/mode-context.tsx`), persisted to `localStorage`, driving mode-aware copy and data source across the app — the core structural decision in Part A.3 of the plan.
- **Design tokens** (`src/app/globals.css`): nature-toned neutral palette (soil/bark/moss/sky), a single accent color reserved for primary actions and confidence indicators, the 4/8/12/16/24/32/48/64px spacing scale, 8px/4px corner radii, and full dark-mode parity — per Part E. A custom taxon-group color system (`src/lib/taxon.ts`, `TaxonBadge`) is used consistently across Explore, Identify, and species pages.
- **Navigation shell**: a top bar (desktop nav + mode toggle) and a 5-tab bottom nav (Camera, Explore, Observations, Activity, Me) per Part B.2, plus a real app icon (`src/app/icon.svg`).
- **Quick ID capture flow** (`/camera`): photo upload/preview and a mock identify step producing a result card with a visible confidence % and an honest "not confident" fallback state instead of a guessed species dressed as fact (Part C.1). The mock model call is isolated in one place (`runMockIdentify`) so it's a clean swap-in point for a real on-device model.
- **Accounts + a real backend** (`src/lib/server/store.ts`, `src/app/api/**`): signup/login/logout with hashed passwords (`node:crypto` scrypt) and httpOnly session cookies, backed by a file-based store at `.data/db.json` (gitignored, dev-only — swap for a real database before any multi-instance deploy). `/login` and `/signup` are real pages against this API.
- **Observations, two ways**: Quick ID Mode always saves locally (`src/lib/observations.ts`, `localStorage`) with a simulated but visible sync state (Queued → Uploading → Confirmed/Failed, retryable) — no account required, matching Part C.1. Naturalist Mode, once signed in, saves through the real API (`src/lib/api-observations.ts`) instead, scoped per-account server-side. `/camera`, `/observations`, and `/me` all switch data source based on mode + auth state.
- **Community ID** (`/observations/[id]`): any signed-in Naturalist Mode user can view another user's server-backed observation, comment on it, or agree with the ID — real cross-account threading (`src/app/api/observations/[id]/comments/route.ts`), not mock data. One agree per user, ownership still enforced server-side for deletes.
- **Quality-grade pipeline**: an observation's grade (Needs ID vs. Research Grade) is computed server-side from independent community agreement — 2+ agrees from users other than the observer, matching Part C.2 and how iNaturalist itself excludes the observer's own ID from the count (`QualityGradeBadge`, shown on My Observations and the detail page).
- **Explore** (`/explore`): working grid/list toggle with taxon-group filter chips over the mock species set.
- **Identify queue** (`/identify`): signed-in Naturalist Mode users get a real queue of other users' observations still below Research Grade (`GET /api/observations/needs-id`) — agreeing posts a real comment and feeds the same quality-grade pipeline. Everyone else (Quick ID Mode, or signed out) sees the same UI over labeled sample data instead.
- **Species profile page** (`/species/[taxon]`): name, photo placeholder, description, seasonality, sighting count, and a danger/toxicity badge slot (Part C.1, Part D.1).
- **Me/profile** (`/me`): real account state (sign in/out), and stats (observation/species/badge counts) computed from whichever store is active.
- **IA stubs** for `/activity`, `/settings/lite-mode`, `/settings/data` — each lists what that screen still needs from the Part C/D matrix; these genuinely need more backend surface (moderation events, real account deletion/export) than is worth mocking further right now.

## What's deliberately not built yet

A real CV model (the identify step is a random mock over `src/lib/mock-species.ts`), map rendering, projects/journals/guides, curator/moderation tools, data export/anonymization, Lite Mode's actual low-resource behavior, and all of Phase 1 (Part D). The file-based store is a local-dev stand-in for a real database — see `src/lib/server/store.ts` for the swap-out point. See `docs/wildkey-plan.md` Part H for the gate that must pass before Phase 1 work starts, and Part K for the full parity QA checklist.

## Development

```bash
npm install
npm run dev      # http://localhost:3000
npm run lint
npm run build
```

Signup/login writes to `.data/db.json` in the project root (gitignored). Delete that file to reset local accounts/observations.
