/**
 * Date helpers. All persistence uses ISO-8601 strings; day-grouping uses local
 * calendar dates (YYYY-MM-DD) so a log at 11pm counts for the day the user
 * experienced it, not UTC's day.
 */

export function nowIso(): string {
  return new Date().toISOString();
}

/** Local calendar date (YYYY-MM-DD) for a Date or ISO string. */
export function toLocalDateKey(input: Date | string = new Date()): string {
  const d = typeof input === 'string' ? new Date(input) : input;
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${y}-${m}-${day}`;
}

/** Start-of-day ISO instant N days before today (local). */
export function startOfDaysAgoIso(days: number): string {
  const d = new Date();
  d.setHours(0, 0, 0, 0);
  d.setDate(d.getDate() - days);
  return d.toISOString();
}

/** End-of-today ISO instant (local). */
export function endOfTodayIso(): string {
  const d = new Date();
  d.setHours(23, 59, 59, 999);
  return d.toISOString();
}

/** Inclusive list of YYYY-MM-DD keys between two local date keys. */
export function dateKeyRange(startKey: string, endKey: string): string[] {
  const out: string[] = [];
  const start = new Date(`${startKey}T00:00:00`);
  const end = new Date(`${endKey}T00:00:00`);
  for (let d = new Date(start); d <= end; d.setDate(d.getDate() + 1)) {
    out.push(toLocalDateKey(d));
  }
  return out;
}

/** Human-friendly date, e.g. "1 Aug 2026". */
export function formatDisplayDate(input: Date | string): string {
  const d = typeof input === 'string' ? new Date(input) : input;
  return d.toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: 'numeric' });
}
