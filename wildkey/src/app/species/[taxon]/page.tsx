import { notFound } from "next/navigation";
import { getMockSpecies, MOCK_SPECIES } from "@/lib/mock-species";
import { TaxonBadge } from "@/components/taxon-badge";

export function generateStaticParams() {
  return MOCK_SPECIES.map((s) => ({ taxon: s.slug }));
}

export default async function SpeciesPage({
  params,
}: {
  params: Promise<{ taxon: string }>;
}) {
  const { taxon } = await params;
  const species = getMockSpecies(taxon);
  if (!species) notFound();

  return (
    <div className="mx-auto flex w-full max-w-2xl flex-1 flex-col gap-6 px-4 py-8 sm:px-6">
      <div
        className="flex aspect-[4/3] w-full items-center justify-center rounded-lg border text-sm"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", color: "var(--color-text-muted)" }}
      >
        Photo placeholder
      </div>

      <div>
        <div className="mb-2 flex flex-wrap gap-2">
          {species.danger && (
            <span
              className="inline-block rounded-full px-2.5 py-1 text-xs font-semibold"
              style={{ background: "var(--color-danger)", color: "var(--color-accent-contrast)" }}
            >
              ⚠ {species.danger}
            </span>
          )}
          {species.sensitive && (
            <span
              className="inline-block rounded-full px-2.5 py-1 text-xs font-semibold"
              style={{ background: "var(--color-soil)", color: "var(--color-accent-contrast)" }}
            >
              Locations hidden — sensitive species
            </span>
          )}
        </div>
        <h1 className="font-display text-3xl font-semibold">{species.commonName}</h1>
        <p className="text-lg italic" style={{ color: "var(--color-text-muted)" }}>
          {species.scientificName}
        </p>
      </div>

      <p>{species.description}</p>

      <dl className="grid grid-cols-2 gap-4 rounded-lg border p-4 text-sm" style={{ borderColor: "var(--color-border)" }}>
        <div>
          <dt style={{ color: "var(--color-text-muted)" }}>Taxon group</dt>
          <dd className="font-medium">
            <TaxonBadge group={species.taxonGroup} />
          </dd>
        </div>
        <div>
          <dt style={{ color: "var(--color-text-muted)" }}>Seasonality</dt>
          <dd className="font-medium">{species.seasonality}</dd>
        </div>
        <div>
          <dt style={{ color: "var(--color-text-muted)" }}>Sightings worldwide</dt>
          <dd className="font-medium">{species.observationCount.toLocaleString()}</dd>
        </div>
      </dl>
    </div>
  );
}
