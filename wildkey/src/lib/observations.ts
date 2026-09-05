export type SyncState = "queued" | "uploading" | "confirmed" | "failed";

export type Observation = {
  id: string;
  createdAt: string;
  photoDataUrl: string;
  commonName: string;
  scientificName: string;
  confidence: number;
  taxonSlug: string;
  syncState: SyncState;
};

const STORAGE_KEY = "wildkey.observations";

function readAll(): Observation[] {
  if (typeof window === "undefined") return [];
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    return raw ? (JSON.parse(raw) as Observation[]) : [];
  } catch {
    return [];
  }
}

function writeAll(observations: Observation[]) {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(observations));
}

export function listObservations(): Observation[] {
  return readAll().sort((a, b) => b.createdAt.localeCompare(a.createdAt));
}

export function saveObservation(
  input: Omit<Observation, "id" | "createdAt" | "syncState">,
): Observation {
  const observation: Observation = {
    ...input,
    id: `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
    createdAt: new Date().toISOString(),
    syncState: "queued",
  };
  writeAll([...readAll(), observation]);
  return observation;
}

export function updateSyncState(id: string, syncState: SyncState) {
  writeAll(readAll().map((o) => (o.id === id ? { ...o, syncState } : o)));
}

export function deleteObservation(id: string) {
  writeAll(readAll().filter((o) => o.id !== id));
}

/**
 * Simulates the local-first sync pipeline (Part D.1): every save is queued,
 * then moves through uploading to a terminal confirmed/failed state that's
 * always visible and retryable — never silent. A real implementation
 * replaces this with an actual network call against the sync API.
 */
export function runMockSync(
  id: string,
  onChange: (state: SyncState) => void,
) {
  onChange("uploading");
  window.setTimeout(() => {
    const succeeded = Math.random() > 0.15;
    const nextState: SyncState = succeeded ? "confirmed" : "failed";
    updateSyncState(id, nextState);
    onChange(nextState);
  }, 900);
}
