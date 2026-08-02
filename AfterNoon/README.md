# AfterNoon

A privacy-first perimenopause / menopause symptom tracker. **Local-first**: all
health data lives in an on-device SQLite database and never leaves the phone
unless the user explicitly opts in to sync.

This is an MVP scaffold focused on the two features the product's
differentiation depends on: the **on-device data architecture** and the
**Doctor Report export**. Screens are wired to real local data end-to-end rather
than stubbed.

## Stack

- **Expo** (SDK 52) + **React Native** + **TypeScript**
- **expo-sqlite** — the on-device source of truth
- **expo-print** + **expo-sharing** — on-device PDF generation and native share
- **@react-navigation** — tab + stack navigation

## Getting started

```bash
cd AfterNoon
npm install
npm start           # then press i / a, or scan with Expo Go
npm run typecheck   # tsc --noEmit
npm test            # jest (pure-logic unit tests)
```

## Architecture

```
src/
  data/                 # local-first data layer (the only code that touches SQLite)
    db.ts               # connection + migrations (single SQLite entry point)
    schema.ts           # DDL + versioned migrations
    types.ts            # domain types (no SQLite leakage)
    repositories/       # symptomLogs, stagingAssessment, cycleNotes, settings
    reportData.ts       # ONE date-range query feeding both Trends and the Report
    index.ts            # `dataAccess` facade — the seam the rest of the app imports
  sync/                 # opt-in cloud sync (not implemented) + the hard boundary
    syncGate.ts         # single choke point: guardHealthDataEgress()
  context/AppContext.tsx# boot, consent/sync posture, data-change signal
  navigation/           # Root -> (Onboarding stack | Tabs) ; Reports sub-stack
  features/
    onboarding/         # Welcome -> TrustIntro -> Staging -> PrivacyConsent
    home/               # daily check-in card (writes real rows) + insight stub
    trends/             # 30/90-day heatmap from local data
    reports/            # date-range picker, preview, on-device PDF export
    community/          # preview surface (no real content, no network)
    settings/           # persisted sync toggle, consent info, data wipe
  constants/            # symptom categories, staging questionnaire
  theme/                # design tokens
```

### The privacy boundary (non-negotiable)

The rule — *no health data leaves the device unless the user opts in* — is
enforced in **one place**: [`src/sync/syncGate.ts`](src/sync/syncGate.ts).

- `settings.sync_enabled` (persisted in SQLite) is the single source of truth.
- The Settings toggle and the onboarding consent screen write that flag; they do
  not flip an in-memory value.
- It defaults to **OFF** and turning it on requires an explicit, confirmed user
  action.
- Any future network write must call `await guardHealthDataEgress(op)` first,
  which **fails closed**. See [`src/sync/README.md`](src/sync/README.md).

There are intentionally **no** third-party analytics or crash-reporting SDKs.
Adding one is a product decision to raise first, not a default.

### One query, two features

`buildReportData(startIso, endIso, generatedAt)` in `data/reportData.ts` is the
single aggregation that both the **Trends** screen and the **Doctor Report**
render from — so the on-screen preview and the exported PDF can never diverge.

### Doctor Report export

1. `reportHtml.ts` — pure `ReportData → HTML` (unit-tested, escapes user input).
2. `pdfExport.ts` — `Print.printToFileAsync` renders the PDF **on-device**, then
   `Sharing.shareAsync` hands it to the OS share sheet. No upload happens.

> Note: we use `expo-print` rather than `react-native-html-to-pdf` — it's the
> Expo-managed-workflow equivalent (same "HTML in, on-device PDF out" contract,
> no native linking, works in EAS builds).

## What's intentionally not done yet

- Cloud sync transport (the boundary and module home exist; the wire does not).
- Cycle-note capture UI (the table, repository and report rendering exist; a
  dedicated entry screen is a fast follow).
- The Home "insight" line is a labelled placeholder, per the build plan.
- Community is a preview only.
- Visual polish is calibrated to a warm placeholder theme; swap the tokens in
  `theme/theme.ts` to match the Claude Design prototype.
