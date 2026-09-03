"use client";

import { useState } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useAuth } from "@/lib/auth-context";

export default function DataSettingsPage() {
  const { user, loading, refresh } = useAuth();
  const router = useRouter();
  const [deleteConfirmText, setDeleteConfirmText] = useState("");
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const [anonymizeConfirmText, setAnonymizeConfirmText] = useState("");
  const [anonymizing, setAnonymizing] = useState(false);
  const [anonymizeError, setAnonymizeError] = useState<string | null>(null);

  const deleteAccount = async () => {
    setDeleting(true);
    setDeleteError(null);
    const res = await fetch("/api/account/delete", { method: "POST" });
    if (!res.ok) {
      setDeleting(false);
      setDeleteError("Something went wrong deleting your account.");
      return;
    }
    await refresh();
    router.push("/");
  };

  const anonymizeAccount = async () => {
    setAnonymizing(true);
    setAnonymizeError(null);
    const res = await fetch("/api/account/anonymize", { method: "POST" });
    if (!res.ok) {
      setAnonymizing(false);
      setAnonymizeError("Something went wrong anonymizing your account.");
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
              Three formats, same underlying data: a full JSON file (account info, every
              observation, every comment), a CSV of just your observations for spreadsheets, and
              a zip of your photos as real image files.
            </p>
            <div className="mt-3 flex flex-wrap gap-2">
              <a
                href="/api/account/export"
                download
                className="inline-block rounded-full px-4 py-2 text-sm font-semibold"
                style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
              >
                Download JSON
              </a>
              <a
                href="/api/account/export/csv"
                download
                className="inline-block rounded-full border px-4 py-2 text-sm font-semibold"
                style={{ borderColor: "var(--color-border)" }}
              >
                Download CSV
              </a>
              <a
                href="/api/account/export/media"
                download
                className="inline-block rounded-full border px-4 py-2 text-sm font-semibold"
                style={{ borderColor: "var(--color-border)" }}
              >
                Download photos (zip)
              </a>
            </div>
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
        <div className="rounded-lg border p-4" style={{ borderColor: "var(--color-border)" }}>
          <p className="font-semibold">Anonymize account</p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Keeps every observation, comment, journal post, guide, and project you&rsquo;ve made —
            your contribution history stays exactly where it is, still credited to this account.
            But your email and password are replaced with an anonymous, unrecoverable placeholder,
            and you&rsquo;re signed out everywhere. There is no way back into this account
            afterward &mdash; export your data first if you want a personal copy. Type{" "}
            <strong>ANONYMIZE</strong> to confirm.
          </p>
          <div className="mt-3 flex gap-2">
            <input
              value={anonymizeConfirmText}
              onChange={(e) => setAnonymizeConfirmText(e.target.value)}
              aria-label="Type ANONYMIZE to confirm account anonymization"
              placeholder="ANONYMIZE"
              className="w-40 rounded border px-3 py-2 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            <button
              onClick={anonymizeAccount}
              disabled={anonymizeConfirmText !== "ANONYMIZE" || anonymizing}
              className="rounded-full border px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ borderColor: "var(--color-border)" }}
            >
              {anonymizing ? "Anonymizing…" : "Anonymize my account"}
            </button>
          </div>
          {anonymizeError && (
            <p className="mt-2 text-sm font-medium" style={{ color: "var(--color-danger)" }}>
              {anonymizeError}
            </p>
          )}
        </div>
      )}

      {!loading && user && (
        <div className="rounded-lg border p-4" style={{ borderColor: "var(--color-danger)" }}>
          <p className="font-semibold" style={{ color: "var(--color-danger)" }}>
            Delete account
          </p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Permanently deletes your account, every observation you own, and every comment
            you&rsquo;ve posted — full erasure, unlike anonymizing above, which keeps your
            contributions. This cannot be undone — there is no grace period yet, so export your
            data first if you want to keep it. Type <strong>DELETE</strong> to confirm.
          </p>
          <div className="mt-3 flex gap-2">
            <input
              value={deleteConfirmText}
              onChange={(e) => setDeleteConfirmText(e.target.value)}
              aria-label="Type DELETE to confirm account deletion"
              placeholder="DELETE"
              className="w-32 rounded border px-3 py-2 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            <button
              onClick={deleteAccount}
              disabled={deleteConfirmText !== "DELETE" || deleting}
              className="rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ background: "var(--color-danger)", color: "var(--color-accent-contrast)" }}
            >
              {deleting ? "Deleting…" : "Delete my account"}
            </button>
          </div>
          {deleteError && (
            <p className="mt-2 text-sm font-medium" style={{ color: "var(--color-danger)" }}>
              {deleteError}
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
          <li>Account migration tooling for switching devices</li>
          <li>A grace period / undo window on deletion or anonymization — both above are immediate and permanent</li>
        </ul>
      </div>
    </div>
  );
}
