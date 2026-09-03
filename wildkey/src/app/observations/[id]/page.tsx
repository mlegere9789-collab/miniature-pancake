"use client";

import { use, useCallback, useEffect, useState } from "react";
import Link from "next/link";
import {
  fetchObservationComments,
  fetchServerObservation,
  postObservationComment,
  updateServerObservationLicense,
  type ObservationComment,
  type ObservationWithGrade,
} from "@/lib/api-observations";
import { useAuth } from "@/lib/auth-context";
import type { CurrentUser } from "@/lib/auth-context";
import { QualityGradeBadge } from "@/components/quality-grade-badge";
import { LazyPhoto } from "@/components/lazy-photo";
import { OBSERVATION_LICENSES, LICENSE_LABELS, LICENSE_DESCRIPTIONS, type ObservationLicense } from "@/lib/observation-license";

export default function ObservationDetailPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const { id } = use(params);
  const { user: currentUser, loading: authLoading } = useAuth();

  const [observation, setObservation] = useState<ObservationWithGrade | null>(null);
  const [author, setAuthor] = useState<CurrentUser | null>(null);
  const [comments, setComments] = useState<ObservationComment[]>([]);
  const [notFoundOrDenied, setNotFoundOrDenied] = useState(false);
  const [draft, setDraft] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [flagReason, setFlagReason] = useState("");
  const [flagOpen, setFlagOpen] = useState(false);
  const [flagSubmitted, setFlagSubmitted] = useState(false);
  const [flagSubmitting, setFlagSubmitting] = useState(false);
  const [updatingLicense, setUpdatingLicense] = useState(false);

  const load = useCallback(async () => {
    const result = await fetchServerObservation(id);
    if (!result) {
      setNotFoundOrDenied(true);
      return;
    }
    setObservation(result.observation);
    setAuthor(result.author);
    setComments(await fetchObservationComments(id));
  }, [id]);

  useEffect(() => {
    if (authLoading) return;
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [authLoading, load]);

  const hasAgreed = comments.some((c) => c.kind === "agree" && c.userId === currentUser?.id);
  const isOwner = observation?.userId === currentUser?.id;

  const changeLicense = async (license: ObservationLicense) => {
    setUpdatingLicense(true);
    const ok = await updateServerObservationLicense(id, license);
    setUpdatingLicense(false);
    if (ok) setObservation((prev) => (prev ? { ...prev, license } : prev));
  };

  const submitComment = async (kind: "comment" | "agree") => {
    if (kind === "comment" && draft.trim().length === 0) return;
    setSubmitting(true);
    const comment = await postObservationComment(id, { body: draft.trim(), kind });
    setSubmitting(false);
    if (comment) {
      setComments((prev) => [...prev, comment]);
      if (kind === "comment") setDraft("");
      // Quality grade is computed server-side from agree counts — refetch
      // the observation so the badge reflects the new consensus.
      if (kind === "agree") {
        const result = await fetchServerObservation(id);
        if (result) setObservation(result.observation);
      }
    }
  };

  if (!authLoading && !currentUser) {
    return (
      <div className="mx-auto flex w-full max-w-xl flex-1 flex-col gap-4 px-4 py-12 text-center sm:px-6">
        <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
          Sign in to view and discuss this observation.
        </p>
        <Link
          href="/login"
          className="mx-auto rounded-full px-4 py-2 text-sm font-semibold"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          Sign in
        </Link>
      </div>
    );
  }

  if (notFoundOrDenied) {
    return (
      <div className="mx-auto flex w-full max-w-xl flex-1 flex-col gap-2 px-4 py-12 text-center sm:px-6">
        <p className="font-semibold">Observation not found.</p>
        <Link href="/observations" style={{ color: "var(--color-accent)" }}>
          Back to My Observations
        </Link>
      </div>
    );
  }

  const submitFlag = async () => {
    if (!flagReason.trim()) return;
    setFlagSubmitting(true);
    const res = await fetch(`/api/observations/${id}/flag`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ reason: flagReason.trim() }),
    });
    setFlagSubmitting(false);
    if (res.ok) {
      setFlagSubmitted(true);
      setFlagOpen(false);
    }
  };

  if (!observation) return null;

  return (
    <div className="mx-auto flex w-full max-w-xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div
        className="overflow-hidden rounded-lg border"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
      >
        <LazyPhoto
          src={observation.photoDataUrl}
          alt={observation.commonName}
          className="aspect-[4/3] w-full object-cover"
        />
        <div className="p-4">
          <div className="flex items-start justify-between gap-2">
            <Link href={`/species/${observation.taxonSlug}`} className="hover:underline">
              <h1 className="font-display text-2xl font-semibold">{observation.commonName}</h1>
            </Link>
            <QualityGradeBadge grade={observation.qualityGrade} />
          </div>
          <p className="text-sm italic" style={{ color: "var(--color-text-muted)" }}>
            {observation.scientificName}
          </p>
          <p className="mt-2 text-xs" style={{ color: "var(--color-text-muted)" }}>
            Observed by {author?.email ?? "unknown"} ·{" "}
            {new Date(observation.createdAt).toLocaleString()}
            {observation.locationName && ` · ${observation.locationName}`}
          </p>
          {!observation.isWild && (
            <span
              className="mt-2 inline-block rounded-full px-2.5 py-1 text-xs font-semibold"
              style={{ background: "var(--color-border)", color: "var(--color-text)" }}
            >
              Captive / cultivated
            </span>
          )}
          {observation.notes && (
            <p className="mt-2 text-sm">{observation.notes}</p>
          )}
          <div className="mt-3 flex items-center gap-2">
            {isOwner ? (
              <label className="flex items-center gap-1.5 text-xs" style={{ color: "var(--color-text-muted)" }}>
                License:
                <select
                  value={observation.license}
                  onChange={(e) => changeLicense(e.target.value as ObservationLicense)}
                  disabled={updatingLicense}
                  aria-label="Change this observation's license"
                  className="rounded border px-1.5 py-1 text-xs"
                  style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
                >
                  {OBSERVATION_LICENSES.map((l) => (
                    <option key={l} value={l}>
                      {LICENSE_LABELS[l]}
                    </option>
                  ))}
                </select>
              </label>
            ) : (
              <span
                className="text-xs"
                style={{ color: "var(--color-text-muted)" }}
                title={LICENSE_DESCRIPTIONS[observation.license]}
              >
                {LICENSE_LABELS[observation.license]}
              </span>
            )}
          </div>
        </div>
      </div>

      <div className="flex items-center justify-between">
        <h2 className="text-sm font-semibold uppercase tracking-wide" style={{ color: "var(--color-text-muted)" }}>
          Community ID ({observation.agreeCount} independent agree
          {observation.agreeCount === 1 ? "" : "s"})
        </h2>
        <button
          onClick={() => submitComment("agree")}
          disabled={hasAgreed || submitting}
          className="rounded-full border px-3 py-1.5 text-xs font-semibold disabled:opacity-40"
          style={{ borderColor: "var(--color-border)" }}
        >
          {hasAgreed ? "You agreed" : "Agree with this ID"}
        </button>
      </div>

      <ul className="flex flex-col gap-3">
        {comments.length === 0 && (
          <li className="text-sm" style={{ color: "var(--color-text-muted)" }}>
            No comments yet — be the first to weigh in.
          </li>
        )}
        {comments.map((c) => (
          <li
            key={c.id}
            className="rounded-lg border p-3 text-sm"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          >
            <div className="flex items-center justify-between">
              <span className="font-semibold">{c.userEmail}</span>
              {c.kind === "agree" && (
                <span
                  className="rounded-full px-2 py-0.5 text-xs font-semibold"
                  style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
                >
                  Agreed
                </span>
              )}
            </div>
            <p className="mt-1">{c.body}</p>
            <p className="mt-1 text-xs" style={{ color: "var(--color-text-muted)" }}>
              {new Date(c.createdAt).toLocaleString()}
            </p>
          </li>
        ))}
      </ul>

      <form
        onSubmit={(e) => {
          e.preventDefault();
          submitComment("comment");
        }}
        className="flex gap-2"
      >
        <input
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          aria-label="Add a comment"
          placeholder="Add a comment…"
          className="flex-1 rounded border px-3 py-2 text-sm"
          style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
        />
        <button
          type="submit"
          disabled={submitting || draft.trim().length === 0}
          className="rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          Post
        </button>
      </form>

      <div className="border-t pt-4" style={{ borderColor: "var(--color-border)" }}>
        {flagSubmitted ? (
          <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
            Flagged for curator review. Thank you.
          </p>
        ) : flagOpen ? (
          <div className="flex flex-col gap-2">
            <input
              value={flagReason}
              onChange={(e) => setFlagReason(e.target.value)}
              aria-label="Reason for flagging this observation"
              placeholder="Why should a curator look at this? (required)"
              className="rounded border px-3 py-2 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
            />
            <div className="flex gap-2">
              <button
                onClick={submitFlag}
                disabled={flagSubmitting || !flagReason.trim()}
                className="rounded-full px-4 py-2 text-xs font-semibold disabled:opacity-40"
                style={{ background: "var(--color-danger)", color: "var(--color-accent-contrast)" }}
              >
                {flagSubmitting ? "Submitting…" : "Submit flag"}
              </button>
              <button
                onClick={() => setFlagOpen(false)}
                className="rounded-full border px-4 py-2 text-xs font-semibold"
                style={{ borderColor: "var(--color-border)" }}
              >
                Cancel
              </button>
            </div>
          </div>
        ) : (
          <button
            onClick={() => setFlagOpen(true)}
            className="text-xs font-semibold"
            style={{ color: "var(--color-danger)" }}
          >
            Flag for curator review
          </button>
        )}
      </div>
    </div>
  );
}
