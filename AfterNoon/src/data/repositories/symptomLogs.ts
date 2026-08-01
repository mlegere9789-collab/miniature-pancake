/**
 * Symptom log persistence + the range aggregation that powers Trends and the
 * Doctor Report. Both features call `getDailyCategorySeverity` with a date
 * range so they can never drift apart.
 */

import { getDb } from '../db';
import { nowIso, toLocalDateKey } from '@/utils/date';
import type {
  DailyCategorySeverity,
  NewSymptomLog,
  Severity,
  SymptomLog,
} from '../types';

interface SymptomRow {
  id: number;
  category: string;
  severity: number;
  note: string | null;
  logged_at: string;
  created_at: string;
}

function mapRow(r: SymptomRow): SymptomLog {
  return {
    id: r.id,
    category: r.category,
    severity: r.severity as Severity,
    note: r.note,
    loggedAt: r.logged_at,
    createdAt: r.created_at,
  };
}

export const symptomLogsRepository = {
  async create(input: NewSymptomLog): Promise<SymptomLog> {
    const db = await getDb();
    const loggedAt = input.loggedAt ?? nowIso();
    const createdAt = nowIso();
    const result = await db.runAsync(
      `INSERT INTO symptom_logs (category, severity, note, logged_at, created_at)
       VALUES (?, ?, ?, ?, ?)`,
      [input.category, input.severity, input.note ?? null, loggedAt, createdAt],
    );
    return {
      id: result.lastInsertRowId,
      category: input.category,
      severity: input.severity,
      note: input.note ?? null,
      loggedAt,
      createdAt,
    };
  },

  async getById(id: number): Promise<SymptomLog | null> {
    const db = await getDb();
    const row = await db.getFirstAsync<SymptomRow>('SELECT * FROM symptom_logs WHERE id = ?', [id]);
    return row ? mapRow(row) : null;
  },

  /** All logs whose loggedAt falls in [startIso, endIso], newest first. */
  async getInRange(startIso: string, endIso: string): Promise<SymptomLog[]> {
    const db = await getDb();
    const rows = await db.getAllAsync<SymptomRow>(
      `SELECT * FROM symptom_logs
       WHERE logged_at >= ? AND logged_at <= ?
       ORDER BY logged_at DESC`,
      [startIso, endIso],
    );
    return rows.map(mapRow);
  },

  /** Logs for a specific local calendar day, used by the Home check-in card. */
  async getForDay(dateKey: string): Promise<SymptomLog[]> {
    const db = await getDb();
    const rows = await db.getAllAsync<SymptomRow>(
      `SELECT * FROM symptom_logs
       WHERE date(logged_at, 'localtime') = ?
       ORDER BY created_at DESC`,
      [dateKey],
    );
    return rows.map(mapRow);
  },

  async deleteById(id: number): Promise<void> {
    const db = await getDb();
    await db.runAsync('DELETE FROM symptom_logs WHERE id = ?', [id]);
  },

  /**
   * Per-day, per-category average severity across a range. This is the single
   * aggregation feeding both the Trends heatmap and the Doctor Report tables.
   * Grouping is done in JS on local date keys so it matches what the user saw.
   */
  async getDailyCategorySeverity(
    startIso: string,
    endIso: string,
  ): Promise<DailyCategorySeverity[]> {
    const logs = await this.getInRange(startIso, endIso);
    const buckets = new Map<string, { sum: number; count: number; date: string; category: string }>();
    for (const log of logs) {
      const dateKey = toLocalDateKey(log.loggedAt);
      const key = `${dateKey}|${log.category}`;
      const bucket = buckets.get(key) ?? { sum: 0, count: 0, date: dateKey, category: log.category };
      bucket.sum += log.severity;
      bucket.count += 1;
      buckets.set(key, bucket);
    }
    return Array.from(buckets.values()).map((b) => ({
      date: b.date,
      category: b.category,
      avgSeverity: b.sum / b.count,
      count: b.count,
    }));
  },

  async countAll(): Promise<number> {
    const db = await getDb();
    const row = await db.getFirstAsync<{ n: number }>('SELECT COUNT(*) AS n FROM symptom_logs');
    return row?.n ?? 0;
  },
};
