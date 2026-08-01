/**
 * Shared report data assembly.
 *
 * A single date-range query pulls symptom logs, cycle notes and the current
 * staging result into one structured object. Both the Trends screen and the
 * Doctor Report render from this shape, so what the user previews on-screen is
 * exactly what the exported PDF contains — no divergent code paths.
 */

import { STAGE_INFO } from '@/constants/staging';
import { SYMPTOM_BY_ID } from '@/constants/symptoms';
import { toLocalDateKey } from '@/utils/date';
import { cycleNotesRepository } from './repositories/cycleNotes';
import { stagingAssessmentRepository } from './repositories/stagingAssessment';
import { symptomLogsRepository } from './repositories/symptomLogs';
import type { CycleNote, DailyCategorySeverity, StagingAssessment, SymptomLog } from './types';

export interface CategorySummary {
  category: string;
  label: string;
  emoji: string;
  /** Number of days the category was logged in the range. */
  daysLogged: number;
  /** Total number of logs. */
  totalLogs: number;
  /** Mean severity across all logs in the range. */
  avgSeverity: number;
  /** Highest single severity recorded. */
  maxSeverity: number;
}

export interface ReportData {
  startIso: string;
  endIso: string;
  startDateKey: string;
  endDateKey: string;
  generatedAt: string;
  staging: StagingAssessment | null;
  stagingLabel: string | null;
  logs: SymptomLog[];
  cycleNotes: CycleNote[];
  daily: DailyCategorySeverity[];
  categorySummaries: CategorySummary[];
  totalLogs: number;
}

export async function buildReportData(
  startIso: string,
  endIso: string,
  generatedAt: string,
): Promise<ReportData> {
  const startDateKey = toLocalDateKey(startIso);
  const endDateKey = toLocalDateKey(endIso);

  const [logs, daily, cycleNotes, staging] = await Promise.all([
    symptomLogsRepository.getInRange(startIso, endIso),
    symptomLogsRepository.getDailyCategorySeverity(startIso, endIso),
    cycleNotesRepository.getInRange(startDateKey, endDateKey),
    stagingAssessmentRepository.getLatest(),
  ]);

  const categorySummaries = summarizeByCategory(logs);

  return {
    startIso,
    endIso,
    startDateKey,
    endDateKey,
    generatedAt,
    staging,
    stagingLabel: staging ? STAGE_INFO[staging.stage].title : null,
    logs,
    cycleNotes,
    daily,
    categorySummaries,
    totalLogs: logs.length,
  };
}

function summarizeByCategory(logs: SymptomLog[]): CategorySummary[] {
  const byCat = new Map<string, { logs: SymptomLog[]; days: Set<string> }>();
  for (const log of logs) {
    const entry = byCat.get(log.category) ?? { logs: [], days: new Set<string>() };
    entry.logs.push(log);
    entry.days.add(toLocalDateKey(log.loggedAt));
    byCat.set(log.category, entry);
  }

  const summaries: CategorySummary[] = [];
  for (const [category, entry] of byCat) {
    const meta = SYMPTOM_BY_ID[category];
    const severities = entry.logs.map((l) => l.severity);
    const total = severities.reduce((a, b) => a + b, 0);
    summaries.push({
      category,
      label: meta?.label ?? category,
      emoji: meta?.emoji ?? '•',
      daysLogged: entry.days.size,
      totalLogs: entry.logs.length,
      avgSeverity: total / entry.logs.length,
      maxSeverity: Math.max(...severities),
    });
  }
  // Most-burdensome first: highest average severity, then most frequent.
  summaries.sort((a, b) => b.avgSeverity - a.avgSeverity || b.totalLogs - a.totalLogs);
  return summaries;
}
