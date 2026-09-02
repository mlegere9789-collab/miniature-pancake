"use client";

import Link from "next/link";
import { useMode } from "@/lib/mode-context";

export default function Home() {
  const { mode } = useMode();
  const isQuickId = mode === "quick-id";

  return (
    <div className="mx-auto flex w-full max-w-3xl flex-1 flex-col gap-8 px-4 py-12 sm:px-6">
      <div>
        <p className="text-sm font-medium" style={{ color: "var(--color-accent)" }}>
          {isQuickId ? "Quick ID Mode" : "Naturalist Mode"}
        </p>
        <h1 className="font-display mt-2 text-4xl font-semibold leading-tight">
          One app. Works everywhere.
          <br />
          Never loses your data.
        </h1>
        <p className="mt-4 max-w-xl text-lg" style={{ color: "var(--color-text-muted)" }}>
          {isQuickId
            ? "Point your camera at anything wild and find out what it is — no account, no ads, fully offline. When we're not sure, we say so."
            : "Log full observations, get community identifications, and contribute research-grade data — with a sync you can always see and trust."}
        </p>
      </div>

      <div className="flex flex-wrap gap-3">
        <Link
          href={isQuickId ? "/camera" : "/observations"}
          className="rounded-full px-5 py-3 text-sm font-semibold"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          {isQuickId ? "Identify something" : "Log an observation"}
        </Link>
        <Link
          href="/explore"
          className="rounded-full border px-5 py-3 text-sm font-semibold"
          style={{ borderColor: "var(--color-border)" }}
        >
          Explore sightings near you
        </Link>
      </div>

      <div className="grid gap-4 sm:grid-cols-2">
        {(isQuickId
          ? [
              { title: "No forced login", body: "Full identification with zero account required." },
              { title: "Fully offline", body: "On-device model — works with no signal, in the field." },
              { title: "Honest uncertainty", body: "We show a genus/family fallback instead of guessing." },
              { title: "No ads, ever", body: "No ads, no purchases, no paywalls on any feature." },
            ]
          : [
              { title: "Visible CV confidence", body: "Every suggestion shows its confidence % up front." },
              { title: "Community ID", body: "Agree, disagree, and drill into taxonomy together." },
              { title: "Research-grade pipeline", body: "Two agreeing IDs at genus-or-finer, same as iNat." },
              { title: "Bulletproof sync", body: "Queued → Uploading → Confirmed, with a visible retry log." },
            ]
        ).map((item) => (
          <div
            key={item.title}
            className="rounded-lg border p-4"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
          >
            <h2 className="font-semibold">{item.title}</h2>
            <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
              {item.body}
            </p>
          </div>
        ))}
      </div>
    </div>
  );
}
