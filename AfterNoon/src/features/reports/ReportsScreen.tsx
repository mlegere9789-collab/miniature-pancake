import { useFocusEffect } from '@react-navigation/native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';
import React, { useCallback, useMemo, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { Body, Card, Heading, PrimaryButton, Screen, Title } from '@/components/ui';
import { dataAccess } from '@/data';
import { colors, radius, spacing, typography } from '@/theme/theme';
import { endOfTodayIso, formatDisplayDate, startOfDaysAgoIso } from '@/utils/date';
import type { ReportsStackParamList } from '@/navigation/types';

type Props = NativeStackScreenProps<ReportsStackParamList, 'ReportsHome'>;

interface RangePreset {
  key: string;
  label: string;
  days: number;
}

const PRESETS: RangePreset[] = [
  { key: '30d', label: 'Last 30 days', days: 30 },
  { key: '90d', label: 'Last 90 days', days: 90 },
  { key: '180d', label: 'Last 6 months', days: 180 },
  { key: '365d', label: 'Last 12 months', days: 365 },
];

export function ReportsScreen({ navigation }: Props) {
  const [selectedKey, setSelectedKey] = useState<string>('90d');
  const [totalLogs, setTotalLogs] = useState(0);

  useFocusEffect(
    useCallback(() => {
      void dataAccess.symptomLogs.countAll().then(setTotalLogs);
    }, []),
  );

  const preset = useMemo(
    () => PRESETS.find((p) => p.key === selectedKey) ?? PRESETS[1]!,
    [selectedKey],
  );

  const startIso = startOfDaysAgoIso(preset.days - 1);
  const endIso = endOfTodayIso();

  return (
    <Screen>
      <Title>Doctor Report</Title>
      <Body muted>
        Build a clear summary of your symptoms to bring to an appointment. It&apos;s generated on
        your device from your own logs — you choose whether and how to share it.
      </Body>

      <Card>
        <Heading>Choose a date range</Heading>
        <View style={styles.presets}>
          {PRESETS.map((p) => {
            const active = p.key === selectedKey;
            return (
              <Pressable
                key={p.key}
                onPress={() => setSelectedKey(p.key)}
                accessibilityRole="button"
                accessibilityState={{ selected: active }}
                style={[styles.chip, active && styles.chipActive]}
              >
                <Text style={[styles.chipText, active && styles.chipTextActive]}>{p.label}</Text>
              </Pressable>
            );
          })}
        </View>
        <View style={styles.rangeSummary}>
          <Text style={styles.rangeText}>
            {formatDisplayDate(startIso)} → {formatDisplayDate(endIso)}
          </Text>
        </View>
      </Card>

      <PrimaryButton
        label="Preview report"
        onPress={() => navigation.navigate('ReportPreview', { startIso, endIso })}
      />

      {totalLogs === 0 && (
        <Card style={styles.notice}>
          <Body muted>
            You haven&apos;t logged any symptoms yet. The report will be mostly empty until you
            record a few check-ins on the Home tab.
          </Body>
        </Card>
      )}

      <Card style={styles.privacyNote}>
        <Text style={styles.privacyIcon}>🔒</Text>
        <Body muted style={{ flex: 1 }}>
          Nothing is uploaded. The PDF is created on this phone; sharing it is entirely your
          choice, through your device&apos;s own share sheet.
        </Body>
      </Card>
    </Screen>
  );
}

const styles = StyleSheet.create({
  presets: { flexDirection: 'row', flexWrap: 'wrap', gap: spacing.sm, marginTop: spacing.sm },
  chip: {
    paddingVertical: spacing.sm,
    paddingHorizontal: spacing.md,
    borderRadius: radius.pill,
    borderWidth: 1.5,
    borderColor: colors.border,
    backgroundColor: colors.surface,
  },
  chipActive: { borderColor: colors.accent, backgroundColor: colors.accentSoft },
  chipText: { ...typography.label, color: colors.textMuted },
  chipTextActive: { color: colors.text },
  rangeSummary: { marginTop: spacing.md },
  rangeText: { ...typography.body, color: colors.text, fontWeight: '600' },
  notice: { backgroundColor: colors.surfaceMuted, borderColor: colors.surfaceMuted },
  privacyNote: {
    flexDirection: 'row',
    gap: spacing.md,
    alignItems: 'center',
    backgroundColor: colors.trustSoft,
    borderColor: colors.trustSoft,
  },
  privacyIcon: { fontSize: 22 },
});
