import React from "react";
import { StyleSheet, View } from "react-native";
import { theme } from "../theme/theme";

interface Props {
  values: number[];
  height?: number;
  color?: string;
}

/**
 * Minimal bar-style trend sparkline with no external chart/SVG dependency —
 * good enough for the 90-day Garden Score trend and per-plant score history
 * (spec §4.3/§4.4) until a richer zoomable chart is worth the extra dep.
 */
export function Sparkline({ values, height = 48, color = theme.color.forestGreenLight }: Props) {
  if (values.length === 0) {
    return <View style={{ height }} />;
  }

  const min = Math.min(...values);
  const max = Math.max(...values);
  const range = max - min || 1;

  return (
    <View style={[styles.container, { height }]}>
      {values.map((v, i) => {
        const barHeight = Math.max(2, ((v - min) / range) * height);
        return <View key={i} style={[styles.bar, { height: barHeight, backgroundColor: color }]} />;
      })}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flexDirection: "row",
    alignItems: "flex-end",
    gap: 2,
  },
  bar: {
    flex: 1,
    borderRadius: 2,
    minWidth: 2,
  },
});
