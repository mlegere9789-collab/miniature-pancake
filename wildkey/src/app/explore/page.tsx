"use client";

import { useMemo, useState } from "react";
import Link from "next/link";
import { MOCK_SPECIES } from "@/lib/mock-species";
import { TAXON_GROUPS, type TaxonGroup } from "@/lib/taxon";
import { TaxonBadge } from "@/components/taxon-badge";

type ViewMode = "grid" | "list";

export default function ExplorePage() {
  const [view, setView] = useState<ViewMode>("grid");
  const [activeGroups, setActiveGroups] = useState<Set<TaxonGroup>>(new Set());

  const toggleGroup = (group: TaxonGroup) => {
    setActiveGroups((prev) => {
      const next = new Set(prev);
      if (next.has(group)) next.delete(group);
      else next.add(group);
      return next;
    });
  };

  const results = useMemo(() => {
    if (activeGroups.size === 0) return MOCK_SPECIES;
    return MOCK_SPECIES.filter((s) => activeGroups.has(s.taxonGroup));
  }, [activeGroups]);

  return (
    <div className="mx-auto flex w-full max-w-4xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div className="flex items-start justify-between gap-4">
        <div>
          <h1 className="font-display text-2xl font-semibold">Explore</h1>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Browse sightings by taxon group. Map view and place/date filters come next — this is
            the grid/list layer of the Explore surface.
          </p>
        </div>
        <div
          role="tablist"
          aria-label="View"
          className="inline-flex shrink-0 rounded-full border p-1 text-sm"
          style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
        >
          {(["grid", "list"] as const).map((mode) => (
            <button
              key={mode}
              role="tab"
              aria-selected={view === mode}
              onClick={() => setView(mode)}
              className="rounded-full px-3 py-1.5 font-medium capitalize transition-colors"
              style={{
                background: view === mode ? "var(--color-accent)" : "transparent",
                color: view === mode ? "var(--color-accent-contrast)" : "var(--color-text-muted)",
              }}
            >
              {mode}
            </button>
          ))}
        </div>
      </div>

      <div className="flex flex-wrap gap-2">
        {TAXON_GROUPS.map((group) => {
          const active = activeGroups.has(group.value);
          return (
            <button
              key={group.value}
              onClick={() => toggleGroup(group.value)}
              className="inline-flex items-center gap-1.5 rounded-full border px-3 py-1.5 text-xs font-medium transition-colors"
              style={{
                borderColor: active ? group.color : "var(--color-border)",
                background: active ? "color-mix(in srgb, var(--color-accent) 12%, transparent)" : "transparent",
                color: active ? "var(--color-text)" : "var(--color-text-muted)",
              }}
            >
              <span aria-hidden className="h-2 w-2 rounded-full" style={{ background: group.color }} />
              {group.label}
            </button>
          );
        })}
      </div>

      {results.length === 0 ? (
        <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
          No species match these filters yet.
        </p>
      ) : view === "grid" ? (
        <div className="grid grid-cols-2 gap-4 sm:grid-cols-3">
          {results.map((s) => (
            <Link
              key={s.slug}
              href={`/species/${s.slug}`}
              className="overflow-hidden rounded-lg border"
              style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
            >
              <div
                className="flex aspect-square items-center justify-center text-xs"
                style={{ background: "var(--color-bg)", color: "var(--color-text-muted)" }}
              >
                Photo
              </div>
              <div className="p-2.5">
                <p className="truncate text-sm font-semibold">{s.commonName}</p>
                <TaxonBadge group={s.taxonGroup} />
              </div>
            </Link>
          ))}
        </div>
      ) : (
        <ul className="flex flex-col divide-y" style={{ borderColor: "var(--color-border)" }}>
          {results.map((s) => (
            <li key={s.slug}>
              <Link
                href={`/species/${s.slug}`}
                className="flex items-center gap-3 py-3"
                style={{ borderColor: "var(--color-border)" }}
              >
                <div
                  className="flex h-12 w-12 shrink-0 items-center justify-center rounded-md text-[10px]"
                  style={{ background: "var(--color-surface)", border: "1px solid var(--color-border)", color: "var(--color-text-muted)" }}
                >
                  Photo
                </div>
                <div className="min-w-0 flex-1">
                  <p className="truncate font-semibold">{s.commonName}</p>
                  <p className="truncate text-xs italic" style={{ color: "var(--color-text-muted)" }}>
                    {s.scientificName}
                  </p>
                </div>
                <TaxonBadge group={s.taxonGroup} />
                <span className="shrink-0 text-xs" style={{ color: "var(--color-text-muted)" }}>
                  {s.observationCount.toLocaleString()}
                </span>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
