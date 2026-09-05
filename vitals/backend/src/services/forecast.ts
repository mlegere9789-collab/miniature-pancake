/**
 * Lightweight trend forecasting (spec §4.3/§4.5: predictive, not just
 * reactive). A full ML forecasting model is Phase 4 scope; this is a simple
 * linear-regression projection over recent score history that's still
 * useful for surfacing "this plant is on track to need attention soon"
 * before it actually crosses the threshold.
 */
export interface ScoreSnapshotInput {
  score: number;
  computedAt: Date;
}

export interface ScoreForecast {
  /** Projected score `daysAhead` days from the most recent snapshot, clamped to [0, 100]. */
  projectedScore: number;
  daysAhead: number;
  trend: "improving" | "declining" | "flat";
  /** True when the projection crosses below the attention threshold but the current score hasn't yet. */
  approachingAttentionThreshold: boolean;
}

const ATTENTION_THRESHOLD = 55;
const FLAT_SLOPE_PER_DAY = 0.05;

/**
 * Ordinary least-squares fit of score against days-since-first-snapshot,
 * then projects `daysAhead` days past the most recent snapshot. Needs at
 * least 3 snapshots to produce a meaningful trend; returns null otherwise.
 */
export function computeScoreForecast(
  snapshots: ScoreSnapshotInput[],
  daysAhead = 14,
): ScoreForecast | null {
  if (snapshots.length < 3) return null;

  const sorted = [...snapshots].sort((a, b) => a.computedAt.getTime() - b.computedAt.getTime());
  const t0 = sorted[0].computedAt.getTime();
  const points = sorted.map((s) => ({
    x: (s.computedAt.getTime() - t0) / (1000 * 60 * 60 * 24),
    y: s.score,
  }));

  const n = points.length;
  const sumX = points.reduce((sum, p) => sum + p.x, 0);
  const sumY = points.reduce((sum, p) => sum + p.y, 0);
  const sumXY = points.reduce((sum, p) => sum + p.x * p.y, 0);
  const sumXX = points.reduce((sum, p) => sum + p.x * p.x, 0);

  const denominator = n * sumXX - sumX * sumX;
  // All snapshots at the same instant (denominator 0) — no meaningful trend to fit.
  const slope = denominator === 0 ? 0 : (n * sumXY - sumX * sumY) / denominator;
  const intercept = (sumY - slope * sumX) / n;

  const mostRecentX = points[points.length - 1].x;
  const currentScore = points[points.length - 1].y;
  const projectedX = mostRecentX + daysAhead;
  const projectedScore = Math.max(0, Math.min(100, Math.round(intercept + slope * projectedX)));

  const trend: ScoreForecast["trend"] =
    slope > FLAT_SLOPE_PER_DAY ? "improving" : slope < -FLAT_SLOPE_PER_DAY ? "declining" : "flat";

  const approachingAttentionThreshold =
    currentScore >= ATTENTION_THRESHOLD && projectedScore < ATTENTION_THRESHOLD;

  return { projectedScore, daysAhead, trend, approachingAttentionThreshold };
}
