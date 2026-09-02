import React, { useCallback, useEffect, useState } from "react";
import { FlatList, Pressable, StyleSheet, Text, View } from "react-native";
import { Sparkline } from "../components/Sparkline";
import { fetchPlant, fetchTwinComparison, setTreatmentPlanCompleted } from "../services/api";
import { scoreColor, theme } from "../theme/theme";
import { CheckIn, PlantDetail, TwinComparison } from "../types/domain";

interface Props {
  plantId: string;
  onCheckIn: (plant: PlantDetail) => void;
  onViewPhotoTimeline: (plant: PlantDetail) => void;
}

/** Per-plant detail view (spec §4.4): score history + diagnostic log. */
export function PlantDetailScreen({ plantId, onCheckIn, onViewPhotoTimeline }: Props) {
  const [plant, setPlant] = useState<PlantDetail | null>(null);
  const [twin, setTwin] = useState<TwinComparison | null>(null);

  const load = useCallback(async () => {
    setPlant(await fetchPlant(plantId));
    fetchTwinComparison(plantId).then(setTwin).catch(() => setTwin(null));
  }, [plantId]);

  useEffect(() => {
    load();
  }, [load]);

  if (!plant) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Loading…</Text>
      </View>
    );
  }

  const history = [...plant.scoreHistory].sort(
    (a, b) => new Date(a.computedAt).getTime() - new Date(b.computedAt).getTime(),
  );

  return (
    <FlatList
      contentContainerStyle={styles.list}
      data={plant.checkIns}
      keyExtractor={(c) => c.id}
      renderItem={({ item }) => <CheckInRow checkIn={item} onChanged={load} />}
      ListHeaderComponent={
        <>
          <View style={styles.heroCard}>
            <Text style={styles.plantName}>{plant.nickname || plant.speciesName}</Text>
            <Text style={styles.plantSpecies}>{plant.speciesName}</Text>
            <Text style={[styles.heroScore, { color: scoreColor(plant.scoreCurrent) }]}>
              {Math.round(plant.scoreCurrent)}
            </Text>
            {history.length > 1 && (
              <View style={styles.sparklineWrap}>
                <Sparkline values={history.map((h) => h.score)} color={scoreColor(plant.scoreCurrent)} />
              </View>
            )}
            <Pressable style={styles.primaryButton} onPress={() => onCheckIn(plant)}>
              <Text style={styles.primaryButtonText}>Check in now</Text>
            </Pressable>
            {plant.checkIns.length >= 2 && (
              <Pressable style={styles.secondaryButton} onPress={() => onViewPhotoTimeline(plant)}>
                <Text style={styles.secondaryButtonText}>Compare photos</Text>
              </Pressable>
            )}
            {twin && twin.cohortSize > 0 && (
              <Text style={styles.twinText}>🌱 {twin.message}</Text>
            )}
            {plant.forecast?.approachingAttentionThreshold && (
              <View style={styles.forecastBanner}>
                <Text style={styles.forecastText}>
                  ⚠ Trending down — projected around {plant.forecast.projectedScore} in{" "}
                  {plant.forecast.daysAhead} days if this continues. Worth a check-in soon.
                </Text>
              </View>
            )}
          </View>
          <Text style={styles.sectionTitle}>History</Text>
        </>
      }
      ListEmptyComponent={<Text style={styles.bodyText}>No check-ins yet — start the streak above.</Text>}
    />
  );
}

function CheckInRow({ checkIn, onChanged }: { checkIn: CheckIn; onChanged: () => void }) {
  const openFlags = checkIn.diagnosticFlags.filter((f) => f.status !== "RESOLVED");

  async function handleToggleTreatment(treatmentPlanId: string, completed: boolean) {
    await setTreatmentPlanCompleted(treatmentPlanId, completed).catch(() => undefined);
    onChanged();
  }

  return (
    <View style={styles.checkInRow}>
      <View style={styles.checkInHeader}>
        <Text style={styles.checkInDate}>{new Date(checkIn.timestamp).toLocaleDateString()}</Text>
        <Text style={[styles.checkInScore, { color: scoreColor(checkIn.computedScore) }]}>
          {Math.round(checkIn.computedScore)}
        </Text>
      </View>
      {openFlags.map((f) => (
        <View key={f.id}>
          <Text style={styles.flagText}>
            ⚠ {f.condition.replace(/-/g, " ")} — {f.urgency.replace(/_/g, " ").toLowerCase()}
          </Text>
          {f.treatmentPlan && !f.treatmentPlan.completed && (
            <View style={styles.treatmentBox}>
              {f.treatmentPlan.steps.map((step, i) => (
                <Text key={i} style={styles.treatmentStep}>
                  • {step}
                </Text>
              ))}
              {f.treatmentPlan.productsRecommended.length > 0 && (
                <Text style={styles.treatmentProducts}>
                  Suggested: {f.treatmentPlan.productsRecommended.join(", ")}
                </Text>
              )}
              <Pressable
                style={styles.treatmentDoneButton}
                onPress={() => handleToggleTreatment(f.treatmentPlan!.id, true)}
              >
                <Text style={styles.treatmentDoneButtonText}>Mark treated</Text>
              </Pressable>
            </View>
          )}
        </View>
      ))}
    </View>
  );
}

