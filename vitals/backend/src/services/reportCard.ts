/**
 * Weekly "Garden Report Card" (spec §4.5): a re-engagement push/email lever,
 * similar to Spotify Wrapped mechanics but weekly. Pure aggregation function
 * so it's easy to unit test independently of the DB queries that assemble
 * its input (see routes/gardens.ts).
 */

export interface ReportCardPlantSummary {
  plantId: string;
  name: string;
  scoreEnd: number;
  delta: number;
}

export interface WeeklyReportCard {
  periodStart: string;
  periodEnd: string;
  gardenScoreStart: number;
  gardenScoreEnd: number;
  gardenScoreDelta: number;
  topPlants: ReportCardPlantSummary[];
  plantsNeedingAttention: ReportCardPlantSummary[];
  checkInsCompleted: number;
  headline: string;
}

export interface PlantWeeklySummaryInput {
  plantId: string;
  name: string;
  scoreStart: number;
  scoreEnd: number;
}

export interface ComputeWeeklyReportCardInput {
  periodStart: Date;
  periodEnd: Date;
  gardenScoreStart: number;
  gardenScoreEnd: number;
  plantSummaries: PlantWeeklySummaryInput[];
  checkInsCompleted: number;
}

export function computeWeeklyReportCard(input: ComputeWeeklyReportCardInput): WeeklyReportCard {
  const gardenScoreDelta = Math.round(input.gardenScoreEnd - input.gardenScoreStart);

  const summaries: ReportCardPlantSummary[] = input.plantSummaries.map((p) => ({
    plantId: p.plantId,
    name: p.name,
    scoreEnd: p.scoreEnd,
    delta: Math.round(p.scoreEnd - p.scoreStart),
  }));

  const topPlants = [...summaries]
    .filter((p) => p.delta > 0)
    .sort((a, b) => b.delta - a.delta)
    .slice(0, 3);

  const plantsNeedingAttention = [...summaries]
    .filter((p) => p.delta < 0 || p.scoreEnd < 60)
    .sort((a, b) => a.scoreEnd - b.scoreEnd)
    .slice(0, 3);

  return {
    periodStart: input.periodStart.toISOString(),
    periodEnd: input.periodEnd.toISOString(),
    gardenScoreStart: Math.round(input.gardenScoreStart),
    gardenScoreEnd: Math.round(input.gardenScoreEnd),
    gardenScoreDelta,
    topPlants,
    plantsNeedingAttention,
    checkInsCompleted: input.checkInsCompleted,
    headline: buildHeadline(gardenScoreDelta, input.checkInsCompleted, topPlants),
  };
}

function buildHeadline(delta: number, checkInsCompleted: number, topPlants: ReportCardPlantSummary[]): string {
  if (checkInsCompleted === 0) {
    return "No check-ins this week — your garden missed you.";
  }
  if (delta > 0) {
    const star = topPlants[0];
    return star
      ? `Garden Score up ${delta} pts this week, led by ${star.name} (+${star.delta}).`
      : `Garden Score up ${delta} pts this week.`;
  }
  if (delta < 0) {
    return `Garden Score down ${Math.abs(delta)} pts this week — check "Needs Attention" for what's driving it.`;
  }
  return "Garden Score held steady this week.";
}
