import React from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { Body, Card, Heading } from '@/components/ui';
import { SEVERITY_LABELS, SYMPTOM_CATEGORIES } from '@/constants/symptoms';
import { colors, radius, spacing, typography } from '@/theme/theme';
import type { Severity } from '@/data';

interface Props {
  /** Latest severity logged today per category (undefined = not logged). */
  todayByCategory: Record<string, Severity | undefined>;
  onLog: (category: string, severity: Severity) => void;
  savingCategory: string | null;
}

const SEVERITIES: Severity[] = [1, 2, 3, 4, 5];

export function CheckInCard({ todayByCategory, onLog, savingCategory }: Props) {
  return (
    <Card>
      <Heading>Today&apos;s check-in</Heading>
      <Body muted>Tap a level for anything you&apos;re feeling. You can log more than once.</Body>

      <View style={styles.list}>
        {SYMPTOM_CATEGORIES.map((cat) => {
          const current = todayByCategory[cat.id];
          const logged = current !== undefined;
          return (
            <View key={cat.id} style={styles.row}>
              <View style={styles.rowHeader}>
                <Text style={styles.emoji}>{cat.emoji}</Text>
                <View style={styles.rowLabel}>
                  <Text style={styles.catLabel}>{cat.label}</Text>
                  {logged ? (
                    <Text style={styles.loggedHint}>Logged · {SEVERITY_LABELS[current]}</Text>
                  ) : cat.hint ? (
                    <Text style={styles.hint}>{cat.hint}</Text>
                  ) : null}
                </View>
              </View>
              <View style={styles.dots} accessibilityRole="radiogroup">
                {SEVERITIES.map((sev) => {
                  const active = current === sev;
                  return (
                    <Pressable
                      key={sev}
                      accessibilityRole="radio"
                      accessibilityLabel={`${cat.label} ${SEVERITY_LABELS[sev]}`}
                      accessibilityState={{ selected: active, disabled: savingCategory === cat.id }}
                      disabled={savingCategory === cat.id}
                      onPress={() => onLog(cat.id, sev)}
                      style={[
                        styles.dot,
                        { backgroundColor: colors.severity[sev - 1] },
                        active && styles.dotActive,
                      ]}
                    >
                      <Text style={[styles.dotText, active && styles.dotTextActive]}>{sev}</Text>
                    </Pressable>
                  );
                })}
              </View>
            </View>
          );
        })}
      </View>
    </Card>
  );
}

const styles = StyleSheet.create({
  list: { gap: spacing.md, marginTop: spacing.sm },
  row: { gap: spacing.sm },
  rowHeader: { flexDirection: 'row', alignItems: 'center', gap: spacing.sm },
  emoji: { fontSize: 22 },
  rowLabel: { flex: 1 },
  catLabel: { ...typography.body, color: colors.text, fontWeight: '600' },
  hint: { ...typography.caption, color: colors.textMuted },
  loggedHint: { ...typography.caption, color: colors.support, fontWeight: '600' },
  dots: { flexDirection: 'row', gap: spacing.sm },
  dot: {
    flex: 1,
    height: 40,
    borderRadius: radius.sm,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 2,
    borderColor: 'transparent',
  },
  dotActive: { borderColor: colors.text },
  dotText: { ...typography.label, color: colors.text, opacity: 0.7 },
  dotTextActive: { opacity: 1 },
});
