"use client";

import { useCallback, useEffect, useState } from "react";
import Link from "next/link";
import { listObservations } from "@/lib/observations";
import { fetchServerObservations } from "@/lib/api-observations";
import { MOCK_BADGES } from "@/lib/mock-badges";
import { useMode } from "@/lib/mode-context";
import { useAuth } from "@/lib/auth-context";

export default function MePage() {
  const { mode } = useMode();
  const { user, loading: authLoading, logOut } = useAuth();
  const [taxonSlugs, setTaxonSlugs] = useState<string[]>([]);

  const useServer = mode === "naturalist" && Boolean(user);

  const loadStats = useCallback(async () => {
    if (useServer) {
      const observations = await fetchServerObservations();
      setTaxonSlugs(observations.map((o) => o.taxonSlug));
    } else {
      setTaxonSlugs(listObservations().map((o) => o.taxonSlug));
    }
  }, [useServer]);

  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect
    loadStats();
  }, [loadStats]);

  const speciesCount = new Set(taxonSlugs).size;
  const earnedBadges = MOCK_BADGES.filter((b) => b.earned);

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div className="flex items-center gap-4">
        <div
          className="flex h-16 w-16 shrink-0 items-center justify-center rounded-full text-xl font-semibold"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          {user ? user.email[0]?.toUpperCase() : "?"}
        </div>
        <div className="min-w-0 flex-1">
          <h1 className="font-display truncate text-2xl font-semibold">
            {user ? user.email : "Guest"}
          </h1>
          <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
            {user
              ? "Signed in — your Naturalist Mode observations sync to this account."
              : "Not signed in — your collection stays on this device."}
          </p>
        </div>
        {!authLoading &&
          (user ? (
            <button
              onClick={logOut}
              className="shrink-0 rounded-full border px-3 py-1.5 text-xs font-semibold"
              style={{ borderColor: "var(--color-border)" }}
            >
              Sign out
            </button>
          ) : (
            mode === "naturalist" && (
              <Link
                href="/login"
                className="shrink-0 rounded-full px-3 py-1.5 text-xs font-semibold"
                style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
              >
                Sign in
              </Link>
            )
          ))}
      </div>

      <div className="grid grid-cols-3 gap-3">
        {[
          { label: "Observations", value: taxonSlugs.length },
          { label: "Species", value: speciesCount },
          { label: "Badges", value: `${earnedBadges.length}/${MOCK_BADGES.length}` },
        ].map((stat) => (
          <div
            key={stat.label}
            className="rounded-lg border p-3 text-center"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          >
            <p className="text-xl font-semibold">{stat.value}</p>
            <p className="text-xs" style={{ color: "var(--color-text-muted)" }}>
              {stat.label}
            </p>
          </div>
        ))}
      </div>

      <div>
        <h2 className="text-sm font-semibold uppercase tracking-wide" style={{ color: "var(--color-text-muted)" }}>
          Badges
        </h2>
        <div className="mt-2 grid grid-cols-2 gap-3 sm:grid-cols-3">
          {MOCK_BADGES.map((badge) => (
            <div
              key={badge.slug}
              className="rounded-lg border p-3"
              style={{
                borderColor: "var(--color-border)",
                background: "var(--color-surface)",
                opacity: badge.earned ? 1 : 0.45,
              }}
            >
              <p className="text-sm font-semibold">{badge.label}</p>
              <p className="mt-1 text-xs" style={{ color: "var(--color-text-muted)" }}>
                {badge.description}
              </p>
            </div>
          ))}
        </div>
      </div>

      <div className="flex gap-3">
        <Link
          href="/settings/lite-mode"
          className="rounded-full border px-4 py-2 text-sm font-semibold"
          style={{ borderColor: "var(--color-border)" }}
        >
          Lite Mode
        </Link>
        <Link
          href="/settings/data"
          className="rounded-full border px-4 py-2 text-sm font-semibold"
          style={{ borderColor: "var(--color-border)" }}
        >
          Data & export
        </Link>
      </div>
    </div>
  );
}
