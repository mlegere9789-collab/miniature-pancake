import React, { useCallback, useEffect, useState } from "react";
import { FlatList, RefreshControl, StyleSheet, Text, View } from "react-native";
import { fetchGarden } from "../services/api";
import { pendingCheckInCount } from "../services/checkInQueue";
import { notifyOutbreakAlertsIfNew, notifyWeatherAlertIfNew } from "../services/notifications";
import { theme, scoreColor } from "../theme/theme";
import { Garden, OutbreakAlert, Plant, WeatherAlert } from "../types/domain";

interface Props {
  gardenId: string;
  onSelectPlant: (plant: Plant) => void;
}

/** Home screen (spec §4.3): hero Garden Score, Needs Attention, Rising Stars. */
export function GardenDashboardScreen({ gardenId, onSelectPlant }: Props) {
  const [garden, setGarden] = useState<Garden | null>(null);
  const [refreshing, setRefreshing] = useState(false);
  const [pendingSyncCount, setPendingSyncCount] = useState(0);

  const load = useCallback(async () => {
    const fetched = await fetchGarden(gardenId);
    setGarden(fetched);
    pendingCheckInCount().then(setPendingSyncCount).catch(() => undefined);

    if (fetched.weatherAlert) notifyWeatherAlertIfNew(fetched.weatherAlert).catch(() => undefined);
    if (fetched.outbreakAlerts.length > 0) notifyOutbreakAlertsIfNew(fetched.outbreakAlerts).catch(() => undefined);
  }, [gardenId]);

  useEffect(() => {
    load();
  }, [load]);

  async function onRefresh() {
    setRefreshing(true);
    await load();
    setRefreshing(false);
  }

  if (!garden) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Loading your garden…</Text>
      </View>
    );
  }

  const risingStarPlants = garden.plants.filter((p) =>
    garden.risingStars.some((r) => r.plantId === p.id),
  );

  return (
    <FlatList
      contentContainerStyle={styles.list}
      refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} />}
      ListHeaderComponent={
        <>
          <View style={styles.heroCard}>
            <Text style={styles.heroLabel}>{garden.name}</Text>
            <Text style={[styles.heroScore, { color: scoreColor(garden.scoreCurrent) }]}>
              {Math.round(garden.scoreCurrent)}
            </Text>
            <Text style={styles.heroCaption}>Garden Score</Text>
          </View>

          {garden.weatherAlert && (
            <FrostBanner alert={garden.weatherAlert} plants={garden.plants} />
          )}

          {garden.outbreakAlerts.length > 0 && <OutbreakBanner alerts={garden.outbreakAlerts} />}

          {pendingSyncCount > 0 && (
            <Text style={styles.pendingSyncText}>
              ⏳ {pendingSyncCount} check-in{pendingSyncCount === 1 ? "" : "s"} waiting to sync
            </Text>
          )}

          <Text style={styles.sectionTitle}>Needs Attention</Text>
        </>
      }
      data={garden.needsAttention}
      keyExtractor={(p) => p.id}
      renderItem={({ item }) => <PlantRow plant={item} onPress={() => onSelectPlant(item)} />}
      ListFooterComponent={
        risingStarPlants.length > 0 ? (
          <>
            <Text style={styles.sectionTitle}>Rising Stars</Text>
            {risingStarPlants.map((p) => (
              <PlantRow key={p.id} plant={p} onPress={() => onSelectPlant(p)} />
            ))}
          </>
        ) : null
      }
    />
  );
}

// Weather-aware banner (spec §4.3): "Frost tonight — 3 of your plants are frost-sensitive."
function FrostBanner({ alert, plants }: { alert: WeatherAlert; plants: Plant[] }) {
  const names = plants
    .filter((p) => alert.affectedPlantIds.includes(p.id))
    .map((p) => p.nickname || p.speciesName);

  return (
    <View style={styles.frostBanner}>
      <Text style={styles.frostBannerText}>
        ❄️ Frost tonight ({Math.round(alert.minTempTonightC)}°C) — {alert.affectedPlantIds.length} frost-sensitive
        plant{alert.affectedPlantIds.length === 1 ? "" : "s"}: {names.join(", ")}
      </Text>
    </View>
  );
}

