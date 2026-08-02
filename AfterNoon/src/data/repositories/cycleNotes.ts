/**
 * Cycle note persistence. One note row per entry; the Doctor Report pulls these
 * by date range alongside symptom logs.
 */

import { getDb } from '../db';
import { nowIso } from '@/utils/date';
import type { CycleNote, FlowLevel, NewCycleNote } from '../types';

interface CycleRow {
  id: number;
  note_date: string;
  flow: string | null;
  note: string | null;
  created_at: string;
}

function mapRow(r: CycleRow): CycleNote {
  return {
    id: r.id,
    noteDate: r.note_date,
    flow: (r.flow as FlowLevel | null) ?? null,
    note: r.note,
    createdAt: r.created_at,
  };
}

export const cycleNotesRepository = {
  async create(input: NewCycleNote): Promise<CycleNote> {
    const db = await getDb();
    const createdAt = nowIso();
    const result = await db.runAsync(
      `INSERT INTO cycle_notes (note_date, flow, note, created_at) VALUES (?, ?, ?, ?)`,
      [input.noteDate, input.flow ?? null, input.note ?? null, createdAt],
    );
    return {
      id: result.lastInsertRowId,
      noteDate: input.noteDate,
      flow: input.flow ?? null,
      note: input.note ?? null,
      createdAt,
    };
  },

  async getInRange(startDateKey: string, endDateKey: string): Promise<CycleNote[]> {
    const db = await getDb();
    const rows = await db.getAllAsync<CycleRow>(
      `SELECT * FROM cycle_notes
       WHERE note_date >= ? AND note_date <= ?
       ORDER BY note_date DESC`,
      [startDateKey, endDateKey],
    );
    return rows.map(mapRow);
  },

  async deleteById(id: number): Promise<void> {
    const db = await getDb();
    await db.runAsync('DELETE FROM cycle_notes WHERE id = ?', [id]);
  },
};
