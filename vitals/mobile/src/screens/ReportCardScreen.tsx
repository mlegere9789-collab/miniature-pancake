import React, { useEffect, useState } from "react";
import { Pressable, Share, StyleSheet, Text, View } from "react-native";
import { fetchWeeklyReportCard } from "../services/api";
import { scoreColor, theme } from "../theme/theme";
import { ReportCardPlantSummary, WeeklyReportCard } from "../types/domain";

interface Props {
  gardenId: string;
}

/**
 * Weekly "Garden Report Card" (spec §4.5/§4.6): a re-engagement recap the
 * user can share. Phase 2 ships the data + a shareable text summary via the
 * native share sheet; a polished branded image card is further Phase 2/3
 * design work once the visual system (spec §6) has real assets.
 */
export function ReportCardScreen({ gardenId }: Props) {
  const [card, setCard] = useState<WeeklyReportCard | null>(null);

  useEffect(() => {
    fetchWeeklyReportCard(gardenId).then(setCard).catch(() => setCard(null));
  }, [gardenId]);

  if (!card) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Loading this week's report…</Text>
      </View>
    );
  }

  async function handleShare() {
    if (!card) return;
    await Share.share({ message: buildShareText(card) });
  }

  return (
    <View style={styles.container}>
      <Text style={styles.periodLabel}>
        {new Date(card.periodStart).toLocaleDateString()} – {new Date(card.periodEnd).toLocaleDateString()}
      </Text>
      <Text style={[styles.heroScore, { color: scoreColor(card.gardenScoreEnd) }]}>{card.gardenScoreEnd}</Text>
      <Text style={styles.deltaText}>
        {card.gardenScoreDelta > 0 ? "▲" : card.gardenScoreDelta < 0 ? "▼" : "—"} {Math.abs(card.gardenScoreDelta)} pts
        this week
      </Text>
      <Text style={styles.headline}>{card.headline}</Text>

      {card.topPlants.length > 0 && (
        <View style={styles.section}>
          <Text style={styles.sectionTitle}>Rising Stars</Text>
          {card.topPlants.map((p) => (
            <PlantLine key={p.plantId} plant={p} />
          ))}
        </View>
      )}

      {card.plantsNeedingAttention.length > 0 && (
        <View style={styles.section}>
          <Text style={styles.sectionTitle}>Needs Attention</Text>
          {card.plantsNeedingAttention.map((p) => (
            <PlantLine key={p.plantId} plant={p} />
          ))}
        </View>
      )}

      <Text style={styles.checkInsText}>{card.checkInsCompleted} check-ins completed this week</Text>

      <Pressable style={styles.shareButton} onPress={handleShare}>
        <Text style={styles.shareButtonText}>Share this week's report</Text>
      </Pressable>
    </View>
  );
}

function PlantLine({ plant }: { plant: ReportCardPlantSummary }) {
  return (
    <Text style={styles.plantLine}>
      {plant.name} — {plant.scoreEnd} ({plant.delta > 0 ? "+" : ""}
      {plant.delta})
    </Text>
  );
}

function buildShareText(card: WeeklyReportCard): string {
  const lines = [
    "My garden's weekly Vitals report",
    `Garden Score: ${card.gardenScoreEnd} (${card.gardenScoreDelta >= 0 ? "+" : ""}${card.gardenScoreDelta} this week)`,
    card.headline,
  ];
  if (card.topPlants.length > 0) {
    lines.push("Rising stars: " + card.topPlants.map((p) => `${p.name} (+${p.delta})`).join(", "));
  }
  return lines.join("\n");
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: theme.color.cream, padding: theme.spacing(3), alignItems: "center" },
  center: { flex: 1, alignItems: "center", justifyContent: "center", backgroundColor: theme.color.cream },
  periodLabel: { fontSize: theme.font.captionSize, color: theme.color.textSecondary },
  heroScore: { fontSize: theme.font.heroSize, fontWeight: "800", marginTop: theme.spacing(1) },
  deltaText: { fontSize: theme.font.bodySize, color: theme.color.textSecondary, marginBottom: theme.spacing(2) },
  headline: {
    fontSize: theme.font.bodySize,
    color: theme.color.textPrimary,
    textAlign: "center",
    marginBottom: theme.spacing(3),
  },
  section: { width: "100%", marginBottom: theme.spacing(2) },
  sectionTitle: { fontSize: theme.font.titleSize, fontWeight: "700", color: theme.color.textPrimary, marginBottom: theme.spacing(1) },
  plantLine: { fontSize: theme.font.bodySize, color: theme.color.textPrimary, marginBottom: theme.spacing(0.5) },
  bodyText: { fontSize: theme.font.bodySize, color: theme.color.textSecondary },
  checkInsText: { fontSize: theme.font.captionSize, color: theme.color.textSecondary, marginTop: theme.spacing(1) },
  shareButton: {
    marginTop: theme.spacing(3),
    backgroundColor: theme.color.forestGreen,
    paddingHorizontal: theme.spacing(4),
    paddingVertical: theme.spacing(2),
    borderRadius: theme.radius.md,
  },
  shareButtonText: { color: theme.color.cream, fontWeight: "600", fontSize: theme.font.bodySize },
});
