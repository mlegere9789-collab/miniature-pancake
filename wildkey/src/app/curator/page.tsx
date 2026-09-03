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

type Curator = { id: string; email: string };

type RoleChange = {
  id: string;
  targetEmail: string;
  changedByEmail: string;
  oldRole: string;
  newRole: string;
  reason: string;
  createdAt: string;
};

export default function CuratorPage() {
  const { user, loading: authLoading } = useAuth();
  const [flags, setFlags] = useState<Flag[] | null>(null);
  const [reasons, setReasons] = useState<Record<string, string>>({});
  const [pending, setPending] = useState<string | null>(null);
  const [curators, setCurators] = useState<Curator[] | null>(null);
  const [roleChanges, setRoleChanges] = useState<RoleChange[]>([]);
  const [promoteEmail, setPromoteEmail] = useState("");
  const [promoteReason, setPromoteReason] = useState("");
  const [promoteError, setPromoteError] = useState<string | null>(null);
  const [promoting, setPromoting] = useState(false);
  const [demoteReasons, setDemoteReasons] = useState<Record<string, string>>({});
  const [demoting, setDemoting] = useState<string | null>(null);
  const [demoteError, setDemoteError] = useState<string | null>(null);

  const load = useCallback(async () => {
    if (!user || user.role !== "curator") return;
    const [flagsRes, curatorsRes] = await Promise.all([
      fetch("/api/curator/flags"),
      fetch("/api/curator/curators"),
    ]);
    setFlags(flagsRes.ok ? ((await flagsRes.json()).flags ?? []) : []);
    if (curatorsRes.ok) {
      const data = await curatorsRes.json();
      setCurators(data.curators ?? []);
      setRoleChanges(data.roleChanges ?? []);
    } else {
      setCurators([]);
    }
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

  const promote = async () => {
    setPromoteError(null);
    setPromoting(true);
    const res = await fetch("/api/curator/promote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: promoteEmail.trim(), reason: promoteReason.trim() }),
    });
    setPromoting(false);
    if (!res.ok) {
      const data = await res.json().catch(() => null);
      setPromoteError(data?.error ?? "Something went wrong promoting that account.");
      return;
    }
    setPromoteEmail("");
    setPromoteReason("");
    load();
  };

  const demote = async (email: string) => {
    const reason = (demoteReasons[email] ?? "").trim();
    if (!reason) return;
    setDemoteError(null);
    setDemoting(email);
    const res = await fetch("/api/curator/demote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, reason }),
    });
    setDemoting(null);
    if (!res.ok) {
      const data = await res.json().catch(() => null);
      setDemoteError(data?.error ?? "Something went wrong removing that curator.");
      return;
    }
    setDemoteReasons((prev) => ({ ...prev, [email]: "" }));
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

      <div className="flex flex-col gap-3 border-t pt-6" style={{ borderColor: "var(--color-border)" }}>
        <div>
          <h2 className="font-display text-xl font-semibold">Curators</h2>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Promote another account to curator by email, or remove a curator&rsquo;s access.
            Every change requires a written reason and is logged below.
          </p>
        </div>

        {curators === null ? null : (
          <ul className="flex flex-col gap-2">
            {curators.map((c) => (
              <li
                key={c.id}
                className="flex flex-wrap items-center justify-between gap-2 rounded-lg border p-3 text-sm"
                style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
              >
                <span className="font-medium">{c.email}</span>
                <div className="flex items-center gap-2">
                  <input
                    value={demoteReasons[c.email] ?? ""}
                    onChange={(e) => setDemoteReasons((prev) => ({ ...prev, [c.email]: e.target.value }))}
                    aria-label={`Reason for removing curator access from ${c.email}`}
                    placeholder="Reason (required)"
                    className="w-48 rounded border px-2 py-1 text-xs"
                    style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
                  />
                  <button
                    onClick={() => demote(c.email)}
                    disabled={demoting === c.email || !(demoteReasons[c.email] ?? "").trim() || c.id === user.id}
                    className="rounded-full border px-3 py-1.5 text-xs font-semibold disabled:opacity-40"
                    style={{ borderColor: "var(--color-danger)", color: "var(--color-danger)" }}
                    title={c.id === user.id ? "You can't remove your own curator role." : undefined}
                  >
                    Remove
                  </button>
                </div>
              </li>
            ))}
          </ul>
        )}

        <div className="flex flex-col gap-2 rounded-lg border p-4" style={{ borderColor: "var(--color-border)" }}>
          <p className="text-sm font-semibold">Promote an account</p>
          <input
            value={promoteEmail}
            onChange={(e) => setPromoteEmail(e.target.value)}
            type="email"
            aria-label="Email of the account to promote to curator"
            placeholder="person@example.com"
            className="rounded border px-3 py-2 text-sm"
            style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
          />
          <input
            value={promoteReason}
            onChange={(e) => setPromoteReason(e.target.value)}
            aria-label="Reason for promoting this account to curator"
            placeholder="Reason (required)"
            className="rounded border px-3 py-2 text-sm"
            style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
          />
          <button
            onClick={promote}
            disabled={promoting || !promoteEmail.trim() || !promoteReason.trim()}
            className="w-fit rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
            style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
          >
            {promoting ? "Promoting…" : "Promote to curator"}
          </button>
          {promoteError && (
            <p className="text-sm font-medium" style={{ color: "var(--color-danger)" }}>
              {promoteError}
            </p>
          )}
          {demoteError && (
            <p className="text-sm font-medium" style={{ color: "var(--color-danger)" }}>
              {demoteError}
            </p>
          )}
        </div>

        {roleChanges.length > 0 && (
          <details className="text-sm">
            <summary className="cursor-pointer font-semibold" style={{ color: "var(--color-text-muted)" }}>
              Role change history
            </summary>
            <ul className="mt-2 flex flex-col gap-2">
              {roleChanges.map((rc) => (
                <li key={rc.id} className="text-xs" style={{ color: "var(--color-text-muted)" }}>
                  <strong>{rc.changedByEmail}</strong> changed <strong>{rc.targetEmail}</strong> from{" "}
                  {rc.oldRole} to {rc.newRole}: {rc.reason} —{" "}
                  {new Date(rc.createdAt).toLocaleString()}
                </li>
              ))}
            </ul>
          </details>
        )}
      </div>
    </div>
  );
}
