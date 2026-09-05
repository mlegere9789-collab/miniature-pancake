// Shape returned by the shared Plant ID & Diagnostic Engine (Idea 5).
// Phase 1 uses a stubbed rules-based implementation (see
// services/diagnosticEngine.ts) that returns this same contract, so the
// scoring service and API never need to change when the real engine lands.

export type FlagSeverity = "COSMETIC" | "MODERATE" | "URGENT";
export type FlagUrgency = "MONITOR" | "THIS_WEEK" | "TREAT_TODAY";
export type FlagTrend = "NEW" | "WORSENING" | "STABLE" | "RESOLVING";

export interface DiagnosticFlagResult {
  condition: string;
  confidence: number; // 0-1
  severity: FlagSeverity;
  urgency: FlagUrgency;
  trend: FlagTrend;
}

export interface VisualVitalityResult {
  /** 0-100, higher is healthier */
  score: number;
  chlorosisPct: number;
  necroticCoveragePct: number;
  wiltingDetected: boolean;
  newGrowthDetected: boolean;
  growthDensityDeltaPct: number; // vs. previous check-in photo
}

export interface EnvironmentalFitResult {
  /** 0-100, higher is a better match to the plant's ideal profile */
  score: number;
  lightExposureMatch: boolean;
  droughtStressDetected: boolean;
  frostRiskDetected: boolean;
}

export interface DiagnosticEngineOutput {
  visualVitality: VisualVitalityResult;
  flags: DiagnosticFlagResult[];
  environmentalFit: EnvironmentalFitResult;
}
