"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import Link from "next/link";
import { generateMockQueue, type QueueItem } from "@/lib/mock-queue";
import { TaxonBadge } from "@/components/taxon-badge";
import { useMode } from "@/lib/mode-context";
import { useAuth } from "@/lib/auth-context";
import {
  fetchObservationsNeedingId,
  postObservationComment,
  type ObservationNeedingId,
} from "@/lib/api-observations";
import { getMockSpecies } from "@/lib/mock-species";

type Decision = "agreed" | "skipped";

export default function IdentifyPage() {
  const { mode } = useMode();
  const { user, loading: authLoading } = useAuth();
  const useRealQueue = mode === "naturalist" && Boolean(user);

  const mockQueue = useMemo(() => generateMockQueue(), []);
  const [realQueue, setRealQueue] = useState<ObservationNeedingId[] | null>(null);
  const [index, setIndex] = useState(0);
  const [decisions, setDecisions] = useState<Record<string, Decision>>({});
  const [submitting, setSubmitting] = useState(false);

  const loadRealQueue = useCallback(async () => {
    setRealQueue(await fetchObservationsNeedingId());
  }, []);

  useEffect(() => {
    if (authLoading || !useRealQueue) return;
    // Resetting queue position for a freshly (re)loaded real queue, and
    // fetching it from the server on mount/mode change — not deriving
    // state from props/state, so react-hooks/set-state-in-effect doesn't
    // apply here.
    // eslint-disable-next-line react-hooks/set-state-in-effect
    setIndex(0);
    setDecisions({});
    loadRealQueue();
  }, [authLoading, useRealQueue, loadRealQueue]);

  const queue: (QueueItem | ObservationNeedingId)[] = useRealQueue ? realQueue ?? [] : mockQueue;
  const current = queue[index];
  const total = queue.length;
  const done = (useRealQueue ? realQueue !== null : true) && index >= total;
  const agreedCount = Object.values(decisions).filter((d) => d === "agreed").length;

  const advance = async (decision: Decision) => {
    if (!current) return;
    if (useRealQueue && decision === "agreed" && "observerEmail" in current) {
      setSubmitting(true);
      await postObservationComment(current.id, { body: "", kind: "agree" });
      setSubmitting(false);
    }
    setDecisions((prev) => ({ ...prev, [current.id]: decision }));
    setIndex((i) => i + 1);
  };

  if (useRealQueue && realQueue === null) return null;

  return (
    <div className="mx-auto flex w-full max-w-xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="font-display text-2xl font-semibold">Identify queue</h1>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            {useRealQueue
              ? "Real observations from other Naturalist Mode users, still short of Research Grade."
              : "Sign in to Naturalist Mode to help identify real community observations. This preview uses sample data."}
          </p>
        </div>
        {!done && total > 0 && (
          <span className="shrink-0 text-sm font-medium" style={{ color: "var(--color-text-muted)" }}>
            {index + 1} / {total}
          </span>
        )}
      </div>

      {total === 0 ? (
        <div
          className="rounded-lg border p-8 text-center text-sm"
          style={{ borderColor: "var(--color-border)", color: "var(--color-text-muted)" }}
        >
          Nothing needs ID right now — check back later.
        </div>
      ) : done ? (
        <div
          className="rounded-lg border p-8 text-center"
          style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
        >
          <p className="font-semibold">Queue complete</p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            You agreed with {agreedCount} of {total} suggested IDs.
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
        current && (
          <IdentifyCard
            key={current.id}
            item={current}
            submitting={submitting}
            onSkip={() => advance("skipped")}
            onAgree={() => advance("agreed")}
          />
        )
      )}
    </div>
  );
}

function IdentifyCard({
  item,
  submitting,
  onSkip,
  onAgree,
}: {
  item: QueueItem | ObservationNeedingId;
  submitting: boolean;
  onSkip: () => void;
  onAgree: () => void;
}) {
  const isReal = "observerEmail" in item;
  const commonName = isReal ? item.commonName : item.species.commonName;
  const scientificName = isReal ? item.scientificName : item.species.scientificName;
  const confidence = isReal ? item.confidence : item.suggestedConfidence;
  const observerLabel = isReal ? item.observerEmail : item.observerName;
  const place = isReal ? null : item.place;
  const taxonGroup = isReal ? getMockSpecies(item.taxonSlug)?.taxonGroup : item.species.taxonGroup;
  const photoUrl = isReal ? item.photoDataUrl : null;

  return (
    <div
      className="overflow-hidden rounded-lg border"
      style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
    >
      {photoUrl ? (
        // eslint-disable-next-line @next/next/no-img-element
        <img src={photoUrl} alt={commonName} className="aspect-[4/3] w-full object-cover" />
      ) : (
        <div
          className="flex aspect-[4/3] w-full items-center justify-center text-sm"
          style={{ background: "var(--color-bg)", color: "var(--color-text-muted)" }}
        >
          Photo placeholder
        </div>
      )}
      <div className="p-4">
        <div className="flex items-center justify-between">
          <span
            className="rounded-full px-2.5 py-1 text-xs font-semibold"
            style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
          >
            {Math.round(confidence * 100)}% CV confidence
          </span>
          {taxonGroup && <TaxonBadge group={taxonGroup} />}
        </div>

        <h2 className="font-display mt-3 text-xl font-semibold">{commonName}</h2>
        <p className="text-sm italic" style={{ color: "var(--color-text-muted)" }}>
          {scientificName}
        </p>
        <p className="mt-2 text-xs" style={{ color: "var(--color-text-muted)" }}>
          Observed by {observerLabel}
          {place ? ` · ${place}` : ""}
        </p>

        <div className="mt-4 flex gap-3">
          <button
            onClick={onSkip}
            disabled={submitting}
            className="flex-1 rounded-full border px-4 py-2.5 text-sm font-semibold disabled:opacity-40"
            style={{ borderColor: "var(--color-border)" }}
          >
            Skip
          </button>
          <button
            onClick={onAgree}
            disabled={submitting}
            className="flex-1 rounded-full px-4 py-2.5 text-sm font-semibold disabled:opacity-40"
            style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
          >
            {submitting ? "Saving…" : "Agree"}
          </button>
        </div>
      </div>
    </div>
  );
}
