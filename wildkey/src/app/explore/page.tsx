"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import { useRouter } from "next/navigation";
import Link from "next/link";
import { MOCK_SPECIES, getMockSpecies } from "@/lib/mock-species";
import { TAXON_GROUPS, taxonColor, type TaxonGroup } from "@/lib/taxon";
import { TaxonBadge } from "@/components/taxon-badge";
import { MapView, type MapMarker, type BasemapKind } from "@/components/map-view";
import { useAuth } from "@/lib/auth-context";

type ViewMode = "grid" | "list" | "map";

type Bounds = { minLat: number; maxLat: number; minLng: number; maxLng: number };

const WORLD_BOUNDS: Bounds = { minLat: -60, maxLat: 75, minLng: -170, maxLng: 170 };

export default function ExplorePage() {
  const router = useRouter();
  const { user, loading: authLoading } = useAuth();
  const [view, setView] = useState<ViewMode>("grid");
  const [activeGroups, setActiveGroups] = useState<Set<TaxonGroup>>(new Set());
  const [markers, setMarkers] = useState<MapMarker[]>([]);
  const [basemap, setBasemap] = useState<BasemapKind>("street");

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

  const loadMapMarkers = useCallback(
    async (bounds: Bounds) => {
      if (!user) return;
      const params = new URLSearchParams({
        minLat: String(bounds.minLat),
        maxLat: String(bounds.maxLat),
        minLng: String(bounds.minLng),
        maxLng: String(bounds.maxLng),
      });
      const res = await fetch(`/api/observations/near?${params}`);
      if (!res.ok) return;
      const data = await res.json();
      setMarkers(
        (data.observations ?? [])
          .filter((o: { lat: number | null }) => o.lat !== null)
          .map((o: { id: string; lat: number; lng: number; taxonSlug: string; commonName: string }) => ({
            id: o.id,
            lat: o.lat,
            lng: o.lng,
            color: taxonColor(getMockSpecies(o.taxonSlug)?.taxonGroup ?? "plant"),
            label: o.commonName,
          })),
      );
    },
    [user],
  );

  useEffect(() => {
    if (view === "map" && !authLoading && user) {
      // eslint-disable-next-line react-hooks/set-state-in-effect
      loadMapMarkers(WORLD_BOUNDS);
    }
  }, [view, authLoading, user, loadMapMarkers]);

  return (
    <div className="mx-auto flex w-full max-w-4xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div className="flex items-start justify-between gap-4">
        <div>
          <h1 className="font-display text-2xl font-semibold">Explore</h1>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Browse sightings by taxon group, or switch to Map for real observation locations.
          </p>
        </div>
        <div
          role="tablist"
          aria-label="View"
          className="inline-flex shrink-0 rounded-full border p-1 text-sm"
          style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
        >
          {(["grid", "list", "map"] as const).map((mode) => (
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

      {view !== "map" && (
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
      )}

      {view === "map" ? (
        !authLoading && !user ? (
          <div
            className="rounded-lg border p-8 text-center text-sm"
            style={{ borderColor: "var(--color-border)", color: "var(--color-text-muted)" }}
          >
            Sign in to a Naturalist Mode account to see real observation locations on the map.{" "}
            <Link href="/login" style={{ color: "var(--color-accent)" }}>
              Sign in
            </Link>
          </div>
        ) : (
          <div className="flex flex-col gap-2">
            <div
              role="tablist"
              aria-label="Basemap"
              className="inline-flex w-fit shrink-0 rounded-full border p-1 text-xs"
              style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
            >
              {(["street", "satellite"] as const).map((kind) => (
                <button
                  key={kind}
                  role="tab"
                  aria-selected={basemap === kind}
                  onClick={() => setBasemap(kind)}
                  className="rounded-full px-3 py-1 font-medium capitalize transition-colors"
                  style={{
                    background: basemap === kind ? "var(--color-accent)" : "transparent",
                    color: basemap === kind ? "var(--color-accent-contrast)" : "var(--color-text-muted)",
                  }}
                >
                  {kind}
                </button>
              ))}
            </div>
            <MapView
              markers={markers}
              basemap={basemap}
              onMoveEnd={loadMapMarkers}
              onMarkerClick={(id) => router.push(`/observations/${id}`)}
              className="h-[60vh] w-full overflow-hidden rounded-lg border"
            />
            <p className="text-xs" style={{ color: "var(--color-text-muted)" }}>
              {markers.length} observation{markers.length === 1 ? "" : "s"} in view. Sensitive
              species show a coarse location, not their exact site.
            </p>
          </div>
        )
      ) : results.length === 0 ? (
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
