"use client";

import { useMemo, useState } from "react";
import Link from "next/link";
import { generateMockQueue } from "@/lib/mock-queue";
import { TaxonBadge } from "@/components/taxon-badge";

type Decision = "agreed" | "skipped";

export default function IdentifyPage() {
  const queue = useMemo(() => generateMockQueue(), []);
  const [index, setIndex] = useState(0);
  const [decisions, setDecisions] = useState<Record<string, Decision>>({});

  const current = queue[index];
  const done = index >= queue.length;
  const agreedCount = Object.values(decisions).filter((d) => d === "agreed").length;

  const advance = (decision: Decision) => {
    if (!current) return;
    setDecisions((prev) => ({ ...prev, [current.id]: decision }));
    setIndex((i) => i + 1);
  };

  return (
    <div className="mx-auto flex w-full max-w-xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="font-display text-2xl font-semibold">Identify queue</h1>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Help confirm identifications from the community.
          </p>
        </div>
        {!done && (
          <span className="shrink-0 text-sm font-medium" style={{ color: "var(--color-text-muted)" }}>
            {index + 1} / {queue.length}
          </span>
        )}
      </div>

      {done ? (
        <div
          className="rounded-lg border p-8 text-center"
          style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
        >
          <p className="font-semibold">Queue complete</p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            You agreed with {agreedCount} of {queue.length} suggested IDs.
          </p>
          <Link
            href="/"
            className="mt-4 inline-block rounded-full px-4 py-2 text-sm font-semibold"
            style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
          >
            Back to home
          </Link>
        </div>
      ) : (
        <div
          className="overflow-hidden rounded-lg border"
          style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
        >
          <div
            className="flex aspect-[4/3] w-full items-center justify-center text-sm"
            style={{ background: "var(--color-bg)", color: "var(--color-text-muted)" }}
          >
            Photo placeholder
          </div>
          <div className="p-4">
            <div className="flex items-center justify-between">
              <span
                className="rounded-full px-2.5 py-1 text-xs font-semibold"
                style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
              >
                {Math.round(current.suggestedConfidence * 100)}% CV confidence
              </span>
              <TaxonBadge group={current.species.taxonGroup} />
            </div>

            <h2 className="font-display mt-3 text-xl font-semibold">{current.species.commonName}</h2>
            <p className="text-sm italic" style={{ color: "var(--color-text-muted)" }}>
              {current.species.scientificName}
            </p>
            <p className="mt-2 text-xs" style={{ color: "var(--color-text-muted)" }}>
              Observed by {current.observerName} · {current.place}
            </p>

            <div className="mt-4 flex gap-3">
              <button
                onClick={() => advance("skipped")}
                className="flex-1 rounded-full border px-4 py-2.5 text-sm font-semibold"
                style={{ borderColor: "var(--color-border)" }}
              >
                Skip
              </button>
              <button
                onClick={() => advance("agreed")}
                className="flex-1 rounded-full px-4 py-2.5 text-sm font-semibold"
                style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
              >
                Agree
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
