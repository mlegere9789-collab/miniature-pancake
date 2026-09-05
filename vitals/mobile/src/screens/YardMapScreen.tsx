import { useFocusEffect } from "@react-navigation/native";
import * as ImagePicker from "expo-image-picker";
import React, { useCallback, useState } from "react";
import { Alert, Image, Pressable, RefreshControl, ScrollView, StyleSheet, Text, View } from "react-native";
import { fetchGarden, setPlantLocation, setYardMapPhoto, toAbsoluteUrl, uploadCheckInPhoto } from "../services/api";
import { scoreColor, theme } from "../theme/theme";
import { Garden, Plant } from "../types/domain";

interface Props {
  gardenId: string;
  onSelectPlant: (plant: Plant) => void;
}

const MAP_HEIGHT = 320;

/**
 * Yard map (spec §4.1): plants pinned onto a wide photo of the yard so you
 * can see at a glance where everything is and how it's doing, rather than
 * just a flat list. `Plant.locationPin` stores relative (0-1) x/y
 * coordinates so a pin stays correctly placed regardless of the image's
 * displayed size.
 */
export function YardMapScreen({ gardenId, onSelectPlant }: Props) {
  const [garden, setGarden] = useState<Garden | null>(null);
  const [loadError, setLoadError] = useState(false);
  const [placingPlantId, setPlacingPlantId] = useState<string | null>(null);
  const [mapWidth, setMapWidth] = useState(0);
  const [busy, setBusy] = useState(false);
  const [refreshing, setRefreshing] = useState(false);

  const load = useCallback(async () => {
    try {
      setGarden(await fetchGarden(gardenId));
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

  // Reload on every focus, not just first mount — returning here from a
  // plant's detail screen (e.g. after archiving it) reuses this same
  // mounted instance, so a mount-only load would show stale data.
  useFocusEffect(
    useCallback(() => {
      load();
    }, [load]),
  );

  async function handlePickYardPhoto() {
    const permission = await ImagePicker.requestMediaLibraryPermissionsAsync();
    if (!permission.granted) {
      Alert.alert("Photo access needed", "Vitals needs photo library access to set your yard map.");
      return;
    }
    const picked = await ImagePicker.launchImageLibraryAsync({ mediaTypes: ImagePicker.MediaTypeOptions.Images, quality: 0.85 });
    if (picked.canceled || !picked.assets[0]) return;

    setBusy(true);
    try {
      const photoUrl = await uploadCheckInPhoto(picked.assets[0].uri);
      await setYardMapPhoto(gardenId, photoUrl);
      await load();
    } catch (err) {
      Alert.alert("Couldn't set yard map", String(err));
    } finally {
      setBusy(false);
    }
  }

  async function handleMapTap(evt: { nativeEvent: { locationX: number; locationY: number } }) {
    if (!placingPlantId || mapWidth === 0) return;
    const { locationX, locationY } = evt.nativeEvent;
    const x = Math.max(0, Math.min(1, locationX / mapWidth));
    const y = Math.max(0, Math.min(1, locationY / MAP_HEIGHT));

    setBusy(true);
    try {
      await setPlantLocation(placingPlantId, { x, y });
      setPlacingPlantId(null);
      await load();
    } catch (err) {
      Alert.alert("Couldn't place pin", String(err));
    } finally {
      setBusy(false);
    }
  }

  if (loadError && !garden) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Couldn't load your yard map.</Text>
        <Pressable style={styles.primaryButton} onPress={load}>
          <Text style={styles.primaryButtonText}>Try again</Text>
        </Pressable>
      </View>
    );
  }

  if (!garden) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Loading your yard map…</Text>
      </View>
    );
  }

  if (!garden.yardMapPhotoUrl) {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Add a wide photo of your yard to start pinning plants.</Text>
        <Pressable style={styles.primaryButton} onPress={handlePickYardPhoto} disabled={busy}>
          <Text style={styles.primaryButtonText}>{busy ? "Uploading…" : "Choose a yard photo"}</Text>
        </Pressable>
      </View>
    );
  }

  const placedPlants = garden.plants.filter((p) => p.locationPin);
  const unplacedPlants = garden.plants.filter((p) => !p.locationPin);
  const mapUri = toAbsoluteUrl(garden.yardMapPhotoUrl);

  return (
    <ScrollView
      contentContainerStyle={styles.container}
      refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} />}
    >
      {placingPlantId && (
        <View style={styles.placingBanner}>
          <Text style={styles.placingBannerText}>Tap the map where this plant is</Text>
        </View>
      )}
      {!placingPlantId && placedPlants.length > 0 && (
        <Text style={styles.hintText}>Hold a pin to move it</Text>
      )}

      <Pressable
        style={styles.mapWrap}
        onPress={handleMapTap}
        onLayout={(e) => setMapWidth(e.nativeEvent.layout.width)}
        disabled={!placingPlantId}
      >
        {mapUri && <Image source={{ uri: mapUri }} style={[styles.mapImage, { height: MAP_HEIGHT }]} resizeMode="cover" />}
        {mapWidth > 0 &&
          placedPlants.map((plant) => (
            <Pressable
              key={plant.id}
              style={[
                styles.pin,
                {
                  left: plant.locationPin!.x * mapWidth - 10,
                  top: plant.locationPin!.y * MAP_HEIGHT - 10,
                  backgroundColor: scoreColor(plant.scoreCurrent),
                },
              ]}
              onPress={() => onSelectPlant(plant)}
              onLongPress={() => setPlacingPlantId(plant.id)}
              accessibilityLabel={`${plant.nickname || plant.speciesName}, score ${Math.round(plant.scoreCurrent)}`}
              accessibilityRole="button"
              accessibilityHint="Double tap to view. Long-press to move this pin."
            />
          ))}
      </Pressable>

      <Pressable style={styles.secondaryButton} onPress={handlePickYardPhoto} disabled={busy}>
        <Text style={styles.secondaryButtonText}>{busy ? "Uploading…" : "Replace yard photo"}</Text>
      </Pressable>

      {unplacedPlants.length > 0 && (
        <View style={styles.section}>
          <Text style={styles.sectionTitle}>Not yet placed</Text>
          {unplacedPlants.map((plant) => (
            <Pressable
              key={plant.id}
              style={[styles.plantRow, placingPlantId === plant.id && styles.plantRowActive]}
              onPress={() => setPlacingPlantId(placingPlantId === plant.id ? null : plant.id)}
              disabled={busy}
            >
              <Text style={styles.plantRowText}>{plant.nickname || plant.speciesName}</Text>
              <Text style={styles.plantRowHint}>{placingPlantId === plant.id ? "Tap the map above" : "Tap to place"}</Text>
            </Pressable>
          ))}
        </View>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { padding: theme.spacing(3), backgroundColor: theme.color.cream, flexGrow: 1 },
  center: { flex: 1, alignItems: "center", justifyContent: "center", backgroundColor: theme.color.cream, padding: theme.spacing(3) },
  bodyText: { fontSize: theme.font.bodySize, color: theme.color.textSecondary, textAlign: "center" },
  placingBanner: {
    backgroundColor: theme.color.forestGreen,
    borderRadius: theme.radius.sm,
    padding: theme.spacing(1.5),
    marginBottom: theme.spacing(1),
  },
  placingBannerText: { color: theme.color.cream, fontSize: theme.font.captionSize, textAlign: "center" },
  hintText: {
    fontSize: theme.font.captionSize,
    color: theme.color.textSecondary,
    textAlign: "center",
    marginBottom: theme.spacing(1),
  },
  mapWrap: { width: "100%", height: MAP_HEIGHT, borderRadius: theme.radius.md, overflow: "hidden", backgroundColor: "#000" },
  mapImage: { width: "100%" },
  pin: {
    position: "absolute",
    width: 20,
    height: 20,
    borderRadius: 10,
    borderWidth: 2,
    borderColor: "white",
  },
  primaryButton: {
    marginTop: theme.spacing(3),
    backgroundColor: theme.color.forestGreen,
    paddingHorizontal: theme.spacing(4),
    paddingVertical: theme.spacing(2),
    borderRadius: theme.radius.md,
  },
  primaryButtonText: { color: theme.color.cream, fontWeight: "600", fontSize: theme.font.bodySize },
  secondaryButton: { marginTop: theme.spacing(2), paddingVertical: theme.spacing(1), alignItems: "center" },
  secondaryButtonText: { color: theme.color.textSecondary, fontSize: theme.font.captionSize },
  section: { marginTop: theme.spacing(3) },
  sectionTitle: { fontSize: theme.font.titleSize, fontWeight: "700", color: theme.color.textPrimary, marginBottom: theme.spacing(1) },
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
  plantRowActive: { borderColor: theme.color.forestGreen, borderWidth: 1.5 },
  plantRowText: { fontSize: theme.font.bodySize, fontWeight: "600", color: theme.color.textPrimary },
  plantRowHint: { fontSize: theme.font.captionSize, color: theme.color.textSecondary },
});
