import type { TaxonGroup } from "@/lib/taxon";

/**
 * Annotations (Part C.2/Part K checklist: "life stage, sex, phenology") —
 * plain client-safe data, kept out of src/lib/server/store.ts (which pulls
 * in better-sqlite3 and can't be imported from a "use client" component).
 * store.ts re-exports these so server-side imports don't change.
 *
 * Real iNaturalist's annotation vocabulary is larger and partly
 * taxon-specific (e.g. separate insect life stages). This is a genuine
 * simplification: one shared Life Stage list and one shared Sex list for
 * every animal group, and Phenology scoped to plants only — honestly
 * smaller than the real thing, not a hidden gap.
 */
export const LIFE_STAGES = ["adult", "subadult", "juvenile", "egg", "larva", "pupa"] as const;
export type LifeStage = (typeof LIFE_STAGES)[number];

export const SEXES = ["male", "female", "cannot_be_determined"] as const;
export type Sex = (typeof SEXES)[number];

export const PHENOLOGIES = ["flowering", "fruiting", "flower_budding", "no_evidence_of_flowering"] as const;
export type Phenology = (typeof PHENOLOGIES)[number];

export const LIFE_STAGE_LABELS: Record<LifeStage, string> = {
  adult: "Adult",
  subadult: "Subadult",
  juvenile: "Juvenile",
  egg: "Egg",
  larva: "Larva/Nymph",
  pupa: "Pupa",
};

export const SEX_LABELS: Record<Sex, string> = {
  male: "Male",
  female: "Female",
  cannot_be_determined: "Cannot be determined",
};

export const PHENOLOGY_LABELS: Record<Phenology, string> = {
  flowering: "Flowering",
  fruiting: "Fruiting",
  flower_budding: "Flower budding",
  no_evidence_of_flowering: "No evidence of flowering",
};

/** Which annotation types make sense for a given taxon group — plants get phenology, animal groups get life stage + sex, fungi get neither here. */
export function annotationsApplicableFor(taxonGroup: TaxonGroup): {
  lifeStage: boolean;
  sex: boolean;
  phenology: boolean;
} {
  if (taxonGroup === "plant") return { lifeStage: false, sex: false, phenology: true };
  if (taxonGroup === "fungus") return { lifeStage: false, sex: false, phenology: false };
  return { lifeStage: true, sex: true, phenology: false };
}
