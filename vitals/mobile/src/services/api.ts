import {
  CreateCheckInResponse,
  CreatePlantInput,
  Garden,
  LeaderboardResult,
  Plant,
  PlantDetail,
  SpeciesDormancyLookup,
  SpeciesSuggestion,
  TwinComparison,
  WeeklyReportCard,
} from "../types/domain";

// Point this at the machine running `npm run dev` in vitals/backend.
// Use your LAN IP (not localhost) when testing on a physical device via Expo Go.
export const API_BASE_URL = process.env.EXPO_PUBLIC_VITALS_API_URL ?? "http://localhost:4000";

/** Resolves a backend-relative photo path (e.g. "/uploads/x.jpg") to a full URL. */
export function toAbsoluteUrl(photoUrl: string | undefined): string | undefined {
  if (!photoUrl) return undefined;
  return photoUrl.startsWith("http") ? photoUrl : `${API_BASE_URL}${photoUrl}`;
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE_URL}${path}`, init);
  if (!res.ok) {
    const body = await res.text();
    throw new Error(`Vitals API ${path} failed (${res.status}): ${body}`);
  }
  return res.json() as Promise<T>;
}

export async function fetchGarden(gardenId: string): Promise<Garden> {
  return request(`/gardens/${gardenId}`);
}

export async function fetchPlant(plantId: string): Promise<PlantDetail> {
  return request(`/plants/${plantId}`);
}

export async function fetchWeeklyReportCard(gardenId: string): Promise<WeeklyReportCard> {
  return request(`/gardens/${gardenId}/report-card`);
}

export async function fetchTwinComparison(plantId: string): Promise<TwinComparison> {
  return request(`/plants/${plantId}/twin-comparison`);
}

export async function fetchLeaderboard(gardenId: string): Promise<LeaderboardResult> {
  return request(`/gardens/${gardenId}/leaderboard`);
}

/**
 * Dormancy defaults for the "Dormant in winter" toggle (spec §4.7). Passing
 * `speciesId` checks the curated per-species dormancy table first (a real
 * deciduous/evergreen/annual habit) and falls back to the hemisphere-only
 * heuristic when the species isn't recognized.
 */
export async function fetchDormancyDefaults(gardenId: string, speciesId?: string): Promise<SpeciesDormancyLookup> {
  const query = speciesId ? `?speciesId=${encodeURIComponent(speciesId)}` : "";
  return request<SpeciesDormancyLookup>(`/gardens/${gardenId}/dormancy-defaults${query}`);
}

export async function searchSpecies(query: string): Promise<SpeciesSuggestion[]> {
  if (!query.trim()) return [];
  return request<SpeciesSuggestion[]>(`/species/search?q=${encodeURIComponent(query.trim())}`);
}

export async function setLeaderboardOptIn(gardenId: string, optIn: boolean): Promise<Garden> {
  return request(`/gardens/${gardenId}/leaderboard-opt-in`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ leaderboardOptIn: optIn }),
  });
}

export async function createPlant(input: CreatePlantInput): Promise<Plant> {
  return request(`/plants`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(input),
  });
}

export async function uploadCheckInPhoto(localUri: string): Promise<string> {
  const form = new FormData();
  // React Native's fetch/FormData accepts this shape for file uploads.
  form.append("photo", {
    uri: localUri,
    name: `checkin-${Date.now()}.jpg`,
    type: "image/jpeg",
  } as unknown as Blob);

  const res = await fetch(`${API_BASE_URL}/uploads`, { method: "POST", body: form });
  if (!res.ok) throw new Error(`photo upload failed (${res.status})`);
  const { photoUrl } = (await res.json()) as { photoUrl: string };
  return photoUrl;
}

export async function submitCheckIn(plantId: string, photoUrl: string): Promise<CreateCheckInResponse> {
  return request(`/checkins`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ plantId, photoUrl }),
  });
}
