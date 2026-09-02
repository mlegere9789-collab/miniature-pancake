"use client";

import { useRef, useState } from "react";
import { IdResultCard } from "@/components/id-result-card";
import { MOCK_SPECIES, type Species } from "@/lib/mock-species";

type IdOutcome = { species: Species; confidence: number };

export default function CameraPage() {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [previewUrl, setPreviewUrl] = useState<string | null>(null);
  const [outcome, setOutcome] = useState<IdOutcome | null>(null);
  const [isIdentifying, setIsIdentifying] = useState(false);

  const handleFile = (file: File | undefined) => {
    if (!file) return;
    setOutcome(null);
    setPreviewUrl(URL.createObjectURL(file));
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

      {outcome && <IdResultCard species={outcome.species} confidence={outcome.confidence} />}
    </div>
  );
}
