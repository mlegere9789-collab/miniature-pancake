"use client";

import { useCallback, useEffect, useState, type FormEvent } from "react";
import Link from "next/link";
import { useAuth } from "@/lib/auth-context";
import { MOCK_SPECIES } from "@/lib/mock-species";

type Project = {
  id: string;
  ownerEmail: string;
  type: "collection" | "traditional";
  title: string;
  description: string;
  taxonFilter: string[];
  memberCount: number;
  createdAt: string;
};

export default function ProjectsPage() {
  const { user, loading: authLoading } = useAuth();
  const [projects, setProjects] = useState<Project[] | null>(null);
  const [type, setType] = useState<"collection" | "traditional">("collection");
  const [title, setTitle] = useState("");
  const [description, setDescription] = useState("");
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(async () => {
    const res = await fetch("/api/projects");
    const data = await res.json();
    setProjects(data.projects ?? []);
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
    const res = await fetch("/api/projects", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ type, title, description, taxonFilter: Array.from(selected) }),
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
        <h1 className="font-display text-2xl font-semibold">Projects</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Collection projects are a saved search — every observation matching the filter, live.
          Traditional projects are a group anyone can opt into.
        </p>
      </div>

      {!authLoading &&
        (user ? (
          <form
            onSubmit={submit}
            className="flex flex-col gap-3 rounded-lg border p-4"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          >
            <div className="flex gap-2">
              {(["collection", "traditional"] as const).map((t) => (
                <button
                  type="button"
                  key={t}
                  onClick={() => setType(t)}
                  className="rounded-full px-3 py-1.5 text-xs font-semibold capitalize"
                  style={{
                    background: type === t ? "var(--color-accent)" : "transparent",
                    color: type === t ? "var(--color-accent-contrast)" : "var(--color-text-muted)",
                    border: `1px solid ${type === t ? "var(--color-accent)" : "var(--color-border)"}`,
                  }}
                >
                  {t}
                </button>
              ))}
            </div>
            <input
              value={title}
              onChange={(e) => setTitle(e.target.value)}
              aria-label="Project title"
              placeholder="Project title"
              className="rounded border px-3 py-2 text-sm font-semibold"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            <textarea
              value={description}
              onChange={(e) => setDescription(e.target.value)}
              aria-label="Project description"
              placeholder="What's this project about?"
              rows={2}
              className="rounded border px-3 py-2 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            {type === "collection" && (
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
            )}
            {error && (
              <p className="text-sm font-medium" style={{ color: "var(--color-danger)" }}>
                {error}
              </p>
            )}
            <button
              type="submit"
              disabled={
                submitting ||
                !title.trim() ||
                !description.trim() ||
                (type === "collection" && selected.size === 0)
              }
              className="self-start rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
            >
              {submitting ? "Creating…" : "Create project"}
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
            to create a project. Anyone can browse projects, signed in or not.
          </div>
        ))}

      {projects === null ? null : projects.length === 0 ? (
        <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
          No projects yet.
        </p>
      ) : (
        <ul className="flex flex-col gap-3">
          {projects.map((project) => (
            <li key={project.id}>
              <Link
                href={`/projects/${project.id}`}
                className="block rounded-lg border p-4"
                style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
              >
                <div className="flex items-center justify-between gap-2">
                  <h2 className="font-display text-lg font-semibold">{project.title}</h2>
                  <span
                    className="shrink-0 rounded-full px-2 py-0.5 text-xs font-semibold capitalize"
                    style={{ background: "var(--color-border)", color: "var(--color-text)" }}
                  >
                    {project.type}
                  </span>
                </div>
                <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
                  {project.description}
                </p>
                <p className="mt-2 text-xs" style={{ color: "var(--color-text-muted)" }}>
                  {project.type === "collection"
                    ? `${project.taxonFilter.length} species in filter`
                    : `${project.memberCount} member${project.memberCount === 1 ? "" : "s"}`}{" "}
                  · by {project.ownerEmail}
                </p>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