// Regional outbreak banner (spec §4.3/§4.5, "Idea 4"): a condition being
// reported by multiple nearby gardens right now, e.g. "Powdery mildew
// reported in 4 nearby gardens this week." Never names which gardens.
function OutbreakBanner({ alerts }: { alerts: OutbreakAlert[] }) {
  return (
    <View style={styles.outbreakBanner}>
      {alerts.map((alert) => (
        <Text key={alert.condition} style={styles.outbreakBannerText}>
          🦠 {formatCondition(alert.condition)} reported in {alert.gardenCount} nearby gardens this week — worth a
          close look on your next check-in.
        </Text>
      ))}
    </View>
  );
}

function formatCondition(condition: string): string {
  const words = condition.replace(/-/g, " ");
  return words.charAt(0).toUpperCase() + words.slice(1);
}

function PlantRow({ plant, onPress }: { plant: Plant; onPress: () => void }) {
  return (
    <View style={styles.plantRow}>
      <View>
        <Text style={styles.plantName}>{plant.nickname || plant.speciesName}</Text>
        <Text style={styles.plantSpecies}>{plant.speciesName}</Text>
      </View>
      <Text style={[styles.plantScore, { color: scoreColor(plant.scoreCurrent) }]} onPress={onPress}>
        {Math.round(plant.scoreCurrent)}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  list: { backgroundColor: theme.color.cream, padding: theme.spacing(2), flexGrow: 1 },
  center: { flex: 1, alignItems: "center", justifyContent: "center", backgroundColor: theme.color.cream },
  heroCard: {
    alignItems: "center",
    paddingVertical: theme.spacing(5),
  },
  heroLabel: { fontSize: theme.font.titleSize, color: theme.color.textPrimary, fontWeight: "600" },
  heroScore: { fontSize: theme.font.heroSize, fontWeight: "800" },
  heroCaption: { fontSize: theme.font.captionSize, color: theme.color.textSecondary },
  frostBanner: {
    backgroundColor: theme.color.forestGreen,
    borderRadius: theme.radius.md,
    padding: theme.spacing(2),
    marginBottom: theme.spacing(1),
  },
  frostBannerText: { color: theme.color.cream, fontSize: theme.font.captionSize, lineHeight: 18 },
  outbreakBanner: {
    backgroundColor: "#FBF0E8",
    borderRadius: theme.radius.md,
    padding: theme.spacing(2),
    marginBottom: theme.spacing(1),
  },
  outbreakBannerText: { color: theme.color.danger, fontSize: theme.font.captionSize, lineHeight: 18 },
  pendingSyncText: {
    fontSize: theme.font.captionSize,
    color: theme.color.textSecondary,
    textAlign: "center",
    marginBottom: theme.spacing(1),
  },
  sectionTitle: {
    fontSize: theme.font.titleSize,
    fontWeight: "700",
    color: theme.color.textPrimary,
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(1),
  },
  bodyText: { fontSize: theme.font.bodySize, color: theme.color.textPrimary },
  plantRow: {
    flexDirection: "row",
    justifyContent: "space-between",
    alignItems: "center",
    paddingVertical: theme.spacing(2),
    paddingHorizontal: theme.spacing(2),
    backgroundColor: "white",
    borderRadius: theme.radius.md,
    marginBottom: theme.spacing(1),
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: theme.color.border,
  },
  plantName: { fontSize: theme.font.bodySize, fontWeight: "600", color: theme.color.textPrimary },
  plantSpecies: { fontSize: theme.font.captionSize, color: theme.color.textSecondary },
  plantScore: { fontSize: 28, fontWeight: "700" },
});
