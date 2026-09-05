"use client";

import { use, useCallback, useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import Link from "next/link";
import { useAuth } from "@/lib/auth-context";
import { LazyPhoto } from "@/components/lazy-photo";
import { QualityGradeBadge } from "@/components/quality-grade-badge";
import type { ObservationWithGrade } from "@/lib/api-observations";

type Project = {
  id: string;
  ownerId: string;
  ownerEmail: string;
  type: "collection" | "traditional";
  title: string;
  description: string;
  taxonFilter: string[];
  memberCount: number;
  createdAt: string;
};

type Member = { userId: string; email: string; joinedAt: string };

type DetailResponse =
  | { project: Project; observations: ObservationWithGrade[] }
  | { project: Project; members: Member[]; isMember: boolean };

export default function ProjectDetailPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const { id } = use(params);
  const router = useRouter();
  const { user } = useAuth();
  const [data, setData] = useState<DetailResponse | null | undefined>(undefined);
  const [membershipPending, setMembershipPending] = useState(false);

  const load = useCallback(async () => {
    const res = await fetch(`/api/projects/${id}`);
    setData(res.ok ? await res.json() : null);
  }, [id]);

  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [load]);

  const removeProject = async () => {
    await fetch(`/api/projects/${id}`, { method: "DELETE" });
    router.push("/projects");
  };

  const toggleMembership = async (join: boolean) => {
    setMembershipPending(true);
    await fetch(`/api/projects/${id}/membership`, { method: join ? "POST" : "DELETE" });
    setMembershipPending(false);
    load();
  };

  if (data === undefined) return null;
  if (data === null) {
    return (
      <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-2 px-4 py-12 text-center sm:px-6">
        <p className="font-semibold">Project not found.</p>
        <Link href="/projects" style={{ color: "var(--color-accent)" }}>
          Back to Projects
        </Link>
      </div>
    );
  }

  const { project } = data;

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-4 px-4 py-8 sm:px-6">
      <div className="flex items-start justify-between gap-2">
        <div>
          <h1 className="font-display text-2xl font-semibold">{project.title}</h1>
          <p className="text-xs" style={{ color: "var(--color-text-muted)" }}>
            {project.type === "collection" ? "Collection project" : "Traditional project"} · by{" "}
            {project.ownerEmail}
          </p>
        </div>
        {user?.id === project.ownerId && (
          <button
            onClick={removeProject}
            className="shrink-0 text-xs font-semibold"
            style={{ color: "var(--color-danger)" }}
          >
            Delete
          </button>
        )}
      </div>

      <p className="text-sm">{project.description}</p>

      {project.type === "collection" && "observations" in data ? (
        <>
          <p className="text-xs font-semibold uppercase tracking-wide" style={{ color: "var(--color-text-muted)" }}>
            {data.observations.length} matching observation{data.observations.length === 1 ? "" : "s"}
          </p>
          {data.observations.length === 0 ? (
            <p className="text-sm" style={{ color: "var(--color-text-muted)" }}>
              No observations match this filter yet.
            </p>
          ) : (
            <ul className="flex flex-col gap-3">
              {data.observations.map((o) => (
                <li
                  key={o.id}
                  className="flex items-center gap-3 rounded-lg border p-3"
                  style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
                >
                  <LazyPhoto
                    src={o.photoDataUrl}
                    alt={o.commonName}
                    className="h-16 w-16 shrink-0 rounded-md object-cover"
                  />
                  <div className="min-w-0 flex-1">
                    <Link
                      href={`/observations/${o.id}`}
                      className="block truncate font-semibold hover:underline"
                    >
                      {o.commonName}
                    </Link>
                    <p className="truncate text-xs italic" style={{ color: "var(--color-text-muted)" }}>
                      {o.scientificName}
                    </p>
                  </div>
                  <QualityGradeBadge grade={o.qualityGrade} />
                </li>
              ))}
            </ul>
          )}
        </>
      ) : "members" in data ? (
        <>
          <div className="flex items-center justify-between">
            <p className="text-xs font-semibold uppercase tracking-wide" style={{ color: "var(--color-text-muted)" }}>
              {data.members.length} member{data.members.length === 1 ? "" : "s"}
            </p>
            {user && (
              <button
                onClick={() => toggleMembership(!data.isMember)}
                disabled={membershipPending}
                className="rounded-full border px-3 py-1.5 text-xs font-semibold disabled:opacity-40"
                style={{ borderColor: "var(--color-border)" }}
              >
                {data.isMember ? "Leave project" : "Join project"}
              </button>
            )}
          </div>
          <ul className="flex flex-col gap-2">
            {data.members.map((m) => (
              <li
                key={m.userId}
                className="rounded-lg border p-3 text-sm"
                style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
              >
                {m.email}
              </li>
            ))}
          </ul>
        </>
      ) : null}
    </div>
  );
}
