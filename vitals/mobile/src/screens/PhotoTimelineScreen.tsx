import React, { useMemo, useState } from "react";
import { ScrollView, StyleSheet, Text, TouchableOpacity, View } from "react-native";
import { BeforeAfterSlider } from "../components/BeforeAfterSlider";
import { toAbsoluteUrl } from "../services/api";
import { scoreColor, theme } from "../theme/theme";
import { CheckIn } from "../types/domain";

interface Props {
  plantLabel: string;
  checkIns: CheckIn[];
}

/**
 * Photo timeline (spec §4.4): a side-by-side slider comparing any two
 * check-ins, "visually striking, highly shareable." Defaults to the
 * earliest and most recent check-in so the comparison is meaningful on
 * first open; either side can be repointed at any other check-in.
 */
export function PhotoTimelineScreen({ plantLabel, checkIns }: Props) {
  const sorted = useMemo(
    () => [...checkIns].sort((a, b) => new Date(a.timestamp).getTime() - new Date(b.timestamp).getTime()),
    [checkIns],
  );

  const [beforeId, setBeforeId] = useState(sorted[0]?.id);
  const [afterId, setAfterId] = useState(sorted[sorted.length - 1]?.id);

  if (sorted.length < 2) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Need at least two check-ins to compare photos.</Text>
      </View>
    );
  }

  const before = sorted.find((c) => c.id === beforeId) ?? sorted[0];
  const after = sorted.find((c) => c.id === afterId) ?? sorted[sorted.length - 1];
  const beforeUri = toAbsoluteUrl(before.photoUrl);
  const afterUri = toAbsoluteUrl(after.photoUrl);

  return (
    <ScrollView contentContainerStyle={styles.container}>
      <Text style={styles.title}>{plantLabel}</Text>
      <Text style={styles.subtitle}>Drag the handle to compare</Text>

      {beforeUri && afterUri && <BeforeAfterSlider beforeUri={beforeUri} afterUri={afterUri} />}

      <Text style={styles.sectionTitle}>Before</Text>
      <CheckInPicker checkIns={sorted} selectedId={beforeId} onSelect={setBeforeId} />

      <Text style={styles.sectionTitle}>After</Text>
      <CheckInPicker checkIns={sorted} selectedId={afterId} onSelect={setAfterId} />
    </ScrollView>
  );
}

function CheckInPicker({
  checkIns,
  selectedId,
  onSelect,
}: {
  checkIns: CheckIn[];
  selectedId: string | undefined;
  onSelect: (id: string) => void;
}) {
  return (
    <View style={styles.pickerRow}>
      {checkIns.map((c) => {
        const selected = c.id === selectedId;
        return (
          <TouchableOpacity
            key={c.id}
            style={[styles.chip, selected && styles.chipSelected]}
            onPress={() => onSelect(c.id)}
          >
            <Text style={[styles.chipDate, selected && styles.chipTextSelected]}>
              {new Date(c.timestamp).toLocaleDateString()}
            </Text>
            <Text style={[styles.chipScore, { color: selected ? theme.color.cream : scoreColor(c.computedScore) }]}>
              {Math.round(c.computedScore)}
            </Text>
          </TouchableOpacity>
        );
      })}
    </View>
  );
}

const styles = StyleSheet.create({
  container: { padding: theme.spacing(3), backgroundColor: theme.color.cream, flexGrow: 1 },
  center: { flex: 1, alignItems: "center", justifyContent: "center", backgroundColor: theme.color.cream, padding: theme.spacing(3) },
  title: { fontSize: theme.font.titleSize, fontWeight: "700", color: theme.color.textPrimary },
  subtitle: { fontSize: theme.font.captionSize, color: theme.color.textSecondary, marginBottom: theme.spacing(2) },
  sectionTitle: {
    fontSize: theme.font.bodySize,
    fontWeight: "600",
    color: theme.color.textPrimary,
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(1),
  },
  bodyText: { fontSize: theme.font.bodySize, color: theme.color.textSecondary, textAlign: "center" },
  pickerRow: { flexDirection: "row", flexWrap: "wrap", gap: theme.spacing(1) },
  chip: {
    paddingHorizontal: theme.spacing(1.5),
    paddingVertical: theme.spacing(1),
    borderRadius: theme.radius.sm,
    backgroundColor: "white",
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: theme.color.border,
    alignItems: "center",
  },
  chipSelected: { backgroundColor: theme.color.forestGreen, borderColor: theme.color.forestGreen },
  chipDate: { fontSize: theme.font.captionSize, color: theme.color.textPrimary },
  chipTextSelected: { color: theme.color.cream },
  chipScore: { fontSize: theme.font.captionSize, fontWeight: "700" },
});
