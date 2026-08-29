import React from "react";
import { StyleSheet, Text, View } from "react-native";
import { theme } from "../theme/theme";

interface Props {
  priorScore: number | null;
  newScore: number;
}

/** "Score dropped 6 pts — ..." style summary shown right after a check-in (spec §4.2). */
export function ScoreDeltaBadge({ priorScore, newScore }: Props) {
  if (priorScore === null) {
    return (
      <View style={[styles.badge, { backgroundColor: theme.color.forestGreenLight }]}>
        <Text style={styles.text}>First check-in logged</Text>
      </View>
    );
  }

  const delta = newScore - priorScore;
  const direction = delta > 0 ? "up" : delta < 0 ? "down" : "unchanged";
  const color =
    direction === "up" ? theme.color.forestGreenLight : direction === "down" ? theme.color.danger : theme.color.gold;
  const arrow = direction === "up" ? "▲" : direction === "down" ? "▼" : "—";

  return (
    <View style={[styles.badge, { backgroundColor: color }]}>
      <Text style={styles.text}>
        {arrow} {Math.abs(delta)} pt{Math.abs(delta) === 1 ? "" : "s"} {direction === "unchanged" ? "" : direction}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  badge: {
    alignSelf: "flex-start",
    paddingHorizontal: theme.spacing(2),
    paddingVertical: theme.spacing(1),
    borderRadius: theme.radius.sm,
  },
  text: {
    color: theme.color.cream,
    fontWeight: "600",
    fontSize: theme.font.captionSize,
  },
});
