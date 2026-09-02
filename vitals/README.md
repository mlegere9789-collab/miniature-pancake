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
│       │   ├── speciesDormancy.ts   Curated per-species dormancy calendar (Phase 3, spec §4.7)
│       │   ├── forecast.ts          Linear-regression score trend forecast (Phase 3/4, spec §4.3/§4.5)
│       │   ├── outbreakDetection.ts Regional outbreak alerts from shared diagnostic flags (Phase 3, spec §4.3/§4.5)
│       │   └── treatmentPlans.ts    Actionable steps/products per diagnostic flag (spec §4.2)
│       ├── routes/          Express routers: plants, checkins, gardens, species, treatment-plans
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
        │   ├── YardMapScreen.tsx         Plants pinned onto a yard photo (spec §4.1)
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
   plant id, calls the diagnostic engine stub, computes the score, persists it,
   and now also generates a `TreatmentPlan` (curated steps + recommended
   products, `backend/src/services/treatmentPlans.ts`) for each diagnostic
   flag (spec §4.2: a diagnosis needs a "what do I do about it"). Shown on
   the plant detail screen under each open flag with a "Mark treated"
   button; an incomplete plan feeds `outstandingTreatmentsIgnored` in care-
   consistency scoring — previously always false, since nothing ever
   created a `TreatmentPlan` despite the model existing since Phase 1.
4. **Ghost-overlay camera flow** (`mobile/src/screens/CheckInCameraScreen.tsx`) —
   the core differentiator: overlays the previous check-in photo at reduced
   opacity so the user aligns angle/framing before capture.
5. **Manual plant entry and editing** (`mobile/src/screens/AddPlantScreen.tsx`,
   `PATCH /plants/:id`) — species, nickname, check-in cadence, and
   importance weight; posts to `POST /plants`. "Edit plant" on the plant
   detail screen reuses the same form to update everything but species
   (locked once created — changing it would silently invalidate score
   history comparisons and twin-plant matching) and `PATCH /plants/:id/archive`
   removes a plant from active views (check-in history is kept).
6. **Score history + check-in log** (`mobile/src/screens/PlantDetailScreen.tsx`) —
   sparkline of `PlantScoreSnapshot` history plus open diagnostic flags per
   check-in. The dashboard hero card shows the same sparkline for the
   `GardenScoreSnapshot` history (the 90-day history was already being
   fetched but never rendered there).
6b. **Offline check-in sync** (`mobile/src/services/checkInQueue.ts`, wired up
    in `App.tsx`) — a check-in captured while offline is queued to
    `AsyncStorage` and flushed on app launch and whenever the app returns to
    the foreground (the closest reliable proxy for "connectivity is back"
    without a network-status dependency). The dashboard shows a "N check-ins
    waiting to sync" note while any are still queued; the queue itself
    already existed but nothing was ever calling `flushCheckInQueue`.
7. **Push notification reminders** (`mobile/src/services/notifications.ts`) —
   local, per-plant reminders on each plant's check-in cadence (Expo Notifications).
   Phase 2 can swap these for server-driven push once predictive/weather-aware
   alerts (spec §4.5) need a backend trigger.
8. **Photo timeline** (`mobile/src/components/BeforeAfterSlider.tsx`,
   `mobile/src/screens/PhotoTimelineScreen.tsx`) — a draggable side-by-side
   slider comparing any two check-in photos (spec §4.4), reachable via
   "Compare photos" on the plant detail screen once a plant has 2+ check-ins.

8b. **Yard map** (`backend/prisma/schema.prisma`'s `Garden.yardMapPhotoUrl`
    and `Plant.locationPin`, `PATCH /gardens/:id/yard-map`,
    `PATCH /plants/:id/location`, `mobile/src/screens/YardMapScreen.tsx`,
    reachable via 🗺️ on the dashboard header) — pin each plant onto a wide
    photo of the yard instead of only seeing it in a flat list. Upload a
    yard photo once, then tap a not-yet-placed plant and tap the map to
    drop its pin; pins are colored by the plant's current score and tap
    through to its detail screen. Coordinates are stored relative (0-1) so
    a pin stays correct at any display size (spec §4.1).

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
   The frost and regional-outbreak banners also fire a local push
   (`notifyWeatherAlertIfNew`/`notifyOutbreakAlertsIfNew` in
   `mobile/src/services/notifications.ts`, deduped once per day per alert)
   the first time the dashboard sees them, so the warning isn't silent
   until the user happens to open the app.
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
    the toggle by hand. `GET /species/search?q=` (`searchSpecies` in the
    same file) backs a species autocomplete on the Add Plant screen so
    typing "jap" surfaces "Japanese Maple" as a tappable suggestion instead
    of relying on the user to type a slug-matching name blind.
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
15. **Score trend forecast** (`backend/src/services/forecast.ts`,
    surfaced on `mobile/src/screens/PlantDetailScreen.tsx`) — a simple
    linear-regression projection of a plant's score history, used to flag
    plants that are trending down and will cross the attention threshold in
    the next couple weeks even though today's score still looks fine (spec
    §4.3/§4.5: "predictive, not just reactive"). A full ML forecasting model
    (Phase 4) is further work; this is a lightweight stand-in that's already
    useful for early warning.