const styles = StyleSheet.create({
  list: { backgroundColor: theme.color.cream, padding: theme.spacing(2), flexGrow: 1 },
  center: { flex: 1, alignItems: "center", justifyContent: "center", backgroundColor: theme.color.cream },
  heroCard: { alignItems: "center", paddingVertical: theme.spacing(4) },
  plantName: { fontSize: theme.font.titleSize, fontWeight: "700", color: theme.color.textPrimary },
  plantSpecies: { fontSize: theme.font.captionSize, color: theme.color.textSecondary, marginBottom: theme.spacing(1) },
  heroScore: { fontSize: theme.font.heroSize, fontWeight: "800" },
  sparklineWrap: { width: "100%", paddingHorizontal: theme.spacing(4), marginTop: theme.spacing(2) },
  sectionTitle: {
    fontSize: theme.font.titleSize,
    fontWeight: "700",
    color: theme.color.textPrimary,
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(1),
  },
  bodyText: { fontSize: theme.font.bodySize, color: theme.color.textSecondary },
  primaryButton: {
    marginTop: theme.spacing(3),
    backgroundColor: theme.color.forestGreen,
    paddingHorizontal: theme.spacing(4),
    paddingVertical: theme.spacing(2),
    borderRadius: theme.radius.md,
  },
  primaryButtonText: { color: theme.color.cream, fontWeight: "600", fontSize: theme.font.bodySize },
  secondaryButton: {
    marginTop: theme.spacing(1.5),
    paddingHorizontal: theme.spacing(4),
    paddingVertical: theme.spacing(1),
  },
  secondaryButtonText: { color: theme.color.forestGreen, fontWeight: "600", fontSize: theme.font.captionSize },
  twinText: {
    marginTop: theme.spacing(2),
    fontSize: theme.font.captionSize,
    color: theme.color.textSecondary,
    textAlign: "center",
    paddingHorizontal: theme.spacing(3),
  },
  forecastBanner: {
    marginTop: theme.spacing(2),
    marginHorizontal: theme.spacing(3),
    backgroundColor: "#FBF0E8",
    borderRadius: theme.radius.sm,
    padding: theme.spacing(2),
  },
  forecastText: {
    fontSize: theme.font.captionSize,
    color: theme.color.danger,
    textAlign: "center",
  },
  checkInRow: {
    backgroundColor: "white",
    borderRadius: theme.radius.md,
    padding: theme.spacing(2),
    marginBottom: theme.spacing(1),
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: theme.color.border,
  },
  checkInHeader: { flexDirection: "row", justifyContent: "space-between" },
  checkInDate: { fontSize: theme.font.bodySize, color: theme.color.textPrimary },
  checkInScore: { fontSize: theme.font.bodySize, fontWeight: "700" },
  flagText: { fontSize: theme.font.captionSize, color: theme.color.danger, marginTop: theme.spacing(1) },
  treatmentBox: {
    backgroundColor: theme.color.cream,
    borderRadius: theme.radius.sm,
    padding: theme.spacing(1.5),
    marginTop: theme.spacing(1),
  },
  treatmentStep: { fontSize: theme.font.captionSize, color: theme.color.textPrimary, marginBottom: theme.spacing(0.5) },
  treatmentProducts: {
    fontSize: theme.font.captionSize,
    color: theme.color.textSecondary,
    fontStyle: "italic",
    marginTop: theme.spacing(0.5),
  },
  treatmentDoneButton: { marginTop: theme.spacing(1), alignSelf: "flex-start" },
  treatmentDoneButtonText: { fontSize: theme.font.captionSize, color: theme.color.forestGreen, fontWeight: "600" },
});
