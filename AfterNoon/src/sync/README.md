# Sync module (opt-in, not yet implemented)

This directory is the designated home for cloud sync. Sync is **off by default**
and is a later, isolated feature. Nothing in the app's core paths depends on it.

## The rule

> No health or behavioral data leaves the device unless the user has explicitly
> opted in to sync in Settings.

This is enforced in **one** place: [`syncGate.ts`](./syncGate.ts).

- `guardHealthDataEgress(op)` — throws `SyncDisabledError` unless
  `settings.sync_enabled` is `true`. Fail-closed.
- `runIfSyncEnabled(op, fn)` — no-ops when disabled.

Both read the persisted `sync_enabled` flag from the settings repository. The
Settings screen toggle writes that same flag through
`dataAccess.settings.setSyncEnabled`, and the onboarding consent screen defaults
it to `false`. There is no in-memory-only path that can turn it on.

## How to add sync later without weakening the boundary

1. Add transport code (HTTP client, encryption, conflict resolution) **inside
   this directory only**.
2. The very first line of any function that transmits user data must be
   `await guardHealthDataEgress('<operation-name>')`.
3. Do not add `if (syncEnabled)` checks in feature/screen code. Feature code
   calls the data-access layer; only this module talks to the network.
4. Keep a single flag (`settings.sync_enabled`) as the source of truth. Do not
   introduce a second toggle.

## What must never be added by default

- Third-party analytics or crash-reporting SDKs that transmit health data or
  behavior tied to health entries. Any analytics proposal is a product decision
  to raise with the team first — it does not belong in this module.