16. **Regional outbreak alerts** (`backend/src/services/outbreakDetection.ts`,
    surfaced on `mobile/src/screens/GardenDashboardScreen.tsx`) — when the
    same diagnostic condition (e.g. powdery mildew) is independently
    reported by several different gardens in the same USDA zone within the
    last 14 days, every garden in that zone sees a "reported in N nearby
    gardens this week" banner (spec §4.3/§4.5, "Idea 4"). A dedicated
    regional pest/disease feed (the "Blight Watch" companion app from the
    spec) is out of scope here; this uses Vitals' own diagnostic flags as
    the signal instead, and only ever exposes a condition name and a garden
    count — never which gardens reported it.

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

## Bug fixes

- **Garden Score neglect penalty measured from the wrong date.**
  `recomputeGardenScore` (`backend/src/routes/checkins.ts`) was flagging a
  plant "overdue" based on `plant.createdAt` instead of its most recent
  check-in, so any plant older than one cadence period stayed permanently
  overdue — and kept dinging the Garden Score's neglect penalty — even
  seconds after a fresh check-in. Fixed by measuring the cadence window from
  the plant's last check-in (`isOverdueForCheckin` in
  `backend/src/services/scoring.ts`, now unit tested).
- **Offline check-in queue never flushed.** See "Offline check-in sync"
  above — `flushCheckInQueue` existed but nothing called it.
- **`TreatmentPlan` model existed but nothing ever created one.** Care-
  consistency scoring's `outstandingTreatmentsIgnored` input queried for
  incomplete treatment plans, but no code path ever created a
  `TreatmentPlan` row — so that scoring input was permanently dead. Fixed
  by generating one per diagnostic flag on check-in (see "Check-in API" above).
- **A thrown error in any route left the request hanging forever.**
  Express 4 doesn't forward a rejected promise from an async route handler
  to error middleware on its own — nothing called `next(err)`, so a bad ID,
  a Prisma error, anything thrown, just left the client waiting with no
  response ever sent (not even a 500). Every route across the API had this
  exposure. Fixed by requiring `express-async-errors` (patches Express's
  router to catch async rejections automatically) plus a JSON 404 handler
  for unmatched routes and a catch-all JSON error handler
  (`backend/src/server.ts`) — verified by hitting an archive endpoint with
  a bad ID against an unreachable DB: the request now returns in ~60ms
  with a 500 instead of hanging.
- **`computeGardenScore` could produce `NaN`.** It divided by the sum of
  every plant's `importanceWeight`, which the spec §4.1 range (0-10)
  explicitly allows to be 0 — a garden where every plant is weighted 0
  divided by zero and would have persisted `NaN` as the Garden Score. Fixed
  by falling back to a plain average when total weight is 0 (now unit
  tested in `scoring.test.ts`).
- **Plant detail screen never refreshed after check-in or edit.** It only
  loaded data on first mount; navigating to Check-In or Edit and back
  returned to the same still-mounted screen instance, so a fresh check-in's
  score or an edited nickname/cadence didn't show until leaving and
  re-entering the screen. Fixed with `useFocusEffect` so it reloads every
  time it regains focus.
- **No way to remove a plant.** `Plant.active` existed and every score
  roll-up/dashboard/yard-map query already filtered on it, but nothing
  ever set it to `false` — there was no way to stop tracking a plant that
  died or was removed. Added `PATCH /plants/:id/archive` and an "Archive
  plant" action on the plant detail screen (with a confirmation, since it's
  not easily undoable from the UI); its check-in history and score
  snapshots are kept, it just drops out of the active views.
- **Check-in reminders silently reset on every refresh.** `App.tsx`
  rescheduled every plant's local reminder from "now" (`scheduleAllReminders`)
  on every `refreshKey` bump — any check-in, any plant add, even the offline
  queue flush — which pushed back every OTHER plant's reminder countdown too.
  Frequent app use could make reminders effectively never fire. Fixed by
  running `scheduleAllReminders` once per app launch and only resetting the
  specific plant's own reminder when it's actually checked in.

## Not yet built (later phases)

Auto-segmentation onboarding, a B2B white-label dashboard, and a full ML
forecasting model (today's forecast is a simple linear-regression
stand-in) — see the top-level spec doc for the remaining phase 4 scope.
