"use client";

import { useCallback, useEffect, useState } from "react";
import Link from "next/link";
import { fetchActivity, type ActivityItem } from "@/lib/api-observations";
import { useAuth } from "@/lib/auth-context";

export default function ActivityPage() {
  const { user, loading: authLoading } = useAuth();
  const [activity, setActivity] = useState<ActivityItem[] | null>(null);

  const load = useCallback(async () => {
    if (!user) return;
    setActivity(await fetchActivity());
  }, [user]);

  useEffect(() => {
    if (authLoading) return;
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [authLoading, load]);

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Activity</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Comments and agrees from other people on your observations.
        </p>
      </div>

      {authLoading ? null : !user ? (
        <div
          className="rounded-lg border p-8 text-center text-sm"
          style={{ borderColor: "var(--color-border)", color: "var(--color-text-muted)" }}
        >
          Sign in to a Naturalist Mode account to see community activity on your observations.{" "}
          <Link href="/login" style={{ color: "var(--color-accent)" }}>
            Sign in
          </Link>
        </div>
      ) : activity === null ? null : activity.length === 0 ? (
        <div
          className="rounded-lg border p-8 text-center text-sm"
          style={{ borderColor: "var(--color-border)", color: "var(--color-text-muted)" }}
        >
          Nothing yet. Once someone comments on or agrees with one of your observations, it shows
          up here.
        </div>
      ) : (
        <ul className="flex flex-col gap-3">
          {activity.map((item) => (
            <li key={item.id}>
              <Link
                href={`/observations/${item.observationId}`}
                className="block rounded-lg border p-3"
                style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
              >
                <p className="text-sm">
                  <strong>{item.userEmail}</strong>{" "}
                  {item.kind === "agree" ? "agreed with your ID of" : "commented on"}{" "}
                  <strong>{item.observationCommonName}</strong>
                </p>
                {item.kind === "comment" && (
                  <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
                    &ldquo;{item.body}&rdquo;
                  </p>
                )}
                <p className="mt-1 text-xs" style={{ color: "var(--color-text-muted)" }}>
                  {new Date(item.createdAt).toLocaleString()}
                </p>
              </Link>
            </li>
          ))}
        </ul>
      )}

      <div
        className="rounded-lg border p-4"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
      >
        <p className="text-xs font-semibold uppercase tracking-wide" style={{ color: "var(--color-text-muted)" }}>
          Not built yet
        </p>
        <ul className="mt-2 list-disc space-y-1 pl-5 text-sm">
          <li>Badge and challenge notifications</li>
          <li>Curator/moderation notices with a clear, appealable written reason</li>
          <li>Read/unread state and push notifications</li>
        </ul>
      </div>
    </div>
  );
}
