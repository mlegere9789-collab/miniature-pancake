# Vitals — The Whole-Garden Health Score Dashboard

Phase 1 (MVP) scaffold: manual plant entry, check-in photo capture with a
ghost-overlay alignment guide, a stubbed rules-based scorer (swappable later
for the shared Plant ID & Diagnostic Engine), score history, and a single
Garden Score home screen.

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
│       │   └── diagnosticEngine.ts  Stubbed rules-based scorer (Idea 5 stand-in)
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
        │   └── AddPlantScreen.tsx        Manual plant entry (Phase 1 skips auto-segmentation)
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

## Running locally

### Backend
```bash
cd vitals/backend
npm install
cp .env.example .env   # set DATABASE_URL
npx prisma migrate dev --name init
npm run dev
```

### Mobile
```bash
cd vitals/mobile
npm install
npx expo start
```

## Not yet built (later phases)

Auto-segmentation onboarding, weather/environmental-fit integration, regional
outbreak modifiers, neighborhood leaderboard, seasonal recalibration curves,
weekly report card generation — see the top-level spec doc for phase 2-4 scope.
