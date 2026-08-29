import React, { useState } from "react";
import { Alert, Pressable, ScrollView, StyleSheet, Switch, Text, TextInput, View } from "react-native";
import { createPlant } from "../services/api";
import { theme } from "../theme/theme";
import { Plant } from "../types/domain";

interface Props {
  gardenId: string;
  onCreated: (plant: Plant) => void;
  onCancel: () => void;
}

/**
 * Manual plant entry (spec §4.1, Phase 1: "skip auto-segmentation for v1").
 * Auto-segmentation from a wide yard photo is Phase 3 scope.
 */
export function AddPlantScreen({ gardenId, onCreated, onCancel }: Props) {
  const [speciesName, setSpeciesName] = useState("");
  const [nickname, setNickname] = useState("");
  const [checkinCadenceDays, setCheckinCadenceDays] = useState("14");
  const [importanceWeight, setImportanceWeight] = useState("1");
  const [frostSensitive, setFrostSensitive] = useState(false);
  const [dormantInWinter, setDormantInWinter] = useState(false);
  const [submitting, setSubmitting] = useState(false);

  async function handleSubmit() {
    if (!speciesName.trim()) {
      Alert.alert("Species required", "Enter what kind of plant this is.");
      return;
    }
    setSubmitting(true);
    try {
      const plant = await createPlant({
        gardenId,
        speciesId: speciesName.trim().toLowerCase().replace(/\s+/g, "-"),
        speciesName: speciesName.trim(),
        nickname: nickname.trim() || undefined,
        checkinCadenceDays: Number(checkinCadenceDays) || 14,
        importanceWeight: Number(importanceWeight) || 1,
        frostSensitive,
        // Nov-Feb preset for now (spec §4.7); a real per-species/hemisphere
        // dormancy calendar is further Phase 3 work.
        dormancyMonths: dormantInWinter ? [11, 12, 1, 2] : [],
      });
      onCreated(plant);
    } catch (err) {
      Alert.alert("Couldn't add plant", String(err));
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <ScrollView contentContainerStyle={styles.container}>
      <Text style={styles.title}>Add a plant</Text>

      <Field label="Species (e.g. Tomato, Japanese Maple)" value={speciesName} onChangeText={setSpeciesName} />
      <Field label="Nickname (optional)" value={nickname} onChangeText={setNickname} />
      <Field
        label="Check-in cadence (days)"
        value={checkinCadenceDays}
        onChangeText={setCheckinCadenceDays}
        keyboardType="number-pad"
      />
      <Field
        label="Importance (0–10, how much this plant matters to your Garden Score)"
        value={importanceWeight}
        onChangeText={setImportanceWeight}
        keyboardType="decimal-pad"
      />

      <View style={styles.switchRow}>
        <Text style={[styles.label, { flex: 1, marginBottom: 0 }]}>
          Frost-sensitive (gets a frost-warning alert)
        </Text>
        <Switch value={frostSensitive} onValueChange={setFrostSensitive} />
      </View>

      <View style={styles.switchRow}>
        <Text style={[styles.label, { flex: 1, marginBottom: 0 }]}>
          Dormant in winter (e.g. deciduous trees — won't be scored as declining for expected leaf drop)
        </Text>
        <Switch value={dormantInWinter} onValueChange={setDormantInWinter} />
      </View>

      <Pressable style={styles.primaryButton} onPress={handleSubmit} disabled={submitting}>
        <Text style={styles.primaryButtonText}>{submitting ? "Adding…" : "Add plant"}</Text>
      </Pressable>
      <Pressable style={styles.secondaryButton} onPress={onCancel}>
        <Text style={styles.secondaryButtonText}>Cancel</Text>
      </Pressable>
    </ScrollView>
  );
}

function Field(props: {
  label: string;
  value: string;
  onChangeText: (v: string) => void;
  keyboardType?: "default" | "number-pad" | "decimal-pad";
}) {
  return (
    <View style={styles.field}>
      <Text style={styles.label}>{props.label}</Text>
      <TextInput
        style={styles.input}
        value={props.value}
        onChangeText={props.onChangeText}
        keyboardType={props.keyboardType ?? "default"}
        placeholderTextColor={theme.color.textSecondary}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { padding: theme.spacing(3), backgroundColor: theme.color.cream, flexGrow: 1 },
  title: { fontSize: theme.font.titleSize, fontWeight: "700", color: theme.color.textPrimary, marginBottom: theme.spacing(3) },
  field: { marginBottom: theme.spacing(2) },
  switchRow: {
    flexDirection: "row",
    justifyContent: "space-between",
    alignItems: "center",
    marginBottom: theme.spacing(2),
  },
  label: { fontSize: theme.font.captionSize, color: theme.color.textSecondary, marginBottom: theme.spacing(1) },
  input: {
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: theme.color.border,
    borderRadius: theme.radius.sm,
    paddingHorizontal: theme.spacing(2),
    paddingVertical: theme.spacing(1.5),
    fontSize: theme.font.bodySize,
    backgroundColor: "white",
    color: theme.color.textPrimary,
  },
  primaryButton: {
    marginTop: theme.spacing(2),
    backgroundColor: theme.color.forestGreen,
    paddingVertical: theme.spacing(2),
    borderRadius: theme.radius.md,
    alignItems: "center",
  },
  primaryButtonText: { color: theme.color.cream, fontWeight: "600", fontSize: theme.font.bodySize },
  secondaryButton: { marginTop: theme.spacing(2), paddingVertical: theme.spacing(1), alignItems: "center" },
  secondaryButtonText: { color: theme.color.textSecondary, fontSize: theme.font.bodySize },
});
