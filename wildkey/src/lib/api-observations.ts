import type {
  ActivityItem,
  LifeStage,
  ObservationComment,
  ObservationDetail,
  ObservationLicense,
  ObservationPhoto,
  ObservationWithGrade,
  Phenology,
  ServerObservation,
  Sex,
} from "@/lib/server/store";
import type { CurrentUser } from "@/lib/auth-context";

export type {
  ServerObservation,
  ObservationComment,
  ObservationWithGrade,
  ObservationDetail,
  ObservationPhoto,
  ActivityItem,
  ObservationLicense,
  LifeStage,
  Sex,
  Phenology,
};

export async function fetchActivity(): Promise<ActivityItem[]> {
  const res = await fetch("/api/activity");
  if (!res.ok) return [];
  const data = await res.json();
  return data.activity ?? [];
}

export async function fetchServerObservations(): Promise<ObservationWithGrade[]> {
  const res = await fetch("/api/observations");
  if (!res.ok) return [];
  const data = await res.json();
  return data.observations ?? [];
}

export async function createServerObservation(input: {
  photoDataUrl: string;
  commonName: string;
  scientificName: string;
  confidence: number;
  taxonSlug: string;
  isWild: boolean;
  locationName: string;
  notes: string;
  lat?: number | null;
  lng?: number | null;
  license?: ObservationLicense;
  extraPhotoDataUrls?: string[];
  lifeStage?: LifeStage | null;
  sex?: Sex | null;
  phenology?: Phenology | null;
}): Promise<ServerObservation | null> {
  const res = await fetch("/api/observations", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(input),
  });
  if (!res.ok) return null;
  const data = await res.json();
  return data.observation ?? null;
}

export async function deleteServerObservation(id: string): Promise<boolean> {
  const res = await fetch(`/api/observations/${id}`, { method: "DELETE" });
  return res.ok;
}

export async function updateServerObservationLicense(
  id: string,
  license: ObservationLicense,
): Promise<boolean> {
  const res = await fetch(`/api/observations/${id}`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ license }),
  });
  return res.ok;
}

export async function updateServerObservationAnnotations(
  id: string,
  updates: { lifeStage?: LifeStage | null; sex?: Sex | null; phenology?: Phenology | null },
): Promise<boolean> {
  const res = await fetch(`/api/observations/${id}`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(updates),
  });
  return res.ok;
}

export async function fetchServerObservation(
  id: string,
): Promise<{ observation: ObservationDetail; author: CurrentUser | null } | null> {
  const res = await fetch(`/api/observations/${id}`);
  if (!res.ok) return null;
  return res.json();
}

export async function addServerObservationPhotos(
  id: string,
  photoDataUrls: string[],
): Promise<boolean> {
  const res = await fetch(`/api/observations/${id}/photos`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ photoDataUrls }),
  });
  return res.ok;
}

export async function deleteServerObservationPhoto(id: string, photoId: string): Promise<boolean> {
  const res = await fetch(`/api/observations/${id}/photos/${photoId}`, { method: "DELETE" });
  return res.ok;
}

export async function setServerObservationCoverPhoto(id: string, photoId: string): Promise<boolean> {
  const res = await fetch(`/api/observations/${id}/photos/${photoId}/cover`, { method: "POST" });
  return res.ok;
}

export async function fetchObservationComments(id: string): Promise<ObservationComment[]> {
  const res = await fetch(`/api/observations/${id}/comments`);
  if (!res.ok) return [];
  const data = await res.json();
  return data.comments ?? [];
}

export type ObservationNeedingId = ObservationWithGrade & { observerEmail: string };

export async function fetchObservationsNeedingId(): Promise<ObservationNeedingId[]> {
  const res = await fetch("/api/observations/needs-id");
  if (!res.ok) return [];
  const data = await res.json();
  return data.observations ?? [];
}

export async function postObservationComment(
  id: string,
  input: { body: string; kind: "comment" | "agree" },
): Promise<ObservationComment | null> {
  const res = await fetch(`/api/observations/${id}/comments`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(input),
  });
  if (!res.ok) return null;
  const data = await res.json();
  return data.comment ?? null;
}
