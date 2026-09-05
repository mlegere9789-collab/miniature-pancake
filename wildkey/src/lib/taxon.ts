import type { Species } from "@/lib/mock-species";

export type TaxonGroup = Species["taxonGroup"];

export const TAXON_GROUPS: { value: TaxonGroup; label: string; color: string }[] = [
  { value: "plant", label: "Plants", color: "var(--color-moss)" },
  { value: "bird", label: "Birds", color: "var(--color-sky)" },
  { value: "insect", label: "Insects", color: "var(--color-warning)" },
  { value: "fungus", label: "Fungi", color: "var(--color-soil)" },
  { value: "mammal", label: "Mammals", color: "var(--color-bark)" },
  { value: "reptile", label: "Reptiles/Amphibians", color: "var(--color-danger)" },
];

export function taxonColor(group: TaxonGroup): string {
  return TAXON_GROUPS.find((g) => g.value === group)?.color ?? "var(--color-text-muted)";
}

export function taxonLabel(group: TaxonGroup): string {
  return TAXON_GROUPS.find((g) => g.value === group)?.label ?? group;
}
