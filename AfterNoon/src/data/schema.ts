/**
 * SQLite schema + migrations.
 *
 * Migrations are applied in order by `runMigrations`, guarded by PRAGMA
 * user_version. To evolve the schema, append a new migration — never edit an
 * existing one that has shipped.
 */

export interface Migration {
  version: number;
  up: string;
}

export const MIGRATIONS: readonly Migration[] = [
  {
    version: 1,
    up: `
      CREATE TABLE IF NOT EXISTS symptom_logs (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        category   TEXT    NOT NULL,
        severity   INTEGER NOT NULL CHECK (severity BETWEEN 1 AND 5),
        note       TEXT,
        logged_at  TEXT    NOT NULL,
        created_at TEXT    NOT NULL
      );
      CREATE INDEX IF NOT EXISTS idx_symptom_logs_logged_at ON symptom_logs (logged_at);
      CREATE INDEX IF NOT EXISTS idx_symptom_logs_category  ON symptom_logs (category);

      CREATE TABLE IF NOT EXISTS staging_assessments (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        stage       TEXT    NOT NULL,
        score       INTEGER NOT NULL,
        answers     TEXT    NOT NULL,
        assessed_at TEXT    NOT NULL
      );

      CREATE TABLE IF NOT EXISTS cycle_notes (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        note_date  TEXT NOT NULL,
        flow       TEXT,
        note       TEXT,
        created_at TEXT NOT NULL
      );
      CREATE INDEX IF NOT EXISTS idx_cycle_notes_date ON cycle_notes (note_date);

      CREATE TABLE IF NOT EXISTS app_settings (
        key   TEXT PRIMARY KEY,
        value TEXT NOT NULL
      );
    `,
  },
];

export const LATEST_VERSION = MIGRATIONS.reduce((m, x) => Math.max(m, x.version), 0);
