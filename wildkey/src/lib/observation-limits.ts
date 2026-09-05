/**
 * Plain client-safe constant, kept out of src/lib/server/store.ts (which
 * pulls in better-sqlite3 and can't be imported from a "use client"
 * component) — store.ts re-exports this so server-side imports don't
 * change. Cover + extras — matches real-world multi-photo caps (e.g.
 * iNaturalist's own upload limit), not an arbitrary number.
 */
export const MAX_PHOTOS_PER_OBSERVATION = 6;
