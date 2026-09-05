"use client";

import { useCallback, useEffect, useState, type FormEvent } from "react";
import Link from "next/link";
import { useAuth } from "@/lib/auth-context";

type JournalPost = {
  id: string;
  authorEmail: string;
  title: string;
  body: string;
  createdAt: string;
};

export default function JournalPage() {
  const { user, loading: authLoading } = useAuth();
  const [posts, setPosts] = useState<JournalPost[] | null>(null);
  const [title, setTitle] = useState("");
  const [body, setBody] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(async () => {
    const res = await fetch("/api/journal");
    const data = await res.json();
    setPosts(data.posts ?? []);
  }, []);

  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [load]);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setSubmitting(true);
    setError(null);
    const res = await fetch("/api/journal", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ title, body }),
    });
    setSubmitting(false);
    if (!res.ok) {
      const data = await res.json().catch(() => null);
      setError(data?.error ?? "Something went wrong.");
      return;
    }
    setTitle("");
    setBody("");
    load();
  };

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Journal</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Longer-form posts from the community — trip reports, natural history notes, anything
          that doesn&rsquo;t fit in a single observation.
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
              aria-label="Post title"
              placeholder="Title"
              className="rounded border px-3 py-2 text-sm font-semibold"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            <textarea
              value={body}
              onChange={(e) => setBody(e.target.value)}
              aria-label="Post body"
              placeholder="What did you see?"
              rows={4}
              className="rounded border px-3 py-2 text-sm"
              style={{ borderColor: "var(--color-border)", background: "var(--color-bg)" }}
            />
            {error && (
              <p className="text-sm font-medium" style={{ color: "var(--color-danger)" }}>
                {error}
              </p>
            )}
            <button
              type="submit"
              disabled={submitting || !title.trim() || !body.trim()}
              className="self-start rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
            >
              {submitting ? "Posting…" : "Post"}
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
            to post a journal entry. Anyone can read journal posts, signed in or not.
          </div>
        ))}

      {posts === null ? null : posts.length === 0 ? (
        <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
          Nothing posted yet.
        </p>
      ) : (
        <ul className="flex flex-col gap-3">
          {posts.map((post) => (
            <li key={post.id}>
              <Link
                href={`/journal/${post.id}`}
                className="block rounded-lg border p-4"
                style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
              >
                <h2 className="font-display text-lg font-semibold">{post.title}</h2>
                <p className="mt-1 line-clamp-2 text-sm" style={{ color: "var(--color-text-muted)" }}>
                  {post.body}
                </p>
                <p className="mt-2 text-xs" style={{ color: "var(--color-text-muted)" }}>
                  {post.authorEmail} · {new Date(post.createdAt).toLocaleString()}
                </p>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
