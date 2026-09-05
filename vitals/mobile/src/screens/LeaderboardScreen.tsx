import React, { useCallback, useEffect, useState } from "react";
import { StyleSheet, Switch, Text, View } from "react-native";
import { fetchGarden, fetchLeaderboard, setLeaderboardOptIn } from "../services/api";
import { scoreColor, theme } from "../theme/theme";
import { LeaderboardResult } from "../types/domain";

interface Props {
  gardenId: string;
}

/**
 * Opt-in neighborhood leaderboard (spec §4.6): friendly competition among
 * gardens in the same USDA zone, entirely anonymized — rank and percentile
 * only, never another garden's name or details. Opting in via the switch
 * below is the whole consent flow.
 */
export function LeaderboardScreen({ gardenId }: Props) {
  const [optedIn, setOptedIn] = useState(false);
  const [result, setResult] = useState<LeaderboardResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const garden = await fetchGarden(gardenId);
      setOptedIn(garden.leaderboardOptIn);
      setError(null);
      if (garden.leaderboardOptIn) {
        try {
          setResult(await fetchLeaderboard(gardenId));
        } catch (err) {
          setResult(null);
          setError(String(err));
        }
      } else {
        setResult(null);
      }
    } catch (err) {
      setError(String(err));
    } finally {
      setLoading(false);
    }
  }, [gardenId]);

  useEffect(() => {
    load();
  }, [load]);

  async function handleToggle(value: boolean) {
    setOptedIn(value); // optimistic
    try {
      await setLeaderboardOptIn(gardenId, value);
      await load();
    } catch (err) {
      setOptedIn(!value); // roll back on failure
      setError(String(err));
    }
  }

  return (
    <View style={styles.container}>
      <View style={styles.switchRow}>
        <View style={{ flex: 1 }}>
          <Text style={styles.switchLabel}>Join the neighborhood leaderboard</Text>
          <Text style={styles.switchCaption}>
            Anonymized — only your Garden Score and rank are shared, never your name or plants.
          </Text>
        </View>
        <Switch value={optedIn} onValueChange={handleToggle} />
      </View>

      {loading && <Text style={styles.bodyText}>Loading…</Text>}

      {!loading && optedIn && result && !error && (
        <View style={styles.card}>
          <Text style={[styles.rankScore, { color: scoreColor(result.percentile) }]}>#{result.rank}</Text>
          <Text style={styles.bodyText}>
            of {result.totalParticipants} garden{result.totalParticipants === 1 ? "" : "s"} in your zone
          </Text>
          <Text style={styles.captionText}>Scoring higher than {result.percentile}% of the group</Text>
        </View>
      )}

      {!loading && error && <Text style={styles.bodyText}>{error}</Text>}

      {!loading && !optedIn && !error && (
        <Text style={styles.bodyText}>Opt in above to see how your Garden Score compares nearby.</Text>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: theme.color.cream, padding: theme.spacing(3) },
  switchRow: {
    flexDirection: "row",
    alignItems: "center",
    backgroundColor: "white",
    borderRadius: theme.radius.md,
    padding: theme.spacing(2),
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: theme.color.border,
  },
  switchLabel: { fontSize: theme.font.bodySize, fontWeight: "600", color: theme.color.textPrimary },
  switchCaption: { fontSize: theme.font.captionSize, color: theme.color.textSecondary, marginTop: theme.spacing(0.5) },
  card: { alignItems: "center", marginTop: theme.spacing(4) },
  rankScore: { fontSize: theme.font.heroSize, fontWeight: "800" },
  bodyText: { fontSize: theme.font.bodySize, color: theme.color.textPrimary, textAlign: "center", marginTop: theme.spacing(3) },
  captionText: { fontSize: theme.font.captionSize, color: theme.color.textSecondary, marginTop: theme.spacing(1) },
});
