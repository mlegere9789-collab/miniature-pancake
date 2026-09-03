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

  db.exec(`
    CREATE TABLE IF NOT EXISTS users (
      id TEXT PRIMARY KEY,
      email TEXT NOT NULL UNIQUE,
      password_hash TEXT NOT NULL,
      password_salt TEXT NOT NULL,
      created_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS sessions (
      token TEXT PRIMARY KEY,
      user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);

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
      notes TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_observations_user_id ON observations(user_id);
    CREATE INDEX IF NOT EXISTS idx_observations_taxon_slug ON observations(taxon_slug);

    CREATE TABLE IF NOT EXISTS comments (
      id TEXT PRIMARY KEY,
      observation_id TEXT NOT NULL REFERENCES observations(id) ON DELETE CASCADE,
      user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      user_email TEXT NOT NULL,
      body TEXT NOT NULL,
      kind TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_comments_observation_id ON comments(observation_id);
    CREATE INDEX IF NOT EXISTS idx_comments_user_id ON comments(user_id);
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
