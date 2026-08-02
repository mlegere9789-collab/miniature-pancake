/**
 * Staging assessment persistence. We keep every assessment (append-only) so a
 * re-take is a new row and history is preserved; `getLatest` returns the most
 * recent, which is what onboarding and the Doctor Report use.
 */

import { getDb } from '../db';
import { nowIso } from '@/utils/date';
import type { MenopauseStage } from '@/constants/staging';
import type { NewStagingAssessment, StagingAssessment } from '../types';

interface StagingRow {
  id: number;
  stage: string;
  score: number;
  answers: string;
  assessed_at: string;
}

function mapRow(r: StagingRow): StagingAssessment {
  let answers: Record<string, number> = {};
  try {
    answers = JSON.parse(r.answers) as Record<string, number>;
  } catch {
    answers = {};
  }
  return {
    id: r.id,
    stage: r.stage as MenopauseStage,
    score: r.score,
    answers,
    assessedAt: r.assessed_at,
  };
}

export const stagingAssessmentRepository = {
  async create(input: NewStagingAssessment): Promise<StagingAssessment> {
    const db = await getDb();
    const assessedAt = input.assessedAt ?? nowIso();
    const result = await db.runAsync(
      `INSERT INTO staging_assessments (stage, score, answers, assessed_at)
       VALUES (?, ?, ?, ?)`,
      [input.stage, input.score, JSON.stringify(input.answers), assessedAt],
    );
    return {
      id: result.lastInsertRowId,
      stage: input.stage,
      score: input.score,
      answers: input.answers,
      assessedAt,
    };
  },

  async getLatest(): Promise<StagingAssessment | null> {
    const db = await getDb();
    const row = await db.getFirstAsync<StagingRow>(
      'SELECT * FROM staging_assessments ORDER BY assessed_at DESC, id DESC LIMIT 1',
    );
    return row ? mapRow(row) : null;
  },
};
