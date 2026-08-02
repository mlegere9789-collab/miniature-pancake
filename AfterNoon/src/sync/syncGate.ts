/**
 * THE network boundary for health data.
 *
 * Architectural rule (from the product spec, non-negotiable): no health or
 * behavioral data leaves the device unless the user has explicitly opted in to
 * sync. This module is the ONE place that rule is enforced in code. Any future
 * network write that touches user data MUST go through `guardHealthDataEgress`
 * (or `runIfSyncEnabled`). A UI toggle alone is not sufficient — the toggle
 * flips a persisted flag, and this gate reads that flag before every egress.
 *
 * There is intentionally NO transport code here yet. Sync is a later, isolated,
 * opt-in module; when it lands it plugs in behind this gate rather than
 * scattering `if (syncEnabled)` checks through feature code.
 */

import { settingsRepository } from '@/data/repositories/settings';

export class SyncDisabledError extends Error {
  constructor(operation: string) {
    super(
      `Blocked network egress "${operation}": sync is disabled. Health data ` +
        `stays on-device until the user opts in via Settings.`,
    );
    this.name = 'SyncDisabledError';
  }
}

/**
 * Throws unless the user has opted in to sync. Call this at the top of ANY
 * function that would transmit user data off-device. Fail closed.
 */
export async function guardHealthDataEgress(operation: string): Promise<void> {
  const enabled = await settingsRepository.isSyncEnabled();
  if (!enabled) throw new SyncDisabledError(operation);
}

/**
 * Runs `fn` only if sync is enabled; otherwise returns `null` without invoking
 * it. Use for best-effort background pushes where a hard throw isn't wanted.
 */
export async function runIfSyncEnabled<T>(
  operation: string,
  fn: () => Promise<T>,
): Promise<T | null> {
  const enabled = await settingsRepository.isSyncEnabled();
  if (!enabled) {
    if (__DEV__) {
      // eslint-disable-next-line no-console
      console.log(`[syncGate] skipped "${operation}" — sync disabled`);
    }
    return null;
  }
  return fn();
}

/** Read-only helper for UI that needs to reflect the current posture. */
export async function isSyncEnabled(): Promise<boolean> {
  return settingsRepository.isSyncEnabled();
}
