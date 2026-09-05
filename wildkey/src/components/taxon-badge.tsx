import { taxonColor, taxonLabel, type TaxonGroup } from "@/lib/taxon";

export function TaxonBadge({ group }: { group: TaxonGroup }) {
  return (
    <span className="inline-flex items-center gap-1.5 text-xs font-medium" style={{ color: "var(--color-text-muted)" }}>
      <span
        aria-hidden
        className="h-2.5 w-2.5 rounded-full"
        style={{ background: taxonColor(group) }}
      />
      {taxonLabel(group)}
    </span>
  );
}
