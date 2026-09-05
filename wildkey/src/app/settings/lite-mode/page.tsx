"use client";

import { useLiteMode } from "@/lib/lite-mode-context";

export default function LiteModePage() {
  const { liteMode, setLiteMode } = useLiteMode();

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Lite Mode</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          A first-class mode for old devices and slow connections — not a stripped
          &ldquo;lesser&rdquo; experience.
        </p>
      </div>

      <div
        className="flex items-center justify-between rounded-lg border p-4"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
      >
        <div>
          <p className="font-semibold">Lite Mode</p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Photos never load automatically — every photo across the app becomes a
            &ldquo;tap to load&rdquo; placeholder until you ask for it.
          </p>
        </div>
        <button
          role="switch"
          aria-checked={liteMode}
          onClick={() => setLiteMode(!liteMode)}
          className="relative h-7 w-12 shrink-0 rounded-full transition-colors"
          style={{ background: liteMode ? "var(--color-accent)" : "var(--color-border)" }}
        >
          <span
            className="absolute top-0.5 h-6 w-6 rounded-full bg-white transition-transform"
            style={{ transform: liteMode ? "translateX(22px)" : "translateX(2px)" }}
          />
        </button>
      </div>

      <div
        className="rounded-lg border p-4"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
      >
        <p className="text-xs font-semibold uppercase tracking-wide" style={{ color: "var(--color-text-muted)" }}>
          Not built yet
        </p>
        <ul className="mt-2 list-disc space-y-1 pl-5 text-sm">
          <li>No background sync, no live map tiles, explicit &ldquo;sync now&rdquo; only</li>
          <li>CV suggestions off by default</li>
          <li>Verified performance on a 6-year-old low-end Android device on throttled 2G</li>
        </ul>
      </div>
    </div>
  );
}
