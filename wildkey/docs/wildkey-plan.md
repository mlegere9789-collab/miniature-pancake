# [Working Title: "Wildkey"] — Full Product & Design Plan

A nature-identification platform built to match iNaturalist + Seek completely, then exceed them.

Professional design plan — Version 1.0

> Naming note: "Wildkey" is a placeholder used throughout this document so specs read naturally. Swap in the real name/brand before production.

This document is the source spec for the `wildkey/` app in this repository. See `wildkey/README.md` for what has actually been built so far (Phase 0 scaffold) versus what remains.

## PART A — STRATEGY & POSITIONING

### A.1 Governing principle

Every decision in this plan is graded against one rule:

> Stability, function, user-friendliness, full parity, and polish come before any new feature. Nothing in the "exceed" column ships before the "match" column is 100% solid.

This document is split into two hard phases:

- **PHASE 0 — PARITY**: everything a current iNaturalist/Seek user would need to feel "this is at least as good, nothing is missing."
- **PHASE 1 — SUPREMACY**: the ranked list of things iNaturalist's own users have been begging for, built once Phase 0 is bulletproof.

No Phase 1 item may begin development until Phase 0 has shipped, been stress-tested, and hit the stability/parity bar defined in Part H.

### A.2 Core positioning statement

> One app. Works everywhere. Never loses your data. Never lies to you about what it doesn't know.

This targets the four most consistently-repeated failure modes: app fragmentation, poor offline/low-end performance, no backup/data portability, and (for Seek specifically) overconfident or unsafe identifications.

### A.3 The single biggest structural decision: ONE app, not four

iNaturalist's most self-inflicted wound is that "iNaturalist" today means: Seek, iNaturalist Classic, iNaturalist Next, and inaturalist.org — four codebases with different feature sets.

Wildkey ships as exactly one application (responsive web + one native app per platform, one codebase family, shared design system) with a mode toggle, not a separate app:

- **Quick ID Mode** (replaces Seek): camera-first, no login required, gamified, private, kid-safe.
- **Naturalist Mode** (replaces iNaturalist proper): full observation logging, community ID, projects, journals, research-grade data pipeline.

A user can switch modes with one tap, and anything logged in Quick ID Mode can be uploaded to Naturalist Mode later, reliably, with one tap, at any time, forever.

## PART B — INFORMATION ARCHITECTURE

### B.1 Top-level site map (web)

```
/                          Home (logged out: pitch + live map; logged in: personalized feed)
/explore                   Map/grid/list search — the Explore equivalent
/identify                  High-throughput ID queue for community identifiers
/species/[taxon]           Species profile pages
/places/[place]            Place pages (species lists, stats, map)
/projects                  Browse + create Collection/Traditional projects
/projects/[slug]
/journal                   Journal posts (own + followed)
/guides                    Curated field guides
/people/[username]         Profiles, stats, badges
/messages                  Direct messaging (opt-in, off by default)
/notifications
/stats                     Global site stats, leaderboards
/help                      Full help center
/help/getting-started
/curator                   Curator/moderator tools (permissioned)
/settings
/settings/data             Data export, account portability, deletion/anonymization
/settings/accessibility
/settings/lite-mode
/about, /donate, /jobs, /press, /api-docs
```

### B.2 Mobile app navigation (5-tab bottom nav)

1. Camera (default landing tab — Quick ID or full observation capture depending on mode)
2. Explore (map/search)
3. My Observations (list/grid, sortable, searchable)
4. Activity (notifications, community replies, ID requests)
5. Me (profile, badges, settings, mode toggle)

A persistent, single mode toggle (Quick ID ⟷ Naturalist) lives in the top bar on every screen, not buried in settings.

## PART C — PHASE 0: FEATURE PARITY MATRIX

### C.1 Quick ID Mode (Seek parity)

- Live camera ID: real-time on-device inference, auto-capture on high confidence, manual capture always available
- Photo-library ID: identify from any existing photo, same accuracy as live camera
- Fully offline: on-device model, zero network dependency for identification
- No forced login: full identification functionality with zero account
- Location privacy: coarse/obscured location only unless user opts into full account + sharing
- Species profile card: name, photo, short description, seasonality, global sighting count
- Personal collection/gallery: local-first, auto-synced to cloud once account exists
- Badges & challenges: non-predatory, no dark patterns, no push-notification spam
- No ads, no purchases, no paywall — ever, on any feature
- Honest uncertainty: genus/family-level fallback with a plain-language "we're not confident" state, never a guessed species dressed as fact
- Multi-language: minimum parity with Seek's 20+ languages at launch

