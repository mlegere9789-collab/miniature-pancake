/**
 * Domain types for AfterNoon's local-first data model.
 *
 * These are the shapes the rest of the app consumes. They deliberately do NOT
 * leak SQLite specifics (row objects, column casing) — the repositories map
 * between raw rows and these types so the storage engine stays swappable.
 */

import type { MenopauseStage } from '@/constants/staging';

export type Severity = 1 | 2 | 3 | 4 | 5;

/** A single symptom entry from a daily check-in. */
export interface SymptomLog {
  id: number;
  category: string;
  severity: Severity;
  note: string | null;
  /** ISO-8601 instant the symptom is logged *for*. */
  loggedAt: string;
  /** ISO-8601 instant the row was written. */
  createdAt: string;
}

export interface NewSymptomLog {
  category: string;
  severity: Severity;
  note?: string | null;
  /** Defaults to "now" when omitted. */
  loggedAt?: string;
}

/** Result of the onboarding staging self-assessment. */
export interface StagingAssessment {
  id: number;
  stage: MenopauseStage;
  score: number;
  /** Map of questionId -> chosen point value. */
  answers: Record<string, number>;
  assessedAt: string;
}

export interface NewStagingAssessment {
  stage: MenopauseStage;
  score: number;
  answers: Record<string, number>;
  assessedAt?: string;
}

export type FlowLevel = 'none' | 'spotting' | 'light' | 'medium' | 'heavy';

/** A free-form cycle note for a given day. */
export interface CycleNote {
  id: number;
  noteDate: string; // YYYY-MM-DD
  flow: FlowLevel | null;
  note: string | null;
  createdAt: string;
}

export interface NewCycleNote {
  noteDate: string;
  flow?: FlowLevel | null;
  note?: string | null;
}

/**
 * Aggregated slice used by both Trends and the Doctor Report. Building this
 * once from a date-range query keeps the two features in lockstep.
 */
export interface DailyCategorySeverity {
  date: string; // YYYY-MM-DD
  category: string;
  /** Average severity for that category on that day. */
  avgSeverity: number;
  /** Number of logs contributing to the average. */
  count: number;
}
