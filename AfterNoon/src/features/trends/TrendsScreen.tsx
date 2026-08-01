import { useFocusEffect } from '@react-navigation/native';
import React, { useCallback, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { Body, Card, Heading, Screen, Title } from '@/components/ui';
import { Heatmap } from './Heatmap';
import { buildReportData, type ReportData } from '@/data/reportData';
import { useApp } from '@/context/AppContext';
import { colors, radius, spacing, typography } from '@/theme/theme';
import { endOfTodayIso, nowIso, startOfDaysAgoIso } from '@/utils/date';

type RangeOption = 30 | 90;

export function TrendsScreen() {
  const { dataVersion } = useApp();
  const [range, setRange] = useState<RangeOption>(30);
  const [data, setData] = useState<ReportData | null>(null);
  const [loading, setLoading] = useState(true);

  const load = useCallback(async (days: RangeOption) => {
    setLoading(true);
    try {
      const startIso = startOfDaysAgoIso(days - 1);
      const endIso = endOfTodayIso();
      const report = await buildReportData(startIso, endIso, nowIso());
      setData(report);
    } finally {
      setLoading(false);
    }
  }, []);

  useFocusEffect(
    useCallback(() => {
      void load(range);
    }, [load, range, dataVersion]),
  );

  return (
    <Screen>
      <Title>Trends</Title>
      <Body muted>How your symptoms have moved over time — built from your on-device logs.</Body>

      <View style={styles.rangeRow}>
        {([30, 90] as RangeOption[]).map((r) => {
          const active = range === r;
          return (
            <Pressable
              key={r}
              onPress={() => setRange(r)}
              accessibilityRole="button"
              accessibilityState={{ selected: active }}
              style={[styles.rangeChip, active && styles.rangeChipActive]}
            >
              <Text style={[styles.rangeText, active && styles.rangeTextActive]}>Last {r} days</Text>
            </Pressable>
          );
        })}
      </View>

      <Card>
        <Heading>Symptom heatmap</Heading>
        {loading ? (
          <Body muted>Loading…</Body>
        ) : data ? (
          <Heatmap daily={data.daily} startDateKey={data.startDateKey} endDateKey={data.endDateKey} />
        ) : null}
      </Card>

      {data && data.categorySummaries.length > 0 && (
        <Card>
          <Heading>Most-felt symptoms</Heading>
          {data.categorySummaries.slice(0, 5).map((s) => (
            <View key={s.category} style={styles.summaryRow}>
              <Text style={styles.summaryLabel}>
                {s.emoji} {s.label}
              </Text>
              <View style={styles.summaryMeta}>
                <Text style={styles.summaryStat}>avg {s.avgSeverity.toFixed(1)}</Text>
                <Text style={styles.summaryDays}>
                  {s.daysLogged} {s.daysLogged === 1 ? 'day' : 'days'}
                </Text>
              </View>
            </View>
          ))}
        </Card>
      )}

      {data && data.totalLogs === 0 && !loading && (
        <Card>
          <Body muted>
            Nothing logged in this window yet. Head to Home and record how you&apos;re feeling —
            your trends will build up here.
          </Body>
        </Card>
      )}
    </Screen>
  );
}

const styles = StyleSheet.create({
  rangeRow: { flexDirection: 'row', gap: spacing.sm },
  rangeChip: {
    paddingVertical: spacing.sm,
    paddingHorizontal: spacing.md,
    borderRadius: radius.pill,
    borderWidth: 1.5,
    borderColor: colors.border,
    backgroundColor: colors.surface,
  },
  rangeChipActive: { borderColor: colors.accent, backgroundColor: colors.accentSoft },
  rangeText: { ...typography.label, color: colors.textMuted },
  rangeTextActive: { color: colors.text },
  summaryRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: spacing.sm,
    borderBottomWidth: 1,
    borderBottomColor: colors.border,
  },
  summaryLabel: { ...typography.body, color: colors.text },
  summaryMeta: { flexDirection: 'row', gap: spacing.md, alignItems: 'center' },
  summaryStat: { ...typography.label, color: colors.accent },
  summaryDays: { ...typography.caption, color: colors.textMuted },
});
