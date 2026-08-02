import type { NativeStackScreenProps } from '@react-navigation/native-stack';
import React, { useCallback, useEffect, useState } from 'react';
import { ActivityIndicator, Alert, StyleSheet, Text, View } from 'react-native';

import { Body, Card, Heading, PrimaryButton, Screen, Title } from '@/components/ui';
import { SEVERITY_LABELS } from '@/constants/symptoms';
import { buildReportData, type ReportData } from '@/data/reportData';
import { exportReportPdf } from './pdfExport';
import { colors, radius, spacing, typography } from '@/theme/theme';
import { formatDisplayDate, nowIso } from '@/utils/date';
import type { ReportsStackParamList } from '@/navigation/types';

type Props = NativeStackScreenProps<ReportsStackParamList, 'ReportPreview'>;

export function ReportPreviewScreen({ route }: Props) {
  const { startIso, endIso } = route.params;
  const [data, setData] = useState<ReportData | null>(null);
  const [loading, setLoading] = useState(true);
  const [exporting, setExporting] = useState(false);

  useEffect(() => {
    let cancelled = false;
    (async () => {
      const report = await buildReportData(startIso, endIso, nowIso());
      if (!cancelled) {
        setData(report);
        setLoading(false);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [startIso, endIso]);

  const handleExport = useCallback(async () => {
    if (!data) return;
    setExporting(true);
    try {
      const result = await exportReportPdf(data);
      if (!result.shared) {
        Alert.alert('PDF ready', `Saved to:\n${result.uri}`);
      }
    } catch (err) {
      Alert.alert('Export failed', err instanceof Error ? err.message : 'Please try again.');
    } finally {
      setExporting(false);
    }
  }, [data]);

  if (loading || !data) {
    return (
      <Screen scroll={false} contentStyle={styles.center}>
        <ActivityIndicator color={colors.accent} size="large" />
      </Screen>
    );
  }

  return (
    <Screen>
      {/* This in-app preview and the exported PDF are both rendered from the
          same ReportData, so what you see is what gets exported. */}
      <View style={styles.docHeader}>
        <Title>Symptom Summary</Title>
        <Text style={styles.range}>
          {formatDisplayDate(data.startDateKey)} – {formatDisplayDate(data.endDateKey)}
        </Text>
        <Text style={styles.generated}>Generated {formatDisplayDate(data.generatedAt)}</Text>
        {data.stagingLabel && (
          <Text style={styles.staging}>Self-identified stage: {data.stagingLabel}</Text>
        )}
      </View>

      <Card>
        <Heading>Symptom overview</Heading>
        {data.categorySummaries.length === 0 ? (
          <Body muted>No symptoms logged in this range.</Body>
        ) : (
          <>
            <View style={[styles.tRow, styles.tHead]}>
              <Text style={[styles.tCell, styles.tName]}>Symptom</Text>
              <Text style={[styles.tCell, styles.tNum]}>Avg</Text>
              <Text style={[styles.tCell, styles.tNum]}>Peak</Text>
              <Text style={[styles.tCell, styles.tNum]}>Days</Text>
            </View>
            {data.categorySummaries.map((s) => (
              <View key={s.category} style={styles.tRow}>
                <Text style={[styles.tCell, styles.tName]}>
                  {s.emoji} {s.label}
                </Text>
                <Text style={[styles.tCell, styles.tNum]}>{s.avgSeverity.toFixed(1)}</Text>
                <Text style={[styles.tCell, styles.tNum]}>
                  {s.maxSeverity}
                </Text>
                <Text style={[styles.tCell, styles.tNum]}>{s.daysLogged}</Text>
              </View>
            ))}
            <Text style={styles.legendNote}>
              Peak {data.categorySummaries[0] ? SEVERITY_LABELS[data.categorySummaries[0].maxSeverity] : ''} · severity is a 1–5 self-rating
            </Text>
          </>
        )}
      </Card>

      <Card>
        <Heading>Cycle notes</Heading>
        {data.cycleNotes.length === 0 ? (
          <Body muted>No cycle notes recorded in this period.</Body>
        ) : (
          data.cycleNotes.map((c) => (
            <View key={c.id} style={styles.noteRow}>
              <Text style={styles.noteDate}>{formatDisplayDate(c.noteDate)}</Text>
              <Text style={styles.noteBody}>
                {c.flow ? `${c.flow}` : ''}
                {c.flow && c.note ? ' · ' : ''}
                {c.note ?? ''}
              </Text>
            </View>
          ))
        )}
      </Card>

      <Card style={styles.disclaimer}>
        <Body muted>
          This is a symptom diary summary to support a clinical conversation — not a diagnosis.
          Generated on-device from {data.totalLogs} self-recorded{' '}
          {data.totalLogs === 1 ? 'entry' : 'entries'}.
        </Body>
      </Card>

      <PrimaryButton
        label={exporting ? 'Preparing PDF…' : 'Export PDF & share'}
        onPress={handleExport}
        loading={exporting}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  center: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  docHeader: { gap: spacing.xs },
  range: { ...typography.body, color: colors.text, fontWeight: '600' },
  generated: { ...typography.caption, color: colors.textMuted },
  staging: { ...typography.body, color: colors.trust, marginTop: spacing.xs },
  tRow: { flexDirection: 'row', paddingVertical: spacing.sm, borderBottomWidth: 1, borderBottomColor: colors.border },
  tHead: { borderBottomWidth: 2 },
  tCell: { ...typography.caption, color: colors.text },
  tName: { flex: 2 },
  tNum: { flex: 1, textAlign: 'right' },
  legendNote: { ...typography.caption, color: colors.textMuted, marginTop: spacing.sm, fontStyle: 'italic' },
  noteRow: { paddingVertical: spacing.sm, borderBottomWidth: 1, borderBottomColor: colors.border },
  noteDate: { ...typography.label, color: colors.text },
  noteBody: { ...typography.body, color: colors.textMuted },
  disclaimer: { backgroundColor: colors.surfaceMuted, borderColor: colors.surfaceMuted },
});
