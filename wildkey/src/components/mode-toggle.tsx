"use client";

import { useMode } from "@/lib/mode-context";

export function ModeToggle() {
  const { mode, setMode } = useMode();

  return (
    <div
      role="tablist"
      aria-label="App mode"
      className="inline-flex rounded-full border p-1 text-sm"
      style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
    >
      {(
        [
          { value: "quick-id", label: "Quick ID" },
          { value: "naturalist", label: "Naturalist" },
        ] as const
      ).map((option) => {
        const active = mode === option.value;
        return (
          <button
            key={option.value}
            role="tab"
            aria-selected={active}
            onClick={() => setMode(option.value)}
            className="rounded-full px-3 py-1.5 font-medium transition-colors"
            style={{
              background: active ? "var(--color-accent)" : "transparent",
              color: active ? "var(--color-accent-contrast)" : "var(--color-text-muted)",
            }}
          >
            {option.label}
          </button>
        );
      })}
    </div>
  );
}
