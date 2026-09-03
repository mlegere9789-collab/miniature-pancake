"use client";

import { useEffect, useRef, useState } from "react";
import Link from "next/link";
import { IdResultCard } from "@/components/id-result-card";
import { getMockSpecies, type Species } from "@/lib/mock-species";
import { runMockSync, saveObservation, type SyncState } from "@/lib/observations";
import { createServerObservation } from "@/lib/api-observations";
import { useMode } from "@/lib/mode-context";
import { useAuth } from "@/lib/auth-context";
import { useLocale } from "@/lib/locale-context";
import { identifyImage, warmUpModel, CvModelError } from "@/lib/cv-model";
import {
  OBSERVATION_LICENSES,
  DEFAULT_OBSERVATION_LICENSE,
  LICENSE_LABELS,
  LICENSE_DESCRIPTIONS,
  type ObservationLicense,
} from "@/lib/observation-license";

type IdOutcome = { species: Species | null; confidence: number; rawLabel: string };

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
  const { t } = useLocale();
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [previewUrl, setPreviewUrl] = useState<string | null>(null);
  const [outcome, setOutcome] = useState<IdOutcome | null>(null);
  const [isIdentifying, setIsIdentifying] = useState(false);
  const [savedSyncState, setSavedSyncState] = useState<SyncState | null>(null);
  const [locationName, setLocationName] = useState("");
  const [notes, setNotes] = useState("");
  const [isWild, setIsWild] = useState(true);
  const [license, setLicense] = useState<ObservationLicense>(DEFAULT_OBSERVATION_LICENSE);
  const [coords, setCoords] = useState<{ lat: number; lng: number } | null>(null);
  const [locating, setLocating] = useState(false);
  const [locationError, setLocationError] = useState<string | null>(null);
  const [identifyError, setIdentifyError] = useState<string | null>(null);
  const useServerSync = mode === "naturalist" && Boolean(user);

  // Kick off the model download as soon as this screen mounts so it's
  // likely already cached by the time the user has taken/chosen a photo.
  useEffect(() => {
    warmUpModel();
  }, []);

  const handleFile = async (file: File | undefined) => {
    if (!file) return;
    setOutcome(null);
    setSavedSyncState(null);
    setLocationName("");
    setNotes("");
    setIsWild(true);
    setLicense(DEFAULT_OBSERVATION_LICENSE);
    setCoords(null);
    setLocationError(null);
    setIdentifyError(null);
    setPreviewUrl(await readFileAsDataUrl(file));
  };

  const useMyLocation = () => {
    if (!navigator.geolocation) {
      setLocationError("Geolocation isn't available in this browser.");
      return;
    }
    setLocating(true);
    setLocationError(null);
    navigator.geolocation.getCurrentPosition(
      (position) => {
        setCoords({ lat: position.coords.latitude, lng: position.coords.longitude });
        setLocating(false);
      },
      (err) => {
        setLocationError(err.message || "Couldn't get your location.");
        setLocating(false);
      },
      { timeout: 10000 },
    );
  };

  const runIdentify = async () => {
    if (!previewUrl) return;
    setIsIdentifying(true);
    setIdentifyError(null);
    try {
      const img = new Image();
      img.crossOrigin = "anonymous";
      await new Promise<void>((resolve, reject) => {
        img.onload = () => resolve();
        img.onerror = () => reject(new Error("Couldn't load the photo for identification."));
        img.src = previewUrl;
      });

      const result = await identifyImage(img);
      const species = result.speciesSlug ? (getMockSpecies(result.speciesSlug) ?? null) : null;
      setOutcome({ species, confidence: result.confidence, rawLabel: result.rawLabel });
    } catch (err) {
      const message =
        err instanceof CvModelError
          ? err.message
          : "Something went wrong running the identification model.";
      setIdentifyError(message);
    } finally {
      setIsIdentifying(false);
    }
  };

  const saveToObservations = async () => {
    if (!outcome || !outcome.species || !previewUrl) return;

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
        lat: coords?.lat ?? null,
        lng: coords?.lng ?? null,
        license,
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
        <h1 className="font-display text-2xl font-semibold">{t("camera.title")}</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          {t("camera.subtitle")}
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
            {t("camera.choosePhotoPlaceholder")}
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
          {previewUrl ? t("camera.chooseDifferentPhoto") : t("camera.takeOrChoosePhoto")}
        </button>
        <button
          onClick={runIdentify}
          disabled={!previewUrl || isIdentifying}
          className="flex-1 rounded-full px-4 py-3 text-sm font-semibold disabled:opacity-40"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          {isIdentifying ? t("camera.identifying") : t("camera.identify")}
        </button>
      </div>

      {identifyError && (
        <p className="text-sm font-medium" style={{ color: "var(--color-danger)" }}>
          {identifyError}
        </p>
      )}

      {outcome && (
        <div className="flex flex-col gap-3">
          <IdResultCard species={outcome.species} confidence={outcome.confidence} rawLabel={outcome.rawLabel} />

          {savedSyncState === null && outcome.species && (
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
                  <div className="flex items-center gap-2 text-xs">
                    <button
                      type="button"
                      onClick={useMyLocation}
                      disabled={locating}
                      className="rounded-full border px-3 py-1.5 font-semibold disabled:opacity-40"
                      style={{ borderColor: "var(--color-border)" }}
                    >
                      {locating ? "Locating…" : coords ? "Location attached ✓" : "Use my location"}
                    </button>
                    {locationError && (
                      <span style={{ color: "var(--color-danger)" }}>{locationError}</span>
                    )}
                  </div>
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
                  <label className="flex flex-col gap-1 text-sm font-medium">
                    License
                    <select
                      value={license}
                      onChange={(e) => setLicense(e.target.value as ObservationLicense)}
                      className="rounded border px-3 py-2 text-sm font-normal"
                      style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
                    >
                      {OBSERVATION_LICENSES.map((l) => (
                        <option key={l} value={l}>
                          {LICENSE_LABELS[l]}
                        </option>
                      ))}
                    </select>
                    <span className="text-xs font-normal" style={{ color: "var(--color-text-muted)" }}>
                      {LICENSE_DESCRIPTIONS[license]}
                    </span>
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
          )}
          {savedSyncState !== null && (
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
