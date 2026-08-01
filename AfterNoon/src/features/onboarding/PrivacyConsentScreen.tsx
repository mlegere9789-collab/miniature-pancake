import type { NativeStackScreenProps } from '@react-navigation/native-stack';
import React, { useState } from 'react';
import { StyleSheet, Switch, Text, View } from 'react-native';

import { Body, Card, Heading, PrimaryButton, Screen, Title } from '@/components/ui';
import { useApp } from '@/context/AppContext';
import { colors, radius, spacing } from '@/theme/theme';
import type { OnboardingStackParamList } from '@/navigation/types';

type Props = NativeStackScreenProps<OnboardingStackParamList, 'PrivacyConsent'>;

export function PrivacyConsentScreen(_props: Props) {
  const { acceptConsent, completeOnboarding } = useApp();
  // Sync starts OFF. Turning it on is a deliberate switch flip — this local
  // state is the real value that gets persisted, not decoration.
  const [syncEnabled, setSyncEnabled] = useState(false);
  const [busy, setBusy] = useState(false);

  async function finish() {
    setBusy(true);
    try {
      // Persist the explicit choice, then mark onboarding complete. The root
      // navigator swaps to the main tabs once both are recorded.
      await acceptConsent(syncEnabled);
      await completeOnboarding();
    } finally {
      setBusy(false);
    }
  }

  return (
    <Screen contentStyle={styles.content}>
      <View style={styles.header}>
        <Text style={styles.emoji}>🛡️</Text>
        <Title>Your data, your call</Title>
        <Body muted>
          AfterNoon stores your entries on this device. You can optionally turn on encrypted
          cloud sync later — it is off unless you choose to enable it.
        </Body>
      </View>

      <Card>
        <View style={styles.toggleRow}>
          <View style={styles.toggleText}>
            <Heading>Cloud sync</Heading>
            <Body muted>
              {syncEnabled
                ? 'On — your entries can be backed up and synced across devices.'
                : 'Off — nothing leaves this device. Recommended.'}
            </Body>
          </View>
          <Switch
            value={syncEnabled}
            onValueChange={setSyncEnabled}
            trackColor={{ false: colors.surfaceMuted, true: colors.accentSoft }}
            thumbColor={syncEnabled ? colors.accent : colors.surface}
            accessibilityLabel="Enable cloud sync"
          />
        </View>
        <View style={styles.divider} />
        <Body muted>
          You can change this any time in Settings. Sync is a genuine on/off boundary: while it
          is off, the app makes no network calls with your health data.
        </Body>
      </Card>

      <View style={styles.actions}>
        <PrimaryButton
          label="I understand — continue"
          onPress={finish}
          loading={busy}
        />
        <Body muted style={styles.footnote}>
          Continuing records your consent to store data locally on this device.
        </Body>
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  content: { gap: spacing.lg, paddingVertical: spacing.xl, flexGrow: 1 },
  header: { gap: spacing.sm },
  emoji: { fontSize: 44 },
  toggleRow: { flexDirection: 'row', alignItems: 'center', gap: spacing.md },
  toggleText: { flex: 1, gap: spacing.xs },
  divider: { height: 1, backgroundColor: colors.border, marginVertical: spacing.sm },
  actions: { gap: spacing.md, marginTop: 'auto' },
  footnote: { textAlign: 'center' },
});
