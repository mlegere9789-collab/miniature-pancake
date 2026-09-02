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
│       │   ├── comparison.ts        Twin-plant percentile + leaderboard rank (Phase 3, spec §4.4/§4.6)
│       │   ├── dashboard.ts         Rising Stars ranking (spec §4.3), pulled out of routes/gardens.ts for testability
│       │   └── speciesDormancy.ts   Curated per-species dormancy calendar (Phase 3, spec §4.7)
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
        │   ├── ReportCardScreen.tsx      Weekly Garden Report Card + native share sheet
        │   ├── LeaderboardScreen.tsx     Opt-in toggle + neighborhood leaderboard rank
        │   └── PhotoTimelineScreen.tsx   Before/after check-in photo slider (spec §4.4)
        ├── components/       Sparkline, ScoreDeltaBadge, GhostOverlay, BeforeAfterSlider
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
8. **Photo timeline** (`mobile/src/components/BeforeAfterSlider.tsx`,
   `mobile/src/screens/PhotoTimelineScreen.tsx`) — a draggable side-by-side
   slider comparing any two check-in photos (spec §4.4), reachable via
   "Compare photos" on the plant detail screen once a plant has 2+ check-ins.

### Phase 2

9. **Weather-driven environmental fit** (`backend/src/services/weatherService.ts`) —
   fetches frost/drought signals from Open-Meteo (no API key needed) for the
   garden's lat/lon and applies a penalty to the diagnostic engine's
   environmental-fit sub-score (spec §3.1.3: predictive, not just reactive).
   A `Garden` needs `latitude`/`longitude` set (see the seed script below) for
   this to activate; it's a no-op otherwise.
10. **Frost-risk dashboard banner** (`mobile/src/screens/GardenDashboardScreen.tsx`) —
   `GET /gardens/:id` now returns `weatherAlert`, populated when frost is
   forecast tonight and at least one `frostSensitive` plant is in the garden
   (spec §4.3/§4.5). Mark a plant frost-sensitive from the Add Plant screen.
11. **Weekly Garden Report Card** (`backend/src/services/reportCard.ts`,
    `GET /gardens/:id/report-card`, `mobile/src/screens/ReportCardScreen.tsx`) —
    a 7-day recap (Garden Score delta, rising stars, plants needing attention,
    check-ins completed) rendered as a branded card. The share button captures
    that card with `react-native-view-shot` and shares it as a PNG via
    `expo-sharing`'s native share sheet (spec §4.5/§4.6), falling back to a
    plain-text share if image sharing isn't available on the platform.

### Phase 3 (in progress)

12. **Seasonal score recalibration** (`backend/src/services/scoring.ts`'s
    `isSeasonallyDormant` + the dormancy floor in `computePlantScore`) — a
    plant's `dormancyMonths` (1-12) suppress the visual-vitality dip from
    expected leaf drop/browning during its dormant season, so a deciduous
    tree isn't scored as declining every winter (spec §4.7). Toggle "Dormant
    in winter" on the Add Plant screen; the preset months come from
    `GET /gardens/:id/dormancy-defaults` (`defaultDormancyMonths` in
    scoring.ts), which picks Nov-Feb or May-Aug based on the garden's
    latitude sign, so southern-hemisphere gardens get the right season.
    Typing a species name that matches the curated table in
    `backend/src/services/speciesDormancy.ts` (pass `?speciesId=`) instead
    auto-detects its real habit — deciduous, evergreen, or annual — and
    pre-sets (and explains) the toggle; an unrecognized species still falls
    back to the hemisphere-only heuristic, and the user can always override
    the toggle by hand.
13. **Twin plants near you** (`backend/src/services/comparison.ts`,
    `GET /plants/:id/twin-comparison`) — an anonymized percentile comparison
    against other active plants of the same species in the same USDA zone,
    shown on the plant detail screen (spec §4.4). Only aggregate scores are
    ever exposed — never another user's garden, name, or photos.
14. **Neighborhood leaderboard** (`GET /gardens/:id/leaderboard`,
    `PATCH /gardens/:id/leaderboard-opt-in`, `mobile/src/screens/LeaderboardScreen.tsx`) —
    opt-in only rank among other opted-in gardens in the same USDA zone
    (spec §4.6). The switch on the leaderboard screen is the entire consent
    flow; no rank is fetched or shown until a garden opts in.

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

Auto-segmentation onboarding and regional outbreak modifiers — see the
top-level spec doc for the remaining phase 3-4 scope.
