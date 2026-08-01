/**
 * The canonical set of symptom categories the check-in and reports are built
 * around. `id` is what gets persisted in `symptom_logs.category`; keep these
 * stable — changing an id orphans existing rows. Labels/emoji are presentation
 * only and safe to edit.
 */

export interface SymptomCategory {
  id: string;
  label: string;
  emoji: string;
  /** Short helper shown under the label on the check-in card. */
  hint?: string;
}

export const SYMPTOM_CATEGORIES: readonly SymptomCategory[] = [
  { id: 'hot_flashes', label: 'Hot flashes', emoji: '🔥', hint: 'Sudden warmth or flushing' },
  { id: 'night_sweats', label: 'Night sweats', emoji: '💧', hint: 'Sweating during sleep' },
  { id: 'sleep', label: 'Sleep', emoji: '😴', hint: 'Trouble falling or staying asleep' },
  { id: 'mood', label: 'Mood', emoji: '🌗', hint: 'Irritability, low mood, anxiety' },
  { id: 'brain_fog', label: 'Brain fog', emoji: '🌫️', hint: 'Focus or memory' },
  { id: 'fatigue', label: 'Fatigue', emoji: '🪫', hint: 'Energy levels' },
  { id: 'headache', label: 'Headache', emoji: '🤕' },
  { id: 'joint_aches', label: 'Joint aches', emoji: '🦴' },
] as const;

export const SYMPTOM_BY_ID: Record<string, SymptomCategory> = Object.fromEntries(
  SYMPTOM_CATEGORIES.map((s) => [s.id, s]),
);

/** Severity 1–5 scale, shared by the check-in card, Trends legend and report. */
export const SEVERITY_LABELS: Record<number, string> = {
  1: 'Mild',
  2: 'Light',
  3: 'Moderate',
  4: 'Strong',
  5: 'Severe',
};
