"use client";

import { useCallback, useEffect, useState } from "react";
import Link from "next/link";
import { useAuth } from "@/lib/auth-context";

type Flag = {
  id: string;
  observationId: string;
  reporterEmail: string;
  reason: string;
  status: string;
  createdAt: string;
  observationCommonName: string;
  observationTaxonSlug: string;
};

export default function CuratorPage() {
  const { user, loading: authLoading } = useAuth();
  const [flags, setFlags] = useState<Flag[] | null>(null);
  const [reasons, setReasons] = useState<Record<string, string>>({});
  const [pending, setPending] = useState<string | null>(null);

  const load = useCallback(async () => {
    if (!user || user.role !== "curator") return;
    const res = await fetch("/api/curator/flags");
    if (!res.ok) {
      setFlags([]);
      return;
    }
    const data = await res.json();
    setFlags(data.flags ?? []);
  }, [user]);

  useEffect(() => {
    if (authLoading) return;
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [authLoading, load]);

  const act = async (flagId: string, action: "resolved" | "dismissed") => {
    const reason = (reasons[flagId] ?? "").trim();
    if (!reason) return;
    setPending(flagId);
    await fetch(`/api/curator/flags/${flagId}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action, reason }),
    });
    setPending(null);
    load();
  };

  if (authLoading) return null;

  if (!user || user.role !== "curator") {
    return (
      <div className="mx-auto flex w-full max-w-xl flex-1 flex-col gap-2 px-4 py-12 text-center sm:px-6">
        <p className="font-semibold">Curator access required.</p>
        <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
          This queue is only visible to curator accounts.
        </p>
      </div>
    );
  }

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Curator queue</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Flagged observations awaiting review. Every resolution requires a written reason,
          logged and visible.
        </p>
      </div>

      {flags === null ? null : flags.length === 0 ? (
        <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
          No open flags.
        </p>
      ) : (
        <ul className="flex flex-col gap-4">
          {flags.map((flag) => (
            <li
              key={flag.id}
              className="rounded-lg border p-4"
              style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
            >
              <Link
                href={`/observations/${flag.observationId}`}
                className="font-semibold hover:underline"
              >
                {flag.observationCommonName}
              </Link>
              <p className="mt-1 text-sm">
                Flagged by <strong>{flag.reporterEmail}</strong>: {flag.reason}
              </p>
              <p className="mt-1 text-xs" style={{ color: "var(--color-text-muted)" }}>
                {new Date(flag.createdAt).toLocaleString()}
              </p>

              <div className="mt-3 flex flex-col gap-2">
                <input
                  value={reasons[flag.id] ?? ""}
                  onChange={(e) => setReasons((prev) => ({ ...prev, [flag.id]: e.target.value }))}
                  aria-label={`Reason for resolving or dismissing this flag on ${flag.observationCommonName}`}
                  placeholder="Reason for resolving or dismissing (required)"
                  className="rounded border px-3 py-2 text-sm"
                  style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
                />
                <div className="flex gap-2">
                  <button
                    onClick={() => act(flag.id, "resolved")}
                    disabled={pending === flag.id || !(reasons[flag.id] ?? "").trim()}
                    className="rounded-full px-3 py-1.5 text-xs font-semibold disabled:opacity-40"
                    style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
                  >
                    Resolve
                  </button>
                  <button
                    onClick={() => act(flag.id, "dismissed")}
                    disabled={pending === flag.id || !(reasons[flag.id] ?? "").trim()}
                    className="rounded-full border px-3 py-1.5 text-xs font-semibold disabled:opacity-40"
                    style={{ borderColor: "var(--color-border)" }}
                  >
                    Dismiss
                  </button>
                </div>
              </div>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
