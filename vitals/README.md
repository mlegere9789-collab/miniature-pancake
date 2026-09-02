# Vitals — The Whole-Garden Health Score Dashboard

Phase 1 (MVP) scaffold: manual plant entry, check-in photo capture with a
ghost-overlay alignment guide, a stubbed rules-based scorer (swappable later
for the shared Plant ID & Diagnostic Engine), score history, and a single
Garden Score home screen. Phase 2 is done: weather-driven environmental-fit
scoring, a frost-risk dashboard banner, and a weekly Garden Report Card with
sharing. Phase 3 has started with seasonal score recalibration.

This app lives alongside the unrelated Whisper codebase at the repo root —
it is a separate product and does not depend on anything in `modules/` or
`orchestrator/`.

## Repo structure

```
vitals/
├── backend/                 Node + TypeScript + Postgres (Prisma) API
│   ├── prisma/
│   │   └── schema.prisma    Data model: User, Garden, Plant, CheckIn,
│   │                        DiagnosticFlag, TreatmentPlan
│   └── src/
│       ├── db/              Prisma client singleton
│       ├── models/          Zod input/output schemas per resource
│       ├── services/
│       │   ├── scoring.ts   Weighted composite scoring engine (spec §3.1/§3.2)
│       │   ├── diagnosticEngine.ts  Stubbed rules-based scorer (Idea 5 stand-in)
│       │   ├── weatherService.ts    Open-Meteo frost/drought signals (Phase 2, spec §5)
│       │   ├── reportCard.ts        Weekly Garden Report Card aggregation (Phase 2, spec §4.5)
│       │   └── comparison.ts        Twin-plant percentile + leaderboard rank (Phase 3, spec §4.4/§4.6)
│       ├── routes/          Express routers: plants, checkins, gardens
│       ├── types/           Shared TS types
│       └── server.ts        App entrypoint
│
└── mobile/                  React Native (Expo) client
    └── src/
        ├── screens/
        │   ├── CheckInCameraScreen.tsx   Ghost-overlay capture flow (core differentiator)
        │   ├── GardenDashboardScreen.tsx Hero Garden Score + Needs Attention / Rising Stars
        │   ├── PlantDetailScreen.tsx     Score history sparkline + check-in log
        │   ├── AddPlantScreen.tsx        Manual plant entry (Phase 1 skips auto-segmentation)
        │   └── ReportCardScreen.tsx      Weekly Garden Report Card + native share sheet
        ├── components/       Sparkline, ScoreDeltaBadge, GhostOverlay
        ├── services/         api.ts (typed backend client), checkInQueue.ts (offline queue),
        │                     notifications.ts (per-plant local check-in reminders)
        ├── theme/            Vitals design tokens (forest green / soil brown / gold)
        └── types/            Shared domain types mirrored from backend
```

## Build order (Phase 1)

1. **Data model** (`backend/prisma/schema.prisma`) — Plant, CheckIn, DiagnosticFlag,
   scoring fields, matching spec §5.1.
2. **Scoring service** (`backend/src/services/scoring.ts`) — implements the
   weighted composite from spec §3.1 (visual 40%, diagnostic 30%, environmental
   fit 15%, care consistency 10%, trend momentum modifier ±10) and the Garden
   Score roll-up from §3.2.
3. **Check-in API** (`backend/src/routes/checkins.ts`) — accepts a photo +
   plant id, calls the diagnostic engine stub, computes the score, persists it.
4. **Ghost-overlay camera flow** (`mobile/src/screens/CheckInCameraScreen.tsx`) —
   the core differentiator: overlays the previous check-in photo at reduced
   opacity so the user aligns angle/framing before capture.
5. **Manual plant entry** (`mobile/src/screens/AddPlantScreen.tsx`) — species,
   nickname, check-in cadence, and importance weight; posts to `POST /plants`.
6. **Score history + check-in log** (`mobile/src/screens/PlantDetailScreen.tsx`) —
   sparkline of `PlantScoreSnapshot` history plus open diagnostic flags per check-in.
7. **Push notification reminders** (`mobile/src/services/notifications.ts`) —
   local, per-plant reminders on each plant's check-in cadence (Expo Notifications).
   Phase 2 can swap these for server-driven push once predictive/weather-aware
   alerts (spec §4.5) need a backend trigger.

### Phase 2

8. **Weather-driven environmental fit** (`backend/src/services/weatherService.ts`) —
   fetches frost/drought signals from Open-Meteo (no API key needed) for the
   garden's lat/lon and applies a penalty to the diagnostic engine's
   environmental-fit sub-score (spec §3.1.3: predictive, not just reactive).
   A `Garden` needs `latitude`/`longitude` set (see the seed script below) for
   this to activate; it's a no-op otherwise.
9. **Frost-risk dashboard banner** (`mobile/src/screens/GardenDashboardScreen.tsx`) —
   `GET /gardens/:id` now returns `weatherAlert`, populated when frost is
   forecast tonight and at least one `frostSensitive` plant is in the garden
   (spec §4.3/§4.5). Mark a plant frost-sensitive from the Add Plant screen.
10. **Weekly Garden Report Card** (`backend/src/services/reportCard.ts`,
    `GET /gardens/:id/report-card`, `mobile/src/screens/ReportCardScreen.tsx`) —
    a 7-day recap (Garden Score delta, rising stars, plants needing attention,
    check-ins completed) with a share button using the native share sheet
    (spec §4.5/§4.6). A polished branded image card (vs. today's plain text
    share) is further design work once the visual system has real assets.

### Phase 3 (in progress)

11. **Seasonal score recalibration** (`backend/src/services/scoring.ts`'s
    `isSeasonallyDormant` + the dormancy floor in `computePlantScore`) — a
    plant's `dormancyMonths` (1-12) suppress the visual-vitality dip from
    expected leaf drop/browning during its dormant season, so a deciduous
    tree isn't scored as declining every winter (spec §4.7). Toggle "Dormant
    in winter" on the Add Plant screen (currently a fixed Nov-Feb preset; a
    real per-species/hemisphere dormancy calendar is further Phase 3 work).
12. **Twin plants near you** (`backend/src/services/comparison.ts`,
    `GET /plants/:id/twin-comparison`) — an anonymized percentile comparison
    against other active plants of the same species in the same USDA zone,
    shown on the plant detail screen (spec §4.4). Only aggregate scores are
    ever exposed — never another user's garden, name, or photos.
13. **Neighborhood leaderboard** (`GET /gardens/:id/leaderboard`,
    `PATCH /gardens/:id/leaderboard-opt-in`) — opt-in only rank among other
    opted-in gardens in the same USDA zone (spec §4.6). No mobile UI yet;
    the API is ready for a settings toggle + leaderboard screen.

## Running locally

### Backend
```bash
cd vitals/backend
npm install
cp .env.example .env   # set DATABASE_URL
npx prisma migrate dev --name init
npx prisma db seed      # creates the demo garden the mobile app points at
npm run dev
```

### Mobile
```bash
cd vitals/mobile
npm install
npx expo start
```

## Not yet built (later phases)

Auto-segmentation onboarding, a branded/image report card share asset
(text-only sharing ships today), regional outbreak modifiers, a per-species/
hemisphere dormancy calendar (today's dormancy toggle is a fixed preset),
a leaderboard opt-in toggle + screen on mobile (API only for now) — see the
top-level spec doc for the remaining phase 3-4 scope.
