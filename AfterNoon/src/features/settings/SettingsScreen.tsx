import React, { useState } from 'react';
import { Alert, StyleSheet, Switch, Text, View } from 'react-native';

import { Body, Card, Heading, PrimaryButton, Screen, Title } from '@/components/ui';
import { useApp } from '@/context/AppContext';
import { dataAccess } from '@/data';
import { colors, radius, spacing, typography } from '@/theme/theme';
import { formatDisplayDate } from '@/utils/date';

export function SettingsScreen() {
  const { consent, setSyncEnabled, notifyDataChanged, refreshConsent } = useApp();
  const [busy, setBusy] = useState(false);

  async function toggleSync(next: boolean) {
    if (next) {
      // Turning sync ON is a deliberate, confirmed action — never implicit.
      Alert.alert(
        'Turn on cloud sync?',
        'Your entries will be eligible to leave this device and sync to the cloud. You can turn this off again any time.',
        [
          { text: 'Cancel', style: 'cancel' },
          {
            text: 'Turn on',
            style: 'default',
            onPress: async () => {
              await setSyncEnabled(true);
            },
          },
        ],
      );
    } else {
      await setSyncEnabled(false);
    }
  }

  function confirmReset() {
    Alert.alert(
      'Delete all data?',
      'This permanently removes every symptom log, cycle note and assessment from this device. This cannot be undone.',
      [
        { text: 'Cancel', style: 'cancel' },
        {
          text: 'Delete everything',
          style: 'destructive',
          onPress: async () => {
            setBusy(true);
            try {
              await dataAccess.resetAllData();
              await refreshConsent();
              notifyDataChanged();
              Alert.alert('Done', 'All local data has been deleted.');
            } finally {
              setBusy(false);
            }
          },
        },
      ],
    );
  }

  return (
    <Screen>
      <Title>Settings</Title>

      <Card>
        <Heading>Privacy</Heading>
        <View style={styles.toggleRow}>
          <View style={styles.toggleText}>
            <Text style={styles.toggleTitle}>Cloud sync</Text>
            <Body muted>
              {consent.syncEnabled
                ? 'On. Your data can sync across devices.'
                : 'Off. Your data stays on this device only.'}
            </Body>
          </View>
          <Switch
            value={consent.syncEnabled}
            onValueChange={toggleSync}
            trackColor={{ false: colors.surfaceMuted, true: colors.accentSoft }}
            thumbColor={consent.syncEnabled ? colors.accent : colors.surface}
            accessibilityLabel="Cloud sync"
          />
        </View>
        <View style={styles.divider} />
        <View style={styles.statusRow}>
          <Text style={styles.statusIcon}>{consent.syncEnabled ? '☁️' : '🔒'}</Text>
          <Body muted style={{ flex: 1 }}>
            {consent.syncEnabled
              ? 'While sync is on, the app may transmit your health data. All egress still passes through a single guarded boundary in code.'
              : 'While sync is off, the app makes no network calls with your health data — this is enforced in code, not just here in the UI.'}
          </Body>
        </View>
      </Card>

      <Card>
        <Heading>Consent</Heading>
        <Body muted>
          {consent.consentAcceptedAt
            ? `You accepted on-device storage on ${formatDisplayDate(consent.consentAcceptedAt)}.`
            : 'No consent recorded yet.'}
        </Body>
      </Card>

      <Card>
        <Heading>Your data</Heading>
        <Body muted>
          Everything you record lives in a local database on this phone. Deleting the app removes
          it. You can also wipe it now:
        </Body>
        <View style={{ marginTop: spacing.sm }}>
          <PrimaryButton
            variant="secondary"
            label="Delete all local data"
            onPress={confirmReset}
            loading={busy}
          />
        </View>
      </Card>

      <Body muted style={styles.footnote}>
        AfterNoon does not include third-party analytics or crash-reporting that transmits your
        health or behavioral data.
      </Body>
    </Screen>
  );
}

const styles = StyleSheet.create({
  toggleRow: { flexDirection: 'row', alignItems: 'center', gap: spacing.md },
  toggleText: { flex: 1, gap: spacing.xs },
  toggleTitle: { ...typography.body, color: colors.text, fontWeight: '600' },
  divider: { height: 1, backgroundColor: colors.border, marginVertical: spacing.md },
  statusRow: { flexDirection: 'row', gap: spacing.md, alignItems: 'flex-start' },
  statusIcon: { fontSize: 20 },
  footnote: { textAlign: 'center', marginTop: spacing.sm },
});
