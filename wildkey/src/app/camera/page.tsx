"use client";

import { useRef, useState } from "react";
import Link from "next/link";
import { IdResultCard } from "@/components/id-result-card";
import { MOCK_SPECIES, type Species } from "@/lib/mock-species";
import { runMockSync, saveObservation, type SyncState } from "@/lib/observations";
import { createServerObservation } from "@/lib/api-observations";
import { useMode } from "@/lib/mode-context";
import { useAuth } from "@/lib/auth-context";

type IdOutcome = { species: Species; confidence: number };

function readFileAsDataUrl(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result as string);
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}

export default function CameraPage() {
  const { mode } = useMode();
  const { user } = useAuth();
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [previewUrl, setPreviewUrl] = useState<string | null>(null);
  const [outcome, setOutcome] = useState<IdOutcome | null>(null);
  const [isIdentifying, setIsIdentifying] = useState(false);
  const [savedSyncState, setSavedSyncState] = useState<SyncState | null>(null);
  const [locationName, setLocationName] = useState("");
  const [notes, setNotes] = useState("");
  const [isWild, setIsWild] = useState(true);
  const useServerSync = mode === "naturalist" && Boolean(user);

  const handleFile = async (file: File | undefined) => {
    if (!file) return;
    setOutcome(null);
    setSavedSyncState(null);
    setLocationName("");
    setNotes("");
    setIsWild(true);
    setPreviewUrl(await readFileAsDataUrl(file));
  };

  const runMockIdentify = () => {
    setIsIdentifying(true);
    // Simulates on-device inference latency. Real implementation calls the
    // bundled on-device model — no network round trip.
    window.setTimeout(() => {
      const species = MOCK_SPECIES[Math.floor(Math.random() * MOCK_SPECIES.length)];
      const confidence = 0.35 + Math.random() * 0.6;
      setOutcome({ species, confidence });
      setIsIdentifying(false);
    }, 700);
  };

  const saveToObservations = async () => {
    if (!outcome || !previewUrl) return;

    if (useServerSync) {
      setSavedSyncState("uploading");
      const saved = await createServerObservation({
        photoDataUrl: previewUrl,
        commonName: outcome.species.commonName,
        scientificName: outcome.species.scientificName,
        confidence: outcome.confidence,
        taxonSlug: outcome.species.slug,
        isWild,
        locationName: locationName.trim(),
        notes: notes.trim(),
      });
      setSavedSyncState(saved ? "confirmed" : "failed");
      return;
    }

    const observation = saveObservation({
      photoDataUrl: previewUrl,
      commonName: outcome.species.commonName,
      scientificName: outcome.species.scientificName,
      confidence: outcome.confidence,
      taxonSlug: outcome.species.slug,
    });
    setSavedSyncState("queued");
    runMockSync(observation.id, setSavedSyncState);
  };

  return (
    <div className="mx-auto flex w-full max-w-xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Identify something</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Works fully offline. No account needed. Your photo stays on this device unless you
          choose to save it.
        </p>
      </div>

      <div
        className="flex aspect-square w-full items-center justify-center overflow-hidden rounded-lg border"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
      >
        {previewUrl ? (
          // eslint-disable-next-line @next/next/no-img-element
          <img src={previewUrl} alt="Selected specimen" className="h-full w-full object-cover" />
        ) : (
          <p className="px-6 text-center text-sm" style={{ color: "var(--color-text-muted)" }}>
            Take a photo or choose one from your library to identify.
          </p>
        )}
      </div>

      <input
        ref={fileInputRef}
        type="file"
        accept="image/*"
        capture="environment"
        className="hidden"
        onChange={(e) => handleFile(e.target.files?.[0])}
      />

      <div className="flex gap-3">
        <button
          onClick={() => fileInputRef.current?.click()}
          className="flex-1 rounded-full border px-4 py-3 text-sm font-semibold"
          style={{ borderColor: "var(--color-border)" }}
        >
          {previewUrl ? "Choose a different photo" : "Take or choose a photo"}
        </button>
        <button
          onClick={runMockIdentify}
          disabled={!previewUrl || isIdentifying}
          className="flex-1 rounded-full px-4 py-3 text-sm font-semibold disabled:opacity-40"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          {isIdentifying ? "Identifying…" : "Identify"}
        </button>
      </div>

      {outcome && (
        <div className="flex flex-col gap-3">
          <IdResultCard species={outcome.species} confidence={outcome.confidence} />

          {savedSyncState === null ? (
            <>
              {useServerSync && (
                <div className="flex flex-col gap-3 rounded-lg border p-4" style={{ borderColor: "var(--color-border)" }}>
                  <label className="flex flex-col gap-1 text-sm font-medium">
                    Location (optional)
                    <input
                      value={locationName}
                      onChange={(e) => setLocationName(e.target.value)}
                      placeholder="e.g. Cedar Ridge Park"
                      className="rounded border px-3 py-2 text-sm font-normal"
                      style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
                    />
                  </label>
                  <label className="flex flex-col gap-1 text-sm font-medium">
                    Notes (optional)
                    <textarea
                      value={notes}
                      onChange={(e) => setNotes(e.target.value)}
                      rows={2}
                      className="rounded border px-3 py-2 text-sm font-normal"
                      style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
                    />
                  </label>
                  <label className="flex items-center gap-2 text-sm font-medium">
                    <input
                      type="checkbox"
                      checked={!isWild}
                      onChange={(e) => setIsWild(!e.target.checked)}
                    />
                    Captive / cultivated (not a wild specimen)
                  </label>
                </div>
              )}
              <button
                onClick={saveToObservations}
                className="rounded-full border px-4 py-3 text-sm font-semibold"
                style={{ borderColor: "var(--color-border)" }}
              >
                Save to My Observations
              </button>
            </>
          ) : (
            <div
              className="flex items-center justify-between rounded-lg border px-4 py-3 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
            >
              <span className="font-medium">
                {savedSyncState === "queued" && "Saved locally — queued to sync"}
                {savedSyncState === "uploading" && "Syncing…"}
                {savedSyncState === "confirmed" && "Saved and synced"}
                {savedSyncState === "failed" && "Sync failed — will retry from My Observations"}
              </span>
              <Link href="/observations" className="font-semibold" style={{ color: "var(--color-accent)" }}>
                View →
              </Link>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
