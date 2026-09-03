"use client";

import { use, useCallback, useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import Link from "next/link";
import { useAuth } from "@/lib/auth-context";

type JournalPost = {
  id: string;
  authorId: string;
  authorEmail: string;
  title: string;
  body: string;
  createdAt: string;
  updatedAt: string;
};

export default function JournalPostPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const { id } = use(params);
  const router = useRouter();
  const { user } = useAuth();
  const [post, setPost] = useState<JournalPost | null | undefined>(undefined);
  const [editing, setEditing] = useState(false);
  const [title, setTitle] = useState("");
  const [body, setBody] = useState("");
  const [saving, setSaving] = useState(false);

  const load = useCallback(async () => {
    const res = await fetch(`/api/journal/${id}`);
    if (!res.ok) {
      setPost(null);
      return;
    }
    const data = await res.json();
    setPost(data.post);
    setTitle(data.post.title);
    setBody(data.post.body);
  }, [id]);

  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [load]);

  const isAuthor = post && user && post.authorId === user.id;

  const save = async () => {
    setSaving(true);
    const res = await fetch(`/api/journal/${id}`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ title, body }),
    });
    setSaving(false);
    if (res.ok) {
      setEditing(false);
      load();
    }
  };

  const remove = async () => {
    await fetch(`/api/journal/${id}`, { method: "DELETE" });
    router.push("/journal");
  };

  if (post === undefined) return null;
  if (post === null) {
    return (
      <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-2 px-4 py-12 text-center sm:px-6">
        <p className="font-semibold">Post not found.</p>
        <Link href="/journal" style={{ color: "var(--color-accent)" }}>
          Back to Journal
        </Link>
      </div>
    );
  }

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-4 px-4 py-8 sm:px-6">
      {editing ? (
        <div className="flex flex-col gap-3">
          <input
            value={title}
            onChange={(e) => setTitle(e.target.value)}
            className="rounded border px-3 py-2 text-lg font-semibold"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          />
          <textarea
            value={body}
            onChange={(e) => setBody(e.target.value)}
            rows={8}
            className="rounded border px-3 py-2 text-sm"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          />
          <div className="flex gap-2">
            <button
              onClick={save}
              disabled={saving}
              className="rounded-full px-4 py-2 text-sm font-semibold disabled:opacity-40"
              style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
            >
              {saving ? "Saving…" : "Save"}
            </button>
            <button
              onClick={() => setEditing(false)}
              className="rounded-full border px-4 py-2 text-sm font-semibold"
              style={{ borderColor: "var(--color-border)" }}
            >
              Cancel
            </button>
          </div>
        </div>
      ) : (
        <>
          <div className="flex items-start justify-between gap-2">
            <h1 className="font-display text-2xl font-semibold">{post.title}</h1>
            {isAuthor && (
              <div className="flex shrink-0 gap-2">
                <button
                  onClick={() => setEditing(true)}
                  className="text-xs font-semibold"
                  style={{ color: "var(--color-accent)" }}
                >
                  Edit
                </button>
                <button
                  onClick={remove}
                  className="text-xs font-semibold"
                  style={{ color: "var(--color-danger)" }}
                >
                  Delete
                </button>
              </div>
            )}
          </div>
          <p className="text-xs" style={{ color: "var(--color-text-muted)" }}>
            {post.authorEmail} · {new Date(post.createdAt).toLocaleString()}
            {post.updatedAt !== post.createdAt && " (edited)"}
          </p>
          <p className="whitespace-pre-wrap text-sm">{post.body}</p>
        </>
      )}
    </div>
  );
}
