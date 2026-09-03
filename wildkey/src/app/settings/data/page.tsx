"use client";

import { useEffect, useState } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useAuth } from "@/lib/auth-context";
import { deleteObservation, listObservations, type Observation } from "@/lib/observations";
import { createServerObservation } from "@/lib/api-observations";

export default function DataSettingsPage() {
  const { user, loading, refresh } = useAuth();
  const router = useRouter();
  const [localObservations, setLocalObservations] = useState<Observation[]>([]);
  const [importing, setImporting] = useState(false);
  const [importProgress, setImportProgress] = useState(0);
  const [importFailures, setImportFailures] = useState(0);
  const [importDone, setImportDone] = useState(false);
  const [deleteConfirmText, setDeleteConfirmText] = useState("");
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const [cancelling, setCancelling] = useState(false);
  const [cancelError, setCancelError] = useState<string | null>(null);
  const [anonymizeConfirmText, setAnonymizeConfirmText] = useState("");
  const [anonymizing, setAnonymizing] = useState(false);
  const [anonymizeError, setAnonymizeError] = useState<string | null>(null);

  const deleteAccount = async () => {
    setDeleting(true);
    setDeleteError(null);
    const res = await fetch("/api/account/delete", { method: "POST" });
    setDeleting(false);
    if (!res.ok) {
      setDeleteError("Something went wrong scheduling your account for deletion.");
      return;
    }
    setDeleteConfirmText("");
    await refresh();
  };

  const cancelDeletion = async () => {
    setCancelling(true);
    setCancelError(null);
    const res = await fetch("/api/account/delete/cancel", { method: "POST" });
    setCancelling(false);
    if (!res.ok) {
      setCancelError("Something went wrong cancelling the scheduled deletion.");
      return;
    }
    await refresh();
  };

  useEffect(() => {
    // Reading localStorage (an external system) on mount, not deriving
    // state from props/state — the pattern react-hooks/set-state-in-effect
    // warns about does not apply here.
    // eslint-disable-next-line react-hooks/set-state-in-effect
    setLocalObservations(listObservations());
  }, []);

  /**
   * Real account-migration tooling (previously listed as "not built yet"):
   * a Quick ID Mode collection lives only in this browser's localStorage —
   * switch devices or clear site data and it's gone. Once signed in, each
   * local observation is uploaded through the same API a Naturalist Mode
   * save already uses, then removed locally only after that upload really
   * succeeds — a move, not a copy, and never a silent one: every failure
   * stays in the local list so nothing is lost if a request fails
   * partway through.
   */
  const importLocalObservations = async () => {
    setImporting(true);
    setImportProgress(0);
    setImportFailures(0);
    setImportDone(false);

    const toImport = listObservations();
    let failures = 0;
    for (let i = 0; i < toImport.length; i++) {
      const o = toImport[i];
      const saved = await createServerObservation({
        photoDataUrl: o.photoDataUrl,
        commonName: o.commonName,
        scientificName: o.scientificName,
        confidence: o.confidence,
        taxonSlug: o.taxonSlug,
        isWild: true,
        locationName: "",
        notes: "",
      });
      if (saved) {
        deleteObservation(o.id);
      } else {
        failures += 1;
      }
      setImportProgress(i + 1);
      setImportFailures(failures);
    }

    setLocalObservations(listObservations());
    setImporting(false);
    setImportDone(true);
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

      {!loading && user && (localObservations.length > 0 || importDone) && (
        <div className="rounded-lg border p-4" style={{ borderColor: "var(--color-border)" }}>
          <p className="font-semibold">Import local observations</p>
          {localObservations.length > 0 && (
            <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
              {localObservations.length} observation{localObservations.length === 1 ? "" : "s"} saved
              on this device in Quick ID Mode — those live only in this browser and won&rsquo;t
              follow you to another device. Import them into your account to make them permanent
              and synced everywhere. Each one is removed from this device only after it&rsquo;s
              confirmed saved to your account, so nothing is lost if an import fails partway
              through.
            </p>
          )}
          {localObservations.length > 0 && (
            <button
              onClick={importLocalObservations}
              disabled={importing}
              className="mt-3 rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
            >
              {importing
                ? `Importing ${importProgress} of ${localObservations.length}…`
                : `Import ${localObservations.length} observation${localObservations.length === 1 ? "" : "s"}`}
            </button>
          )}
          {importDone && (
            <p className="mt-2 text-sm font-medium">
              {importFailures === 0
                ? `Imported all ${importProgress} observations.`
                : `Imported ${importProgress - importFailures} of ${importProgress} — ${importFailures} failed and stayed on this device. Try again.`}
            </p>
          )}
        </div>
      )}

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

      {!loading && user && user.pendingDeletionAt && (
        <div className="rounded-lg border p-4" style={{ borderColor: "var(--color-danger)" }}>
          <p className="font-semibold" style={{ color: "var(--color-danger)" }}>
            Deletion scheduled
          </p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Your account and everything you&rsquo;ve posted will be permanently deleted on{" "}
            <strong>{new Date(user.pendingDeletionAt).toLocaleString()}</strong>. Until then your
            account works exactly as normal — nothing is hidden or read-only. Change your mind
            any time before that date.
          </p>
          <button
            onClick={cancelDeletion}
            disabled={cancelling}
            className="mt-3 rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
            style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
          >
            {cancelling ? "Cancelling…" : "Cancel scheduled deletion"}
          </button>
          {cancelError && (
            <p className="mt-2 text-sm font-medium" style={{ color: "var(--color-danger)" }}>
              {cancelError}
            </p>
          )}
        </div>
      )}

      {!loading && user && !user.pendingDeletionAt && (
        <div className="rounded-lg border p-4" style={{ borderColor: "var(--color-danger)" }}>
          <p className="font-semibold" style={{ color: "var(--color-danger)" }}>
            Delete account
          </p>
          <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
            Permanently deletes your account, every observation you own, and every comment
            you&rsquo;ve posted — full erasure, unlike anonymizing above, which keeps your
            contributions. Deletion is scheduled 14 days out, not immediate — your account keeps
            working normally the whole time, and you can cancel any time before then. Export your
            data first if you want a copy regardless. Type <strong>DELETE</strong> to confirm.
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
              {deleting ? "Scheduling…" : "Delete my account"}
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
          <li>
            Merging two separate accounts into one — importing local Quick ID Mode data into an
            account (above) is the one migration path that&rsquo;s real; a Naturalist Mode account
            itself already syncs everywhere on sign-in, nothing to migrate there
          </li>
          <li>
            A grace period on anonymizing — that one really is immediate and permanent by design
            (see above); deleting now has a real 14-day undo window instead
          </li>
        </ul>
      </div>
    </div>
  );
}
