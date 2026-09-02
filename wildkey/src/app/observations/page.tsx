"use client";

import { useEffect, useState } from "react";
import Link from "next/link";
import {
  deleteObservation,
  listObservations,
  runMockSync,
  type Observation,
  type SyncState,
} from "@/lib/observations";

const SYNC_LABEL: Record<SyncState, string> = {
  queued: "Queued",
  uploading: "Uploading…",
  confirmed: "Confirmed",
  failed: "Failed — tap to retry",
};

const SYNC_COLOR: Record<SyncState, string> = {
  queued: "var(--color-text-muted)",
  uploading: "var(--color-sky)",
  confirmed: "var(--color-accent)",
  failed: "var(--color-danger)",
};

export default function ObservationsPage() {
  const [observations, setObservations] = useState<Observation[] | null>(null);

  useEffect(() => {
    // Reading from localStorage on mount, not deriving state from
    // props/state — the pattern react-hooks/set-state-in-effect warns
    // about does not apply here.
    // eslint-disable-next-line react-hooks/set-state-in-effect
    setObservations(listObservations());
  }, []);

  const refresh = () => setObservations(listObservations());

  const retry = (id: string) => {
    runMockSync(id, () => refresh());
    refresh();
  };

  const remove = (id: string) => {
    deleteObservation(id);
    refresh();
  };

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="font-display text-2xl font-semibold">My Observations</h1>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Local-first — saved to this device immediately, synced in the background. Nothing
            silently vanishes.
          </p>
        </div>
        <Link
          href="/camera"
          className="shrink-0 rounded-full px-4 py-2 text-sm font-semibold"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          + New
        </Link>
      </div>

      {observations === null ? null : observations.length === 0 ? (
        <div
          className="rounded-lg border p-8 text-center text-sm"
          style={{ borderColor: "var(--color-border)", color: "var(--color-text-muted)" }}
        >
          Nothing saved yet.{" "}
          <Link href="/camera" style={{ color: "var(--color-accent)" }}>
            Identify something
          </Link>{" "}
          to start your collection.
        </div>
      ) : (
        <ul className="flex flex-col gap-3">
          {observations.map((o) => (
            <li
              key={o.id}
              className="flex items-center gap-3 rounded-lg border p-3"
              style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
            >
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img
                src={o.photoDataUrl}
                alt={o.commonName}
                className="h-16 w-16 shrink-0 rounded-md object-cover"
              />
              <div className="min-w-0 flex-1">
                <Link
                  href={`/species/${o.taxonSlug}`}
                  className="block truncate font-semibold hover:underline"
                >
                  {o.commonName}
                </Link>
                <p className="truncate text-xs italic" style={{ color: "var(--color-text-muted)" }}>
                  {o.scientificName}
                </p>
                <p className="mt-1 text-xs" style={{ color: "var(--color-text-muted)" }}>
                  {new Date(o.createdAt).toLocaleString()}
                </p>
              </div>
              <div className="flex shrink-0 flex-col items-end gap-1">
                <button
                  onClick={() => (o.syncState === "failed" ? retry(o.id) : undefined)}
                  className="text-xs font-semibold"
                  style={{
                    color: SYNC_COLOR[o.syncState],
                    cursor: o.syncState === "failed" ? "pointer" : "default",
                  }}
                >
                  {SYNC_LABEL[o.syncState]}
                </button>
                <button
                  onClick={() => remove(o.id)}
                  className="text-xs"
                  style={{ color: "var(--color-text-muted)" }}
                >
                  Delete
                </button>
              </div>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
