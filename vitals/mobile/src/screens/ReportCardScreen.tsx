import React, { useEffect, useRef, useState } from "react";
import { Pressable, ScrollView, Share, StyleSheet, Text, View } from "react-native";
import * as Sharing from "expo-sharing";
import ViewShot from "react-native-view-shot";
import { fetchWeeklyReportCard } from "../services/api";
import { scoreColor, theme } from "../theme/theme";
import { ReportCardPlantSummary, WeeklyReportCard } from "../types/domain";

interface Props {
  gardenId: string;
}

/**
 * Weekly "Garden Report Card" (spec §4.5/§4.6): a re-engagement recap the
 * user can share. The card itself doubles as the shareable asset — it's
 * captured with react-native-view-shot and shared as a branded PNG via the
 * native share sheet, falling back to a plain-text share if image sharing
 * isn't available on the platform.
 */
export function ReportCardScreen({ gardenId }: Props) {
  const [card, setCard] = useState<WeeklyReportCard | null>(null);
  const [loadError, setLoadError] = useState(false);
  const [sharing, setSharing] = useState(false);
  const shotRef = useRef<ViewShot>(null);

  function load() {
    setLoadError(false);
    fetchWeeklyReportCard(gardenId)
      .then(setCard)
      .catch(() => setLoadError(true));
  }

  useEffect(load, [gardenId]);

  if (loadError) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Couldn't load this week's report.</Text>
        <Pressable style={styles.shareButton} onPress={load}>
          <Text style={styles.shareButtonText}>Try again</Text>
        </Pressable>
      </View>
    );
  }

  if (!card) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Loading this week's report…</Text>
      </View>
    );
  }

  async function handleShare() {
    if (!card) return;
    setSharing(true);
    try {
      const canShareImage = (await Sharing.isAvailableAsync()) && shotRef.current?.capture;
      if (canShareImage) {
        const uri = await shotRef.current!.capture!();
        await Sharing.shareAsync(uri, { mimeType: "image/png", dialogTitle: "Share your Garden Report Card" });
        return;
      }
    } catch {
      // fall through to text share below
    } finally {
      setSharing(false);
    }
    await Share.share({ message: buildShareText(card) });
  }

  return (
    <ScrollView contentContainerStyle={styles.screen}>
      <ViewShot ref={shotRef} options={{ format: "png", quality: 1 }} style={styles.shotWrap}>
        <View style={styles.card}>
          <Text style={styles.brand}>🌿 Vitals</Text>
          <Text style={styles.periodLabel}>
            {new Date(card.periodStart).toLocaleDateString()} – {new Date(card.periodEnd).toLocaleDateString()}
          </Text>
          <Text style={[styles.heroScore, { color: scoreColor(card.gardenScoreEnd) }]}>{card.gardenScoreEnd}</Text>
          <Text style={styles.deltaText}>
            {card.gardenScoreDelta > 0 ? "▲" : card.gardenScoreDelta < 0 ? "▼" : "—"} {Math.abs(card.gardenScoreDelta)}{" "}
            pts this week
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
        </View>
      </ViewShot>

      <Pressable style={styles.shareButton} onPress={handleShare} disabled={sharing}>
        <Text style={styles.shareButtonText}>{sharing ? "Preparing…" : "Share this week's report"}</Text>
      </Pressable>
    </ScrollView>
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
  screen: { flexGrow: 1, backgroundColor: theme.color.cream, padding: theme.spacing(3), alignItems: "center" },
  center: { flex: 1, alignItems: "center", justifyContent: "center", backgroundColor: theme.color.cream },
  shotWrap: { width: "100%" },
  card: {
    width: "100%",
    backgroundColor: "white",
    borderRadius: theme.radius.lg,
    padding: theme.spacing(4),
    alignItems: "center",
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: theme.color.border,
  },
  brand: {
    fontSize: theme.font.captionSize,
    fontWeight: "700",
    color: theme.color.forestGreen,
    letterSpacing: 1,
    marginBottom: theme.spacing(1),
  },
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
