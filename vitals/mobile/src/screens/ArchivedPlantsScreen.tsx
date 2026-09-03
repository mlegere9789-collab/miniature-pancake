import React, { useCallback, useState } from "react";
import { useFocusEffect } from "@react-navigation/native";
import { Alert, FlatList, Pressable, RefreshControl, StyleSheet, Text, View } from "react-native";
import { fetchArchivedPlants, unarchivePlant } from "../services/api";
import { theme } from "../theme/theme";
import { Plant } from "../types/domain";

interface Props {
  gardenId: string;
  onRestored: () => void;
}

/**
 * Archived plants (spec §4.1) — an accidental "Archive plant" shouldn't be
 * a one-way gate. Lists plants with active: false and lets any of them be
 * restored back into the active Garden Score roll-up, dashboard, and yard map.
 */
export function ArchivedPlantsScreen({ gardenId, onRestored }: Props) {
  const [plants, setPlants] = useState<Plant[] | null>(null);
  const [loadError, setLoadError] = useState(false);
  const [restoringId, setRestoringId] = useState<string | null>(null);
  const [refreshing, setRefreshing] = useState(false);

  const load = useCallback(async () => {
    try {
      setPlants(await fetchArchivedPlants(gardenId));
      setLoadError(false);
    } catch {
      setLoadError(true);
    }
  }, [gardenId]);

  async function onRefresh() {
    setRefreshing(true);
    await load();
    setRefreshing(false);
  }

  useFocusEffect(
    useCallback(() => {
      load();
    }, [load]),
  );

  async function handleRestore(plant: Plant) {
    setRestoringId(plant.id);
    try {
      await unarchivePlant(plant.id);
      await load();
      onRestored();
    } catch (err) {
      Alert.alert("Couldn't restore plant", String(err));
    } finally {
      setRestoringId(null);
    }
  }

  if (loadError && !plants) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Couldn't load archived plants.</Text>
        <Pressable style={styles.primaryButton} onPress={load}>
          <Text style={styles.primaryButtonText}>Try again</Text>
        </Pressable>
      </View>
    );
  }

  if (!plants) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Loading…</Text>
      </View>
    );
  }

  return (
    <FlatList
      contentContainerStyle={styles.list}
      refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} />}
      data={plants}
      keyExtractor={(p) => p.id}
      ListEmptyComponent={<Text style={styles.bodyText}>No archived plants.</Text>}
      renderItem={({ item }) => (
        <View style={styles.row}>
          <View style={{ flex: 1 }}>
            <Text style={styles.plantName}>{item.nickname || item.speciesName}</Text>
            <Text style={styles.plantSpecies}>{item.speciesName}</Text>
          </View>
          <Pressable
            style={styles.restoreButton}
            onPress={() => handleRestore(item)}
            disabled={restoringId === item.id}
          >
            <Text style={styles.restoreButtonText}>{restoringId === item.id ? "Restoring…" : "Restore"}</Text>
          </Pressable>
        </View>
      )}
    />
  );
}

const styles = StyleSheet.create({
  list: { backgroundColor: theme.color.cream, padding: theme.spacing(2), flexGrow: 1 },
  center: {
    flex: 1,
    alignItems: "center",
    justifyContent: "center",
    backgroundColor: theme.color.cream,
    padding: theme.spacing(4),
  },
  bodyText: { fontSize: theme.font.bodySize, color: theme.color.textSecondary, textAlign: "center" },
  primaryButton: {
    marginTop: theme.spacing(3),
    backgroundColor: theme.color.forestGreen,
    paddingHorizontal: theme.spacing(4),
    paddingVertical: theme.spacing(2),
    borderRadius: theme.radius.md,
  },
  primaryButtonText: { color: theme.color.cream, fontWeight: "600", fontSize: theme.font.bodySize },
  row: {
    flexDirection: "row",
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
  restoreButton: {
    paddingHorizontal: theme.spacing(2),
    paddingVertical: theme.spacing(1),
  },
  restoreButtonText: { color: theme.color.forestGreen, fontWeight: "600", fontSize: theme.font.captionSize },
});
