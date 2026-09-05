"use client";

import { use, useCallback, useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import Link from "next/link";
import { useAuth } from "@/lib/auth-context";
import { getMockSpecies } from "@/lib/mock-species";
import { TaxonBadge } from "@/components/taxon-badge";

type Guide = {
  id: string;
  curatorId: string;
  curatorEmail: string;
  title: string;
  description: string;
  taxonSlugs: string[];
  createdAt: string;
};

export default function GuideDetailPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const { id } = use(params);
  const router = useRouter();
  const { user } = useAuth();
  const [guide, setGuide] = useState<Guide | null | undefined>(undefined);

  const load = useCallback(async () => {
    const res = await fetch(`/api/guides/${id}`);
    setGuide(res.ok ? (await res.json()).guide : null);
  }, [id]);

  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect
    load();
  }, [load]);

  const remove = async () => {
    await fetch(`/api/guides/${id}`, { method: "DELETE" });
    router.push("/guides");
  };

  if (guide === undefined) return null;
  if (guide === null) {
    return (
      <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-2 px-4 py-12 text-center sm:px-6">
        <p className="font-semibold">Guide not found.</p>
        <Link href="/guides" style={{ color: "var(--color-accent)" }}>
          Back to Guides
        </Link>
      </div>
    );
  }

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-4 px-4 py-8 sm:px-6">
      <div className="flex items-start justify-between gap-2">
        <h1 className="font-display text-2xl font-semibold">{guide.title}</h1>
        {user?.id === guide.curatorId && (
          <button
            onClick={remove}
            className="shrink-0 text-xs font-semibold"
            style={{ color: "var(--color-danger)" }}
          >
            Delete
          </button>
        )}
      </div>
      <p className="text-xs" style={{ color: "var(--color-text-muted)" }}>
        by {guide.curatorEmail} · {new Date(guide.createdAt).toLocaleDateString()}
      </p>
      <p className="text-sm">{guide.description}</p>

      <ul className="flex flex-col gap-2">
        {guide.taxonSlugs.map((slug) => {
          const species = getMockSpecies(slug);
          if (!species) return null;
          return (
            <li key={slug}>
              <Link
                href={`/species/${slug}`}
                className="flex items-center justify-between rounded-lg border p-3"
                style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
              >
                <div>
                  <p className="font-semibold">{species.commonName}</p>
                  <p className="text-xs italic" style={{ color: "var(--color-text-muted)" }}>
                    {species.scientificName}
                  </p>
                </div>
                <TaxonBadge group={species.taxonGroup} />
              </Link>
            </li>
          );
        })}
      </ul>
    </div>
  );
}
