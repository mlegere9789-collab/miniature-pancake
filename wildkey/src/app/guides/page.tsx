"use client";

import { useCallback, useEffect, useState, type FormEvent } from "react";
import Link from "next/link";
import { useAuth } from "@/lib/auth-context";
import { MOCK_SPECIES } from "@/lib/mock-species";

type Guide = {
  id: string;
  curatorEmail: string;
  title: string;
  description: string;
  taxonSlugs: string[];
  createdAt: string;
};

export default function GuidesPage() {
  const { user, loading: authLoading } = useAuth();
  const [guides, setGuides] = useState<Guide[] | null>(null);
  const [title, setTitle] = useState("");
  const [description, setDescription] = useState("");
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(async () => {
    const res = await fetch("/api/guides");
    const data = await res.json();
    setGuides(data.guides ?? []);
  }, []);

  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [load]);

  const toggleSpecies = (slug: string) => {
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(slug)) next.delete(slug);
      else next.add(slug);
      return next;
    });
  };

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setSubmitting(true);
    setError(null);
    const res = await fetch("/api/guides", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ title, description, taxonSlugs: Array.from(selected) }),
    });
    setSubmitting(false);
    if (!res.ok) {
      const data = await res.json().catch(() => null);
      setError(data?.error ?? "Something went wrong.");
      return;
    }
    setTitle("");
    setDescription("");
    setSelected(new Set());
    load();
  };

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Guides</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Curated collections of species — a themed field guide anyone can put together.
        </p>
      </div>

      {!authLoading &&
        (user ? (
          <form
            onSubmit={submit}
            className="flex flex-col gap-3 rounded-lg border p-4"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          >
            <input
              value={title}
              onChange={(e) => setTitle(e.target.value)}
              placeholder="Guide title, e.g. Backyard Pollinators"
              className="rounded border px-3 py-2 text-sm font-semibold"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            <textarea
              value={description}
              onChange={(e) => setDescription(e.target.value)}
              placeholder="What's this guide for?"
              rows={2}
              className="rounded border px-3 py-2 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            <div className="flex flex-wrap gap-2">
              {MOCK_SPECIES.map((s) => (
                <button
                  type="button"
                  key={s.slug}
                  onClick={() => toggleSpecies(s.slug)}
                  className="rounded-full border px-3 py-1.5 text-xs font-medium"
                  style={{
                    borderColor: "var(--color-border)",
                    background: selected.has(s.slug) ? "var(--color-accent)" : "transparent",
                    color: selected.has(s.slug) ? "var(--color-accent-contrast)" : "var(--color-text-muted)",
                  }}
                >
                  {s.commonName}
                </button>
              ))}
            </div>
            {error && (
              <p className="text-sm font-medium" style={{ color: "var(--color-danger)" }}>
                {error}
              </p>
            )}
            <button
              type="submit"
              disabled={submitting || !title.trim() || !description.trim() || selected.size === 0}
              className="self-start rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
            >
              {submitting ? "Creating…" : "Create guide"}
            </button>
          </form>
        ) : (
          <div
            className="rounded-lg border p-4 text-sm"
            style={{ borderColor: "var(--color-border)", color: "var(--color-text-muted)" }}
          >
            <Link href="/login" style={{ color: "var(--color-accent)" }}>
              Sign in
            </Link>{" "}
            to create a guide. Anyone can browse guides, signed in or not.
          </div>
        ))}

      {guides === null ? null : guides.length === 0 ? (
        <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
          No guides yet.
        </p>
      ) : (
        <ul className="flex flex-col gap-3">
          {guides.map((guide) => (
            <li key={guide.id}>
              <Link
                href={`/guides/${guide.id}`}
                className="block rounded-lg border p-4"
                style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
              >
                <h2 className="font-display text-lg font-semibold">{guide.title}</h2>
                <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
                  {guide.description}
                </p>
                <p className="mt-2 text-xs" style={{ color: "var(--color-text-muted)" }}>
                  {guide.taxonSlugs.length} species · by {guide.curatorEmail}
                </p>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
