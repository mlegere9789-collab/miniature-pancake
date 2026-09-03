/**
 * Open data licensing choice, per observation (Part C.3/G) — plain client-
 * safe data, deliberately kept out of src/lib/server/store.ts (which pulls
 * in better-sqlite3 and can't be imported from a "use client" component).
 * store.ts re-exports these so existing server-side imports don't change.
 */
export const OBSERVATION_LICENSES = ["CC0", "CC-BY", "CC-BY-NC", "all-rights-reserved"] as const;
export type ObservationLicense = (typeof OBSERVATION_LICENSES)[number];

/** Matches the real default this app's own model (iNaturalist) uses. */
export const DEFAULT_OBSERVATION_LICENSE: ObservationLicense = "CC-BY-NC";

export const LICENSE_LABELS: Record<ObservationLicense, string> = {
  CC0: "CC0 — Public domain",
  "CC-BY": "CC-BY — Attribution required",
  "CC-BY-NC": "CC-BY-NC — Attribution, non-commercial",
  "all-rights-reserved": "All rights reserved",
};

export const LICENSE_DESCRIPTIONS: Record<ObservationLicense, string> = {
  CC0: "No rights reserved — anyone can use this photo and data for any purpose, no credit required.",
  "CC-BY": "Anyone can use and share this, including commercially, as long as they credit you.",
  "CC-BY-NC": "Anyone can use and share this non-commercially, as long as they credit you.",
  "all-rights-reserved": "No reuse rights granted — visible on Wildkey, but not licensed for reuse elsewhere.",
};
