import { useFocusEffect } from '@react-navigation/native';
import type { CompositeScreenProps } from '@react-navigation/native';
import type { BottomTabScreenProps } from '@react-navigation/bottom-tabs';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';
import React, { useCallback, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { Body, Card, Screen, Title } from '@/components/ui';
import { CheckInCard } from './CheckInCard';
import { dataAccess, type Severity } from '@/data';
import { useApp } from '@/context/AppContext';
import { colors, radius, spacing, typography } from '@/theme/theme';
import { formatDisplayDate, toLocalDateKey } from '@/utils/date';
import type { MainTabParamList, RootStackParamList } from '@/navigation/types';

type Props = CompositeScreenProps<
  BottomTabScreenProps<MainTabParamList, 'Home'>,
  NativeStackScreenProps<RootStackParamList>
>;

export function HomeScreen({ navigation }: Props) {
  const { dataVersion, notifyDataChanged } = useApp();
  const [todayByCategory, setTodayByCategory] = useState<Record<string, Severity | undefined>>({});
  const [savingCategory, setSavingCategory] = useState<string | null>(null);
  const [logCount, setLogCount] = useState(0);

  const load = useCallback(async () => {
    const dateKey = toLocalDateKey();
    const [logs, total] = await Promise.all([
      dataAccess.symptomLogs.getForDay(dateKey),
      dataAccess.symptomLogs.countAll(),
    ]);
    // Most-recent log per category wins (list is created_at DESC).
    const map: Record<string, Severity | undefined> = {};
    for (const log of logs) {
      if (map[log.category] === undefined) map[log.category] = log.severity;
    }
    setTodayByCategory(map);
    setLogCount(total);
  }, []);

  useFocusEffect(
    useCallback(() => {
      void load();
    }, [load, dataVersion]),
  );

  const handleLog = useCallback(
    async (category: string, severity: Severity) => {
      setSavingCategory(category);
      // Optimistic update so the tap feels instant.
      setTodayByCategory((prev) => ({ ...prev, [category]: severity }));
      try {
        await dataAccess.symptomLogs.create({ category, severity });
        notifyDataChanged();
        await load();
      } finally {
        setSavingCategory(null);
      }
    },
    [load, notifyDataChanged],
  );

  const loggedToday = Object.keys(todayByCategory).length;

  return (
    <Screen>
      <View style={styles.header}>
        <View style={{ flex: 1 }}>
          <Text style={styles.date}>{formatDisplayDate(new Date())}</Text>
          <Title>How are you today?</Title>
        </View>
        <Pressable
          accessibilityRole="button"
          accessibilityLabel="Settings"
          onPress={() => navigation.navigate('Settings')}
          style={styles.settingsBtn}
        >
          <Text style={styles.settingsIcon}>⚙️</Text>
        </Pressable>
      </View>

      {/* Placeholder insight line — wired to a real model once we have enough
          history. Kept as a clearly-labelled stub per the build plan. */}
      <Card style={styles.insight}>
        <Text style={styles.insightIcon}>💡</Text>
        <View style={{ flex: 1 }}>
          <Text style={styles.insightLabel}>Insight</Text>
          <Body muted>
            {logCount < 3
              ? 'Log a few days of symptoms and personalised insights will appear here.'
              : `You've recorded ${logCount} entries so far. Pattern insights are coming soon.`}
          </Body>
        </View>
      </Card>

      <CheckInCard
        todayByCategory={todayByCategory}
        onLog={handleLog}
        savingCategory={savingCategory}
      />

      {loggedToday > 0 && (
        <Body muted style={styles.footnote}>
          {loggedToday} {loggedToday === 1 ? 'symptom' : 'symptoms'} logged today. See patterns in
          the Trends tab.
        </Body>
      )}
    </Screen>
  );
}

const styles = StyleSheet.create({
  header: { flexDirection: 'row', alignItems: 'flex-start', gap: spacing.md },
  date: { ...typography.caption, color: colors.textMuted },
  settingsBtn: {
    width: 40,
    height: 40,
    borderRadius: radius.pill,
    backgroundColor: colors.surface,
    borderWidth: 1,
    borderColor: colors.border,
    alignItems: 'center',
    justifyContent: 'center',
  },
  settingsIcon: { fontSize: 18 },
  insight: { flexDirection: 'row', gap: spacing.md, backgroundColor: colors.trustSoft, borderColor: colors.trustSoft },
  insightIcon: { fontSize: 22 },
  insightLabel: { ...typography.label, color: colors.trust, marginBottom: 2 },
  footnote: { textAlign: 'center' },
});