### C.2 Naturalist Mode (iNaturalist parity)

- Full observation record: photo(s) + audio, date, location, notes
- Community ID system: comment/agree/disagree threading, taxonomic drill-down
- Confidence-scored CV suggestions: visible % shown at all times
- Quality-grade pipeline: equivalent to "Research Grade" (2+ agreeing IDs at genus-or-finer)
- Data Quality flags: wild/captive, evidence type, location accuracy, single-subject
- Annotations: life stage, sex, plant phenology — available in the mobile app at launch, not desktop-only
- Explore search: map/grid/list, full filter set, URL-shareable searches
- Identify queue: dedicated high-throughput screen for community identifiers, in the mobile app
- Projects: Collection (saved search) + Traditional (opt-in) types
- Journals: full parity, available on mobile
- Guides, Places, Site Stats: full parity
- Sensitive-species obscuring: automatic on a maintained sensitive list, manual override available
- Open data licensing choice: per-observation CC license selector
- Public API + open documentation from day one
- Curator/moderation tools: taxonomy correction, flagging, with a documented dispute-resolution path

### C.3 Cross-cutting parity requirements

- iPad/tablet layout: fully responsive, not a stretched phone UI
- Multi-photo upload in one step
- Reorderable/cover-photo selection: drag-to-reorder, discoverable
- Satellite-view default option, sticky across all maps in-app
- Location naming: prefer named park/trail/POI over nearest road
- Fast bulk logging: minimize taps-per-observation

## PART D — PHASE 1: SUPREMACY FEATURES (only after Phase 0 is stable — see Part H gate)

### D.1 Tier 1 — fix the most damaging, longest-running failures

1. Bulletproof sync, always (Queued → Uploading → Confirmed/Failed, local upload log, background sync survives app-close)
2. Full data portability from day one (one-tap export, account migration, account anonymization)
3. "Lite Mode" — shipped, not just promised (no background sync, no live map tiles, CV off by default, works fully offline except explicit "sync now")
4. Danger/safety flags on identifications (toxicity/hazard dataset, non-alarmist badges)
5. Android-grade parity from day one (same crash-rate/perf bar as iOS before either ships)

### D.2 Tier 2 — high-value, clearly-articulated but architecturally harder

- In-app correction/feedback loop for Quick ID misidentifications
- Duplicate-photo detection on upload
- Spectrogram view for audio observations, in-app
- Searchable/filterable identification history
- Clear, human-readable UX for ID-consensus changes
- Documented, transparent taxonomic dispute-resolution process

### D.3 Tier 3 — genuinely new ground (out of scope until D.1/D.2 ship and stabilize)

- Deeper care/cultivation info for garden and ornamental species
- Smarter, opt-in hybrid-taxon CV support
- Richer per-taxon "similar species" comparison tooling

## PART E — VISUAL DESIGN SYSTEM

- **Tone**: credible-scientific but warm — closer to a well-funded field guide than a consumer social app. One brand, two skins.
- **Typography**: a humanist sans for UI + a subtly serif display face for species names and long-form journal content.
- **Color system**: neutral nature-toned base (soil, bark, moss, sky) with a single confident accent color reserved only for primary actions and confidence indicators.
- **Iconography**: custom taxon-group icon set used consistently across map pins, badges, and filters.
- **Photography-first UI**: the organism's photo is always the largest element on any card.

Design tokens (starter spec):

- Spacing scale: 4/8/12/16/24/32/48/64px
- Corner radius: 8px (cards), 4px (inputs/buttons)
- Elevation: flat + 1 subtle shadow level only
- Dark mode: full parity from launch
- Map default: satellite, sticky per user preference across every map surface

Density rule: every list/grid view defaults to information-dense with an optional "visual/tile" view as a toggle — never forced tile-only.

## PART F — TRUST, SAFETY & COMMUNITY GOVERNANCE

- No forced social features; messaging/following/public profiles opt-in, off by default under 18 or in Quick ID Mode
- Clear escalation path for ID disputes (public wiki page, formal "expert flag")
- Suspension/moderation transparency (clear, appealable written reason)
- Data ethics for sensitive species (published obscuring methodology, independent accuracy review)
- Child safety: Quick ID Mode collects no personal data by default; minors' accounts require 13+/parental-consent gate

