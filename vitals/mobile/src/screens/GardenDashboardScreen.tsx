import React, { useCallback, useEffect, useState } from "react";
import { FlatList, RefreshControl, StyleSheet, Text, View } from "react-native";
import { fetchGarden } from "../services/api";
import { theme, scoreColor } from "../theme/theme";
import { Garden, Plant } from "../types/domain";

interface Props {
  gardenId: string;
  onSelectPlant: (plant: Plant) => void;
}

/** Home screen (spec §4.3): hero Garden Score, Needs Attention, Rising Stars. */
export function GardenDashboardScreen({ gardenId, onSelectPlant }: Props) {
  const [garden, setGarden] = useState<Garden | null>(null);
  const [refreshing, setRefreshing] = useState(false);

  const load = useCallback(async () => {
    setGarden(await fetchGarden(gardenId));
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
