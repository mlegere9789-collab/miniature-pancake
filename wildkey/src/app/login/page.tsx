"use client";

import { useState, type FormEvent } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useAuth } from "@/lib/auth-context";

export default function LoginPage() {
  const router = useRouter();
  const { refresh } = useAuth();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  const onSubmit = async (e: FormEvent) => {
    e.preventDefault();
    setSubmitting(true);
    setError(null);
    const res = await fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password }),
    });
    setSubmitting(false);
    if (!res.ok) {
      const data = await res.json().catch(() => null);
      setError(data?.error ?? "Something went wrong.");
      return;
    }
    await refresh();
    router.push("/me");
  };

  return (
    <div className="mx-auto flex w-full max-w-sm flex-1 flex-col gap-6 px-4 py-12 sm:px-6">
      <div>
        <h1 className="font-display text-2xl font-semibold">Sign in</h1>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          Naturalist Mode accounts sync your observations across devices. Quick ID Mode never
          requires this.
        </p>
      </div>

      <form onSubmit={onSubmit} className="flex flex-col gap-4">
        <label className="flex flex-col gap-1 text-sm font-medium">
          Email
          <input
            type="email"
            required
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            className="rounded border px-3 py-2 text-sm"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          />
        </label>
        <label className="flex flex-col gap-1 text-sm font-medium">
          Password
          <input
            type="password"
            required
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            className="rounded border px-3 py-2 text-sm"
            style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
          />
        </label>

        {error && (
          <p className="text-sm font-medium" style={{ color: "var(--color-danger)" }}>
            {error}
          </p>
        )}

        <button
          type="submit"
          disabled={submitting}
          className="rounded-full px-4 py-2.5 text-sm font-semibold disabled:opacity-50"
          style={{ background: "var(--color-accent)", color: "var(--color-accent-contrast)" }}
        >
          {submitting ? "Signing in…" : "Sign in"}
        </button>
      </form>

      <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
        No account?{" "}
        <Link href="/signup" style={{ color: "var(--color-accent)" }} className="font-semibold">
          Sign up
        </Link>
      </p>
    </div>
  );
}
