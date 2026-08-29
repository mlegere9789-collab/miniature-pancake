// Mirrors vitals/backend/prisma/schema.prisma — kept in sync manually for
// Phase 1. Once the API is stable, generate these from an OpenAPI schema.

export interface Plant {
  id: string;
  gardenId: string;
  speciesId: string;
  speciesName: string;
  nickname: string | null;
  plantedDate: string | null;
  importanceWeight: number;
  checkinCadenceDays: number;
  frostSensitive: boolean;
  scoreCurrent: number;
  createdAt: string;
}

export interface WeatherAlert {
  type: "frost";
  minTempTonightC: number;
  affectedPlantIds: string[];
}

export interface PlantScoreSnapshot {
  id: string;
  score: number;
  computedAt: string;
}

export type FlagSeverity = "COSMETIC" | "MODERATE" | "URGENT";
export type FlagUrgency = "MONITOR" | "THIS_WEEK" | "TREAT_TODAY";
export type FlagStatus = "OPEN" | "MONITORING" | "RESOLVED";

export interface DiagnosticFlag {
  id: string;
  condition: string;
  confidence: number;
  severity: FlagSeverity;
  urgency: FlagUrgency;
  status: FlagStatus;
}

export interface CheckIn {
  id: string;
  plantId: string;
  timestamp: string;
  photoUrl: string;
  computedScore: number;
  subscoreBreakdownJson: {
    visualVitality: number;
    diagnosticFlags: number;
    environmentalFit: number;
    careConsistency: number;
    trendMomentum: number;
  };
  diagnosticFlags: DiagnosticFlag[];
}

export interface CreateCheckInResponse {
  checkIn: CheckIn;
  priorScore: number | null;
}

export interface Garden {
  id: string;
  name: string;
  scoreCurrent: number;
  plants: Plant[];
  scoreHistory: PlantScoreSnapshot[];
  needsAttention: Plant[];
  risingStars: { plantId: string; delta: number }[];
  weatherAlert: WeatherAlert | null;
}

export interface PlantDetail extends Plant {
  scoreHistory: PlantScoreSnapshot[];
  checkIns: CheckIn[];
}

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

export interface CreatePlantInput {
  gardenId: string;
  speciesId: string;
  speciesName: string;
  nickname?: string;
  importanceWeight?: number;
  checkinCadenceDays?: number;
  frostSensitive?: boolean;
}
