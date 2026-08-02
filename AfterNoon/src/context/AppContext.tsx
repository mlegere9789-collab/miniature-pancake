/**
 * App-wide state: boot status, consent/sync posture, and a bump signal that
 * screens subscribe to so they re-query local data after a write.
 *
 * Deliberately small — no external state library. The database is the source of
 * truth; this context caches only the consent flags (which gate navigation) and
 * a monotonically increasing `dataVersion` used to invalidate screen queries.
 */

import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
} from 'react';

import { dataAccess } from '@/data';
import { CURRENT_CONSENT_VERSION } from '@/data/repositories/settings';
import type { ConsentState } from '@/data/repositories/settings';
import { nowIso } from '@/utils/date';

interface AppContextValue {
  ready: boolean;
  consent: ConsentState;
  /** Increments whenever local data changes so screens can refetch. */
  dataVersion: number;
  notifyDataChanged: () => void;
  refreshConsent: () => Promise<void>;
  completeOnboarding: () => Promise<void>;
  /** Explicit affirmative consent action from the privacy screen. */
  acceptConsent: (syncEnabled: boolean) => Promise<void>;
  setSyncEnabled: (enabled: boolean) => Promise<void>;
}

const DEFAULT_CONSENT: ConsentState = {
  onboardingComplete: false,
  syncEnabled: false,
  consentVersionAccepted: null,
  consentAcceptedAt: null,
};

const AppContext = createContext<AppContextValue | null>(null);

export function AppProvider({ children }: { children: React.ReactNode }) {
  const [ready, setReady] = useState(false);
  const [consent, setConsent] = useState<ConsentState>(DEFAULT_CONSENT);
  const [dataVersion, setDataVersion] = useState(0);

  const refreshConsent = useCallback(async () => {
    const next = await dataAccess.settings.getConsentState();
    setConsent(next);
  }, []);

  useEffect(() => {
    let cancelled = false;
    (async () => {
      await dataAccess.init();
      const next = await dataAccess.settings.getConsentState();
      if (!cancelled) {
        setConsent(next);
        setReady(true);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, []);

  const notifyDataChanged = useCallback(() => {
    setDataVersion((v) => v + 1);
  }, []);

  const completeOnboarding = useCallback(async () => {
    await dataAccess.settings.setOnboardingComplete(true);
    await refreshConsent();
  }, [refreshConsent]);

  const acceptConsent = useCallback(
    async (syncEnabled: boolean) => {
      // Persist the affirmative choice. syncEnabled is whatever the user
      // explicitly toggled — defaults to false on the consent screen.
      await dataAccess.settings.setSyncEnabled(syncEnabled);
      await dataAccess.settings.recordConsent(nowIso());
      await refreshConsent();
    },
    [refreshConsent],
  );

  const setSyncEnabled = useCallback(
    async (enabled: boolean) => {
      await dataAccess.settings.setSyncEnabled(enabled);
      await refreshConsent();
    },
    [refreshConsent],
  );

  const value = useMemo<AppContextValue>(
    () => ({
      ready,
      consent,
      dataVersion,
      notifyDataChanged,
      refreshConsent,
      completeOnboarding,
      acceptConsent,
      setSyncEnabled,
    }),
    [
      ready,
      consent,
      dataVersion,
      notifyDataChanged,
      refreshConsent,
      completeOnboarding,
      acceptConsent,
      setSyncEnabled,
    ],
  );

  return <AppContext.Provider value={value}>{children}</AppContext.Provider>;
}

export function useApp(): AppContextValue {
  const ctx = useContext(AppContext);
  if (!ctx) throw new Error('useApp must be used within <AppProvider>');
  return ctx;
}

/** Whether onboarding + a current-version consent are both satisfied. */
export function needsOnboarding(consent: ConsentState): boolean {
  return (
    !consent.onboardingComplete ||
    consent.consentVersionAccepted === null ||
    consent.consentVersionAccepted < CURRENT_CONSENT_VERSION
  );
}
