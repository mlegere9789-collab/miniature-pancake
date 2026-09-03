"use client";

import { useState } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useAuth } from "@/lib/auth-context";

export default function DataSettingsPage() {
  const { user, loading, refresh } = useAuth();
  const router = useRouter();
  const [confirmText, setConfirmText] = useState("");
  const [deleting, setDeleting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const deleteAccount = async () => {
    setDeleting(true);
    setError(null);
    const res = await fetch("/api/account/delete", { method: "POST" });
    if (!res.ok) {
      setDeleting(false);
      setError("Something went wrong deleting your account.");
      return;
    }
    await refresh();
    router.push("/");
  };

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Data & export</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Full data portability from day one — no risk of losing your history, ever.
        </p>
      </div>

      <div
        className="rounded-lg border p-4"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
      >
        {loading ? null : user ? (
          <>
            <p className="font-semibold">Export everything</p>
            <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
              Downloads a JSON file with your account info, every observation, and every comment
              you&rsquo;ve posted — everything this account owns in one file.
            </p>
            <a
              href="/api/account/export"
              download
              className="mt-3 inline-block rounded-full px-4 py-2 text-sm font-semibold"
              style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
            >
              Download my data
            </a>
          </>
        ) : (
          <>
            <p className="font-semibold">Sign in to export</p>
            <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
              Export applies to Naturalist Mode accounts. Quick ID Mode never creates an account,
              so there&rsquo;s nothing on a server to export — your local collection already lives
              on this device.
            </p>
            <Link
              href="/login"
              className="mt-3 inline-block rounded-full px-4 py-2 text-sm font-semibold"
              style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
            >
              Sign in
            </Link>
          </>
        )}
      </div>

      {!loading && user && (
        <div className="rounded-lg border p-4" style={{ borderColor: "var(--color-danger)" }}>
          <p className="font-semibold" style={{ color: "var(--color-danger)" }}>
            Delete account
          </p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Permanently deletes your account, every observation you own, and every comment
            you&rsquo;ve posted. This cannot be undone — there is no grace period yet, so export
            your data first if you want to keep it. Type <strong>DELETE</strong> to confirm.
          </p>
          <div className="mt-3 flex gap-2">
            <input
              value={confirmText}
              onChange={(e) => setConfirmText(e.target.value)}
              aria-label="Type DELETE to confirm account deletion"
              placeholder="DELETE"
              className="w-32 rounded border px-3 py-2 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            <button
              onClick={deleteAccount}
              disabled={confirmText !== "DELETE" || deleting}
              className="rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ background: "var(--color-danger)", color: "var(--color-accent-contrast)" }}
            >
              {deleting ? "Deleting…" : "Delete my account"}
            </button>
          </div>
          {error && (
            <p className="mt-2 text-sm font-medium" style={{ color: "var(--color-danger)" }}>
              {error}
            </p>
          )}
        </div>
      )}

      <div
        className="rounded-lg border p-4"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
      >
        <p className="text-xs font-semibold uppercase tracking-wide" style={{ color: "var(--color-text-muted)" }}>
          Not built yet
        </p>
        <ul className="mt-2 list-disc space-y-1 pl-5 text-sm">
          <li>CSV export and a media zip (photos currently ship as inline base64 data URLs inside the JSON, not separate files)</li>
          <li>Account migration tooling for switching devices</li>
          <li>Account anonymization: keep contribution history, remove personal identity</li>
          <li>A grace period / undo window on deletion — deletion above is immediate and permanent</li>
        </ul>
      </div>
    </div>
  );
}
