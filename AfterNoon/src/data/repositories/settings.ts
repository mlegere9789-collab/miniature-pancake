/**
 * Key-value settings + consent state, persisted in `app_settings`.
 *
 * This repository is the source of truth for the privacy posture of the app:
 * `sync_enabled` here is what the sync gate reads. The UI toggle in Settings
 * writes through this — it never flips an in-memory flag on its own.
 */

import { getDb } from '../db';

export type SettingKey =
  | 'onboarding_complete'
  | 'sync_enabled'
  | 'consent_version_accepted'
  | 'consent_accepted_at';

/** The consent copy version the user last affirmatively accepted. Bump when
 * the privacy terms materially change so we can re-prompt. */
export const CURRENT_CONSENT_VERSION = 1;

interface Row {
  key: string;
  value: string;
}

async function getRaw(key: SettingKey): Promise<string | null> {
  const db = await getDb();
  const row = await db.getFirstAsync<Row>('SELECT value FROM app_settings WHERE key = ?', [key]);
  return row?.value ?? null;
}

async function setRaw(key: SettingKey, value: string): Promise<void> {
  const db = await getDb();
  await db.runAsync(
    `INSERT INTO app_settings (key, value) VALUES (?, ?)
     ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
    [key, value],
  );
}

async function getBool(key: SettingKey, fallback = false): Promise<boolean> {
  const raw = await getRaw(key);
  if (raw === null) return fallback;
  return raw === '1' || raw === 'true';
}

async function setBool(key: SettingKey, value: boolean): Promise<void> {
  await setRaw(key, value ? '1' : '0');
}

export interface ConsentState {
  /** Whether the user has been through the privacy consent screen. */
  onboardingComplete: boolean;
  /** THE hard boundary flag. Defaults OFF and stays off until an explicit
   * affirmative action turns it on. */
  syncEnabled: boolean;
  /** The consent version the user accepted, or null if never. */
  consentVersionAccepted: number | null;
  consentAcceptedAt: string | null;
}

export const settingsRepository = {
  async getConsentState(): Promise<ConsentState> {
    const [onboardingComplete, syncEnabled, versionRaw, acceptedAt] = await Promise.all([
      getBool('onboarding_complete'),
      // Sync defaults to OFF. This default is the safe-by-construction posture.
      getBool('sync_enabled', false),
      getRaw('consent_version_accepted'),
      getRaw('consent_accepted_at'),
    ]);
    return {
      onboardingComplete,
      syncEnabled,
      consentVersionAccepted: versionRaw === null ? null : Number(versionRaw),
      consentAcceptedAt: acceptedAt,
    };
  },

  async isSyncEnabled(): Promise<boolean> {
    return getBool('sync_enabled', false);
  },

  /** Turn sync on/off. Turning ON requires an explicit call from an affirmative
   * user action; there is no code path that flips this implicitly. */
  async setSyncEnabled(enabled: boolean): Promise<void> {
    await setBool('sync_enabled', enabled);
  },

  async setOnboardingComplete(complete: boolean): Promise<void> {
    await setBool('onboarding_complete', complete);
  },

  /** Record that the user affirmatively accepted the current consent version. */
  async recordConsent(acceptedAt: string): Promise<void> {
    await setRaw('consent_version_accepted', String(CURRENT_CONSENT_VERSION));
    await setRaw('consent_accepted_at', acceptedAt);
  },
};
