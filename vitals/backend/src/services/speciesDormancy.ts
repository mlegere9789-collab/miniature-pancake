import { defaultDormancyMonths } from "./scoring";

/**
 * Real per-species dormancy calendar (spec §4.7), covering common garden
 * species so "Dormant in winter" can default itself correctly instead of
 * relying on the user to know their plant's habit. `speciesId` is the same
 * slug the client derives from the free-text species name
 * (`speciesName.trim().toLowerCase().replace(/\s+/g, "-")`), so lookups here
 * must use that same slug shape.
 *
 * This is intentionally a small, curated table, not exhaustive — an unknown
 * species falls back to the hemisphere-only heuristic in `scoring.ts`
 * (`defaultDormancyMonths`) rather than guessing.
 */
type DormancyHabit = "deciduous" | "evergreen" | "annual";

const SPECIES_HABITS: Record<string, DormancyHabit> = {
  // Deciduous trees & shrubs — go dormant in winter.
  "japanese-maple": "deciduous",
  maple: "deciduous",
  oak: "deciduous",
  "red-oak": "deciduous",
  birch: "deciduous",
  "river-birch": "deciduous",
  elm: "deciduous",
  dogwood: "deciduous",
  redbud: "deciduous",
  hydrangea: "deciduous",
  forsythia: "deciduous",
  lilac: "deciduous",
  "crape-myrtle": "deciduous",
  "crepe-myrtle": "deciduous",
  hosta: "deciduous",
  peony: "deciduous",
  "japanese-lilac": "deciduous",
  "smoke-bush": "deciduous",
  wisteria: "deciduous",
  grapevine: "deciduous",
  fig: "deciduous",
  "apple-tree": "deciduous",
  "pear-tree": "deciduous",
  "cherry-tree": "deciduous",
  "peach-tree": "deciduous",

  // Evergreens — never seasonally dormant in the sense of expected leaf drop.
  boxwood: "evergreen",
  holly: "evergreen",
  "pine-tree": "evergreen",
  spruce: "evergreen",
  "fir-tree": "evergreen",
  arborvitae: "evergreen",
  juniper: "evergreen",
  camellia: "evergreen",
  rhododendron: "evergreen",
  azalea: "evergreen",
  magnolia: "evergreen",
  "live-oak": "evergreen",
  bamboo: "evergreen",
  fern: "evergreen",
  lavender: "evergreen",
  rosemary: "evergreen",
  succulent: "evergreen",
  cactus: "evergreen",
  "aloe-vera": "evergreen",

  // Annuals / vegetables — no winter dormancy, they simply die back at
  // end-of-season and get replanted.
  tomato: "annual",
  basil: "annual",
  pepper: "annual",
  cucumber: "annual",
  zucchini: "annual",
  lettuce: "annual",
  marigold: "annual",
  petunia: "annual",
  sunflower: "annual",
  zinnia: "annual",
};

export interface SpeciesDormancyLookup {
  /** Whether this species is in the curated table at all. */
  known: boolean;
  habit: DormancyHabit | null;
  /** Recommended `dormancyMonths` for this species/latitude combination. */
  months: number[];
  /** Whether the "Dormant in winter" toggle should default on for this species. */
  suggestDormant: boolean;
}

export function lookupSpeciesDormancy(
  speciesId: string,
  latitude: number | null | undefined,
): SpeciesDormancyLookup {
  const habit = SPECIES_HABITS[speciesId] ?? null;

  if (habit === "deciduous") {
    return { known: true, habit, months: defaultDormancyMonths(latitude), suggestDormant: true };
  }
  if (habit === "evergreen" || habit === "annual") {
    return { known: true, habit, months: [], suggestDormant: false };
  }
  return { known: false, habit: null, months: defaultDormancyMonths(latitude), suggestDormant: false };
}
