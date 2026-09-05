/**
 * Anonymized cross-garden comparisons (spec §4.4 "Twin plants near you",
 * §4.6 neighborhood leaderboard): a light-touch social layer using only
 * aggregate scores from other users' gardens/plants in the same climate
 * zone — never their names, photos, or garden details, and only from
 * gardens/plants that are active (twin comparison) or opted in
 * (leaderboard).
 */

export interface TwinComparisonResult {
  percentile: number; // 0-100: this plant scores higher than this % of its cohort
  cohortSize: number;
  message: string;
}

/** Percentile rank of `score` within `cohort`, 0-100. Ties count as "higher than". */
function percentileRank(score: number, cohort: number[]): number {
  if (cohort.length === 0) return 50; // no data to compare against — neutral default
  const lowerCount = cohort.filter((s) => s < score).length;
  return Math.round((lowerCount / cohort.length) * 100);
}

export function computeTwinComparison(myScore: number, cohortScores: number[]): TwinComparisonResult {
  if (cohortScores.length === 0) {
    return {
      percentile: 50,
      cohortSize: 0,
      message: "Not enough nearby plants of this species yet to compare.",
    };
  }

  const percentile = percentileRank(myScore, cohortScores);
  return {
    percentile,
    cohortSize: cohortScores.length,
    message: `Scoring higher than ${percentile}% of similar plants in your climate zone right now.`,
  };
}

export interface LeaderboardResult {
  rank: number; // 1-indexed, 1 is best
  totalParticipants: number;
  percentile: number;
}

export function computeLeaderboardRank(myScore: number, otherOptedInScores: number[]): LeaderboardResult {
  const allScores = [...otherOptedInScores, myScore].sort((a, b) => b - a);
  const rank = allScores.indexOf(myScore) + 1;
  const totalParticipants = allScores.length;
  const percentile = percentileRank(myScore, otherOptedInScores);

  return { rank, totalParticipants, percentile };
}
