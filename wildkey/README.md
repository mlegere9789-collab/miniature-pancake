# Wildkey

A nature-identification web app scaffold, built toward parity with iNaturalist + Seek before any "exceed" feature. Full product/design spec: [`docs/wildkey-plan.md`](docs/wildkey-plan.md).

"Wildkey" is a placeholder name — swap the brand before production.

## Stack

Next.js (App Router) + TypeScript + Tailwind CSS v4.

## What's scaffolded so far

This is a Phase 0 starting point, not a finished parity build. It establishes the app shell and design system so real features can be built inside it.

- **One app, two modes**: a `Quick ID` / `Naturalist` toggle in the top bar (`src/lib/mode-context.tsx`, `src/components/mode-toggle.tsx`), persisted to `localStorage`, driving mode-aware copy on the home page — the core structural decision in Part A.3 of the plan.
- **Design tokens** (`src/app/globals.css`): nature-toned neutral palette (soil/bark/moss/sky), a single accent color reserved for primary actions and confidence indicators, the 4/8/12/16/24/32/48/64px spacing scale, 8px/4px corner radii, and full dark-mode parity — per Part E.
- **Navigation shell**: a top bar (desktop nav + mode toggle) and a 5-tab bottom nav (Camera, Explore, Observations, Activity, Me) per Part B.2.
- **Quick ID capture flow** (`/camera`): photo upload/preview and a mock identify step producing a result card with a visible confidence % and an honest "not confident" fallback state instead of a guessed species dressed as fact (Part C.1). The mock model call is isolated in one place (`runMockIdentify` in `src/app/camera/page.tsx`) so it's a clean swap-in point for a real on-device model.
- **Species profile page** (`/species/[taxon]`): name, photo placeholder, description, seasonality, sighting count, and a danger/toxicity badge slot (Part C.1, Part D.1).
- **IA stubs** for `/explore`, `/identify`, `/observations`, `/activity`, `/me`, `/settings/lite-mode`, `/settings/data` — each lists what that screen is meant to carry from the Part C parity matrix, as a build checklist rather than a finished feature.

## What's deliberately not built yet

Everything that needs a backend, a real CV model, or native device APIs: accounts/auth, the community ID system, the sync engine and visible sync log, the real on-device identification model, map rendering, projects/journals/guides, curator tools, and all of Phase 1 (Part D). See `docs/wildkey-plan.md` Part H for the gate that must pass before Phase 1 work starts, and Part K for the full parity QA checklist.

## Development

```bash
npm install
npm run dev      # http://localhost:3000
npm run lint
npm run build
```
