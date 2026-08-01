import React, { useMemo } from 'react';
import { ScrollView, StyleSheet, Text, View } from 'react-native';

import { SYMPTOM_BY_ID } from '@/constants/symptoms';
import { colors, radius, spacing, typography } from '@/theme/theme';
import { dateKeyRange } from '@/utils/date';
import type { DailyCategorySeverity } from '@/data';

interface Props {
  daily: DailyCategorySeverity[];
  startDateKey: string;
  endDateKey: string;
}

const CELL = 16;
const GAP = 3;

function severityColor(avg: number | undefined): string {
  if (avg === undefined) return colors.surfaceMuted;
  // avg is 1..5 -> index 0..4
  const idx = Math.min(4, Math.max(0, Math.round(avg) - 1));
  return colors.severity[idx] ?? colors.surfaceMuted;
}

export function Heatmap({ daily, startDateKey, endDateKey }: Props) {
  const days = useMemo(() => dateKeyRange(startDateKey, endDateKey), [startDateKey, endDateKey]);

  // category -> dateKey -> avgSeverity
  const grid = useMemo(() => {
    const m = new Map<string, Map<string, number>>();
    const cats = new Set<string>();
    for (const d of daily) {
      cats.add(d.category);
      const row = m.get(d.category) ?? new Map<string, number>();
      row.set(d.date, d.avgSeverity);
      m.set(d.category, row);
    }
    return { m, categories: Array.from(cats) };
  }, [daily]);

  if (grid.categories.length === 0) {
    return (
      <View style={styles.empty}>
        <Text style={styles.emptyText}>No symptoms logged in this range yet.</Text>
      </View>
    );
  }

  return (
    <View>
      <ScrollView horizontal showsHorizontalScrollIndicator={false}>
        <View>
          {grid.categories.map((cat) => {
            const row = grid.m.get(cat);
            const meta = SYMPTOM_BY_ID[cat];
            return (
              <View key={cat} style={styles.row}>
                <View style={styles.rowLabel}>
                  <Text style={styles.rowLabelText} numberOfLines={1}>
                    {meta?.emoji ?? '•'} {meta?.label ?? cat}
                  </Text>
                </View>
                <View style={styles.cells}>
                  {days.map((day) => (
                    <View
                      key={day}
                      style={[styles.cell, { backgroundColor: severityColor(row?.get(day)) }]}
                    />
                  ))}
                </View>
              </View>
            );
          })}
        </View>
      </ScrollView>
      <Legend />
    </View>
  );
}

function Legend() {
  return (
    <View style={styles.legend}>
      <Text style={styles.legendText}>Milder</Text>
      {colors.severity.map((c, i) => (
        <View key={i} style={[styles.legendCell, { backgroundColor: c }]} />
      ))}
      <Text style={styles.legendText}>Severe</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  row: { flexDirection: 'row', alignItems: 'center', marginBottom: GAP },
  rowLabel: { width: 110, paddingRight: spacing.sm },
  rowLabelText: { ...typography.caption, color: colors.text },
  cells: { flexDirection: 'row', gap: GAP },
  cell: { width: CELL, height: CELL, borderRadius: 3 },
  legend: { flexDirection: 'row', alignItems: 'center', gap: 4, marginTop: spacing.md },
  legendText: { ...typography.caption, color: colors.textMuted },
  legendCell: { width: CELL, height: CELL, borderRadius: 3 },
  empty: { padding: spacing.lg, alignItems: 'center' },
  emptyText: { ...typography.body, color: colors.textMuted, textAlign: 'center' },
});
