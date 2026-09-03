import Database from "better-sqlite3";
import { mkdirSync } from "node:fs";
import path from "node:path";

/**
 * Real embedded SQL database (Part G: "swap this module for a real
 * database"). ACID transactions and real concurrent-write safety, unlike
 * the hand-rolled JSON file store it replaces — the same table shape maps
 * cleanly onto a hosted Postgres later; this is a driver swap behind
 * src/lib/server/store.ts's exported functions, not a rewrite.
 *
 * Still a single-file, single-writer-friendly database, not a
 * horizontally-scaled one — the honest boundary is a multi-instance/
 * multi-region deploy, which needs a hosted server (Postgres, etc.), not
 * anything achievable by swapping a Node driver alone.
 */

const DB_DIR = path.join(process.cwd(), ".data");
const DB_PATH = path.join(DB_DIR, "wildkey.sqlite3");

declare global {
  var __wildkeyDb: Database.Database | undefined;
}

function createConnection(): Database.Database {
  mkdirSync(DB_DIR, { recursive: true });
  const db = new Database(DB_PATH);
  db.pragma("journal_mode = WAL");
  db.pragma("foreign_keys = ON");

  // Table shapes only here — no indexes yet. A pre-existing database from
  // before a column was added would otherwise fail on an index over that
  // column before the migration below has a chance to add it.
  db.exec(`
    CREATE TABLE IF NOT EXISTS users (
      id TEXT PRIMARY KEY,
      email TEXT NOT NULL UNIQUE,
      password_hash TEXT NOT NULL,
      password_salt TEXT NOT NULL,
      role TEXT NOT NULL DEFAULT 'member',
      created_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS sessions (
      token TEXT PRIMARY KEY,
      user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      created_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS observations (
      id TEXT PRIMARY KEY,
      user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      created_at TEXT NOT NULL,
      photo_data_url TEXT NOT NULL,
      common_name TEXT NOT NULL,
      scientific_name TEXT NOT NULL,
      confidence REAL NOT NULL,
      taxon_slug TEXT NOT NULL,
      sync_state TEXT NOT NULL,
      is_wild INTEGER NOT NULL,
      location_name TEXT NOT NULL DEFAULT '',
      notes TEXT NOT NULL DEFAULT '',
      lat REAL,
      lng REAL
    );

    CREATE TABLE IF NOT EXISTS comments (
      id TEXT PRIMARY KEY,
      observation_id TEXT NOT NULL REFERENCES observations(id) ON DELETE CASCADE,
      user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      user_email TEXT NOT NULL,
      body TEXT NOT NULL,
      kind TEXT NOT NULL,
      created_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS journal_posts (
      id TEXT PRIMARY KEY,
      author_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      title TEXT NOT NULL,
      body TEXT NOT NULL,
      created_at TEXT NOT NULL,
      updated_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS guides (
      id TEXT PRIMARY KEY,
      curator_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      title TEXT NOT NULL,
      description TEXT NOT NULL,
      taxon_slugs TEXT NOT NULL,
      created_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS projects (
      id TEXT PRIMARY KEY,
      owner_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      type TEXT NOT NULL,
      title TEXT NOT NULL,
      description TEXT NOT NULL,
      taxon_filter TEXT NOT NULL,
      created_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS project_members (
      project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
      user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      joined_at TEXT NOT NULL,
      PRIMARY KEY (project_id, user_id)
    );

    CREATE TABLE IF NOT EXISTS observation_flags (
      id TEXT PRIMARY KEY,
      observation_id TEXT NOT NULL REFERENCES observations(id) ON DELETE CASCADE,
      reporter_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      reason TEXT NOT NULL,
      status TEXT NOT NULL DEFAULT 'open',
      created_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS curator_actions (
      id TEXT PRIMARY KEY,
      flag_id TEXT NOT NULL REFERENCES observation_flags(id) ON DELETE CASCADE,
      curator_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      action TEXT NOT NULL,
      reason TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
  `);

  // Defensive migrations for a pre-existing .data/wildkey.sqlite3 created
  // before these columns existed (fresh databases already have them via
  // the CREATE TABLE statements above). Must run before any index touches
  // these columns.
  const userColumns = db.prepare("PRAGMA table_info(users)").all() as { name: string }[];
  if (!userColumns.some((c) => c.name === "role")) {
    db.exec("ALTER TABLE users ADD COLUMN role TEXT NOT NULL DEFAULT 'member'");
  }
  const observationColumns = db.prepare("PRAGMA table_info(observations)").all() as { name: string }[];
  if (!observationColumns.some((c) => c.name === "lat")) {
    db.exec("ALTER TABLE observations ADD COLUMN lat REAL");
  }
  if (!observationColumns.some((c) => c.name === "lng")) {
    db.exec("ALTER TABLE observations ADD COLUMN lng REAL");
  }

  db.exec(`
    CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);
    CREATE INDEX IF NOT EXISTS idx_observations_user_id ON observations(user_id);
    CREATE INDEX IF NOT EXISTS idx_observations_taxon_slug ON observations(taxon_slug);
    CREATE INDEX IF NOT EXISTS idx_observations_lat_lng ON observations(lat, lng);
    CREATE INDEX IF NOT EXISTS idx_comments_observation_id ON comments(observation_id);
    CREATE INDEX IF NOT EXISTS idx_comments_user_id ON comments(user_id);
    CREATE INDEX IF NOT EXISTS idx_journal_posts_author_id ON journal_posts(author_id);
    CREATE INDEX IF NOT EXISTS idx_guides_curator_id ON guides(curator_id);
    CREATE INDEX IF NOT EXISTS idx_projects_owner_id ON projects(owner_id);
    CREATE INDEX IF NOT EXISTS idx_project_members_user_id ON project_members(user_id);
    CREATE INDEX IF NOT EXISTS idx_observation_flags_observation_id ON observation_flags(observation_id);
    CREATE INDEX IF NOT EXISTS idx_observation_flags_status ON observation_flags(status);
    CREATE INDEX IF NOT EXISTS idx_curator_actions_flag_id ON curator_actions(flag_id);
  `);

  return db;
}

// Reused across hot-reloads in dev (Next.js dev server re-evaluates modules
// on change) so we don't leak connections or hit "database is locked" from
// two open handles on the same WAL file.
export const db = globalThis.__wildkeyDb ?? createConnection();
if (process.env.NODE_ENV !== "production") {
  globalThis.__wildkeyDb = db;
}
