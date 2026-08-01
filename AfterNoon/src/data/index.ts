/**
 * Data-access facade.
 *
 * The rest of the app imports `dataAccess` from here and never reaches into
 * `db.ts` or individual repositories. This is the seam that keeps storage
 * details — and, later, an opt-in sync module — isolated from feature code.
 */

import { getDb, resetAllData } from './db';
import { cycleNotesRepository } from './repositories/cycleNotes';
import { settingsRepository } from './repositories/settings';
import { stagingAssessmentRepository } from './repositories/stagingAssessment';
import { symptomLogsRepository } from './repositories/symptomLogs';

export const dataAccess = {
  symptomLogs: symptomLogsRepository,
  staging: stagingAssessmentRepository,
  cycleNotes: cycleNotesRepository,
  settings: settingsRepository,

  /** Force schema/migrations to run before first use (called at app boot). */
  async init(): Promise<void> {
    await getDb();
  },

  resetAllData,
};

export type DataAccess = typeof dataAccess;

export * from './types';
export { buildReportData } from './reportData';
