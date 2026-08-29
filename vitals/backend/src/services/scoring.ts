import { DiagnosticEngineOutput, FlagSeverity, FlagUrgency } from "../types/diagnosticEngine";

/**
 * Weighted composite plant scoring per spec §3.1.
 *   - Visual vitality signal      40%
 *   - Diagnostic flags            30%
 *   - Environmental fit           15%
 *   - Care consistency            10%
 *   - Trend momentum              modifier, ±10, not part of the 100% base
 */
export const SCORE_WEIGHTS = {
  visualVitality: 0.4,
  diagnosticFlags: 0.3,
  environmentalFit: 0.15,
  careConsistency: 0.1,
} as const;

export interface SubscoreBreakdown {
  visualVitality: number;
  diagnosticFlags: number;
  environmentalFit: number;
  careConsistency: number;
  trendMomentum: number; // signed modifier actually applied, already clamped to [-10, 10]
}

export interface ComputeScoreInput {
  engineOutput: DiagnosticEngineOutput;
  /** Days since the previous check-in was due; <=0 means on-time or early. */
  daysLateForCheckin: number;
  /** Were any open treatment plans for this plant marked completed since the last check-in? */
  outstandingTreatmentsIgnored: boolean;
  /** Composite scores (0-100) from the plant's last up-to-3 check-ins, oldest first. */
  recentScores: number[];
  /** Is this plant naturally dormant right now (spec §4.7)? Suppresses the visual-vitality dip from expected leaf drop/browning. */
  isDormant?: boolean;
}

const DORMANCY_VISUAL_VITALITY_FLOOR = 70;

/**
 * Seasonal recalibration (spec §4.7): a deciduous plant losing leaves in its
 * dormant season is healthy for that time of year, not declining. `months`
 * is 1-12 (January = 1); `now` defaults to the current date.
 */
export function isSeasonallyDormant(months: number[], now: Date = new Date()): boolean {
  return months.includes(now.getMonth() + 1);
}

const FLAG_SEVERITY_PENALTY: Record<FlagSeverity, number> = {
  COSMETIC: 5,
  MODERATE: 15,
  URGENT: 30,
};

const FLAG_URGENCY_MULTIPLIER: Record<FlagUrgency, number> = {
  MONITOR: 0.5,
  THIS_WEEK: 0.8,
  TREAT_TODAY: 1.0,
};

function scoreDiagnosticFlags(engineOutput: DiagnosticEngineOutput): number {
  if (engineOutput.flags.length === 0) return 100;

  const totalPenalty = engineOutput.flags.reduce((sum, flag) => {
    const base = FLAG_SEVERITY_PENALTY[flag.severity] * FLAG_URGENCY_MULTIPLIER[flag.urgency];
    const trendFactor = flag.trend === "WORSENING" ? 1.3 : flag.trend === "RESOLVING" ? 0.6 : 1.0;
    return sum + base * trendFactor * flag.confidence;
  }, 0);

  return Math.max(0, 100 - totalPenalty);
}

function scoreCareConsistency(daysLateForCheckin: number, outstandingTreatmentsIgnored: boolean): number {
  let score = 100;
  if (daysLateForCheckin > 0) {
    // Lose 2 points per day late, capped at 40.
    score -= Math.min(40, daysLateForCheckin * 2);
  }
  if (outstandingTreatmentsIgnored) {
    score -= 20;
  }
  return Math.max(0, score);
}

/**
 * Trend momentum per spec §3.1.5: rate of change over the last 3 check-ins,
 * expressed as a signed modifier capped at ±10 and applied on top of the
 * weighted base score (not blended into the 100% weighting).
 */
function computeTrendMomentum(recentScores: number[], newBaseScore: number): number {
  const history = [...recentScores, newBaseScore];
  if (history.length < 2) return 0;

  const first = history[0];
  const last = history[history.length - 1];
  const delta = last - first;

  // Scale so a full swing across 3 check-ins maps to roughly ±10.
  const modifier = (delta / 100) * 30;
  return Math.max(-10, Math.min(10, Math.round(modifier)));
}

export function computePlantScore(input: ComputeScoreInput): {
  finalScore: number;
  breakdown: SubscoreBreakdown;
} {
  const visualVitality = input.isDormant
    ? Math.max(input.engineOutput.visualVitality.score, DORMANCY_VISUAL_VITALITY_FLOOR)
    : input.engineOutput.visualVitality.score;
  const diagnosticFlags = scoreDiagnosticFlags(input.engineOutput);
  const environmentalFit = input.engineOutput.environmentalFit.score;
  const careConsistency = scoreCareConsistency(input.daysLateForCheckin, input.outstandingTreatmentsIgnored);

  const baseScore =
    visualVitality * SCORE_WEIGHTS.visualVitality +
    diagnosticFlags * SCORE_WEIGHTS.diagnosticFlags +
    environmentalFit * SCORE_WEIGHTS.environmentalFit +
    careConsistency * SCORE_WEIGHTS.careConsistency;

  const trendMomentum = computeTrendMomentum(input.recentScores, baseScore);
  const finalScore = Math.max(0, Math.min(100, Math.round(baseScore + trendMomentum)));

  return {
    finalScore,
    breakdown: {
      visualVitality,
      diagnosticFlags,
      environmentalFit,
      careConsistency,
      trendMomentum,
    },
  };
}

/**
 * Garden Score roll-up per spec §3.2: importance-weighted average of active
 * plant scores, with a neglect penalty for overdue plants and a small
 * diversity bonus for species variety. Regional outbreak exposure (Idea 4)
 * is intentionally left as a documented extension point for Phase 3.
 */
export interface GardenPlantInput {
  score: number;
  importanceWeight: number;
  speciesId: string;
  isOverdueForCheckin: boolean;
}

export function computeGardenScore(plants: GardenPlantInput[]): number {
  if (plants.length === 0) return 100;

  const totalWeight = plants.reduce((sum, p) => sum + p.importanceWeight, 0);
  const weightedAverage =
    plants.reduce((sum, p) => sum + p.score * p.importanceWeight, 0) / totalWeight;

  const overdueCount = plants.filter((p) => p.isOverdueForCheckin).length;
  const neglectPenalty = Math.min(15, overdueCount * 3);

  const uniqueSpecies = new Set(plants.map((p) => p.speciesId)).size;
  const diversityRatio = uniqueSpecies / plants.length;
  const diversityBonus = diversityRatio > 0.5 ? 3 : 0;

  return Math.max(0, Math.min(100, Math.round(weightedAverage - neglectPenalty + diversityBonus)));
}
