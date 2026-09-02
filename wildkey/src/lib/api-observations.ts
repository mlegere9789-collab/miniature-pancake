import type { ServerObservation } from "@/lib/server/store";

export type { ServerObservation };

export async function fetchServerObservations(): Promise<ServerObservation[]> {
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