## PART G — TECHNICAL ARCHITECTURE

- On-device CV model bundled with the app (no network call for baseline ID)
- Local-first sync engine with conflict resolution and a visible, inspectable sync log
- Single shared core between web and native apps (shared API, design tokens, taxonomy/species data layer)
- Progressive enhancement: full features on modern hardware, core function preserved on old hardware/slow connections
- Performance budget: cold start under 2s on a 6-year-old low/mid-tier device, crash-free session rate above 99.5% on iOS and Android before Phase 0 is "done"

## PART H — QUALITY GATE: WHEN IS "PARITY" ACTUALLY DONE?

No Phase 1 feature work begins until:

1. Every row in Part C is implemented and tested on both iOS and Android
2. Crash-free session rate ≥ 99.5% sustained for 30 days on both platforms
3. Sync failure rate ≤ 0.1%, and 100% of failures surface a visible, actionable error state
4. App is usable (capture + queue observations) on a 6-year-old low-end Android device on throttled 2G
5. A structured usability panel rates the app "as good as or better than" iNaturalist/Seek on every category in the Part C matrix
6. Full accessibility audit passed (screen reader support, contrast, tap-target sizing) across both modes

## PART I — SUCCESS METRICS

**Phase 0 (parity):** crash-free rate, sync success rate, cold-start time, task-completion time for "log one observation" vs. iNat Classic, NPS from a panel of existing iNat/Seek users.

**Phase 1 (supremacy):** % of Quick ID users who transfer historical data to a full account, retention curve for low-end/poor-connectivity users, volume and resolution time of in-app misidentification reports.

## PART J — RISK REGISTER

| Risk | Mitigation |
|---|---|
| CV model accuracy starts behind iNat's | Partner/license open biodiversity datasets (GBIF, open-source models); be transparent about confidence |
| Small initial community → slower human-verification loop | Dedicated identifier-recruitment push + fast Identify-queue UX |
| "One app, two modes" adds complexity | Rigorous UX testing on the mode-toggle specifically |
| Offline-first + full sync engine is hard | Treat as highest-risk workstream; staff first, gate other Phase 0 work behind an early sync-engine spike |
| Repeating iNat's iOS-first mistake | Require simultaneous iOS + Android release for every version |

## PART K — APPENDIX: FULL PARITY CHECKLIST (flat list, for QA sign-off)

- [ ] Live camera ID, offline, on-device
- [ ] Photo-library ID (same accuracy as camera)
- [ ] No forced login for basic ID
- [ ] Obscured location by default
- [ ] Species profile card (name, photo, description, seasonality, sighting count)
- [ ] Local + cloud-synced personal collection
- [ ] Badges/challenges, no dark patterns
- [ ] Zero ads, zero purchases, zero paywalls
- [ ] Honest confidence/uncertainty display
- [ ] 20+ language support
- [ ] Full observation logging (photo + audio + notes + location)
- [ ] Community ID/comment/agree system
- [ ] Visible CV confidence %
- [ ] Quality-grade pipeline (Research-Grade equivalent)
- [ ] Data Quality flags
- [ ] Annotations (life stage, sex, phenology) — mobile + web
- [ ] Explore (map/grid/list + full filters)
- [ ] Identify queue — mobile + web
- [ ] Projects (Collection + Traditional)
- [ ] Journals — mobile + web
- [ ] Guides, Places, Site Stats
- [ ] Sensitive-species obscuring (auto + manual)
- [ ] Per-observation CC licensing
- [ ] Public API + docs
- [ ] Curator/moderation tools with documented dispute process
- [ ] iPad/tablet responsive layout
- [ ] Multi-photo upload in one step
- [ ] Reorderable/cover-photo selection (discoverable, not hidden)
- [ ] Sticky satellite-map default across all map surfaces
- [ ] Named-place location labeling (not just nearest road)
- [ ] Bulk-logging speed at or above iNat Classic benchmark
- [ ] Visible sync state + retry + local log (zero silent failures)
- [ ] One-tap full data export
- [ ] Account anonymization option
- [ ] Lite Mode (fully offline-capable, low-resource)
- [ ] Danger/toxicity flags on species cards
- [ ] Android crash-free rate parity with iOS at launch
