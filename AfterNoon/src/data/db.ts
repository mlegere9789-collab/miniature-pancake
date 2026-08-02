/**
 * SQLite connection + migration bootstrap.
 *
 * This is the ONLY module that talks to expo-sqlite directly. Everything else
 * goes through repositories, which receive the handle from here. Swapping the
 * storage engine (e.g. to a mock in tests, or op-sqlite later) means touching
 * only this file.
 */

import * as SQLite from 'expo-sqlite';

import { LATEST_VERSION, MIGRATIONS } from './schema';

const DB_NAME = 'afternoon.db';

let dbPromise: Promise<SQLite.SQLiteDatabase> | null = null;

async function open(): Promise<SQLite.SQLiteDatabase> {
  const db = await SQLite.openDatabaseAsync(DB_NAME);
  await db.execAsync('PRAGMA journal_mode = WAL;');
  await db.execAsync('PRAGMA foreign_keys = ON;');
  await runMigrations(db);
  return db;
}

async function runMigrations(db: SQLite.SQLiteDatabase): Promise<void> {
  const row = await db.getFirstAsync<{ user_version: number }>('PRAGMA user_version;');
  const current = row?.user_version ?? 0;
  if (current >= LATEST_VERSION) return;

  for (const migration of MIGRATIONS) {
    if (migration.version <= current) continue;
    await db.withTransactionAsync(async () => {
      await db.execAsync(migration.up);
    });
    // PRAGMA user_version does not accept bound params.
    await db.execAsync(`PRAGMA user_version = ${migration.version};`);
  }
}

/**
 * Returns the shared, migrated database handle. Safe to call from anywhere;
 * the connection is opened once and reused.
 */
export function getDb(): Promise<SQLite.SQLiteDatabase> {
  if (!dbPromise) {
    dbPromise = open().catch((err) => {
      // Reset so a later call can retry rather than caching a rejected promise.
      dbPromise = null;
      throw err;
    });
  }
  return dbPromise;
}

/** Test/reset helper: drops all rows but keeps the schema. */
export async function resetAllData(): Promise<void> {
  const db = await getDb();
  await db.withTransactionAsync(async () => {
    await db.execAsync(`
      DELETE FROM symptom_logs;
      DELETE FROM staging_assessments;
      DELETE FROM cycle_notes;
      DELETE FROM app_settings;
    `);
  });
}
