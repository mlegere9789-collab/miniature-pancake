/**
 * Pure dashboard aggregation logic (spec §4.3), split out of routes/gardens.ts
 * so it's unit-testable without a database.
 */

export interface RisingStar {
  plantId: string;
  delta: number;
}

export interface PlantRecentHistory {
  plantId: string;
  /** Up to the last 3 PlantScoreSnapshot scores, most recent first. */
  recentScores: number[];
}

/**
 * "Rising Stars" (spec §4.3): plants trending up over their last few
 * check-ins, for positive reinforcement — not just a "Needs Attention" list.
 * Requires at least 2 snapshots to have a trend at all.
 */
export function computeRisingStars(histories: PlantRecentHistory[], limit = 5): RisingStar[] {
  const rising: RisingStar[] = [];

  for (const { plantId, recentScores } of histories) {
    if (recentScores.length < 2) continue;
    const mostRecent = recentScores[0];
    const oldest = recentScores[recentScores.length - 1];
    if (mostRecent > oldest) {
      rising.push({ plantId, delta: mostRecent - oldest });
    }
  }

  return rising.sort((a, b) => b.delta - a.delta).slice(0, limit);
}
