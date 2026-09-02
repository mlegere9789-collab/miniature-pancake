export type Species = {
  slug: string;
  commonName: string;
  scientificName: string;
  taxonGroup: "plant" | "bird" | "insect" | "fungus" | "mammal" | "reptile";
  description: string;
  seasonality: string;
  observationCount: number;
  danger?: string;
};

export const MOCK_SPECIES: Species[] = [
  {
    slug: "common-dandelion",
    commonName: "Common Dandelion",
    scientificName: "Taraxacum officinale",
    taxonGroup: "plant",
    description:
      "A widespread flowering herb with toothed leaves and a bright yellow flower head that matures into a white seed puff.",
    seasonality: "Flowers spring through fall in most temperate climates.",
    observationCount: 482_310,
  },
  {
    slug: "eastern-black-widow",
    commonName: "Eastern Black Widow",
    scientificName: "Latrodectus mactans",
    taxonGroup: "insect",
    description:
      "A shiny black spider with a red hourglass marking on the underside of its abdomen, typically found in sheltered, undisturbed spots.",
    seasonality: "Active year-round in warmer climates; most visible in late summer.",
    observationCount: 18_204,
    danger: "Venomous — bite can cause serious symptoms. Do not handle.",
  },
  {
    slug: "american-robin",
    commonName: "American Robin",
    scientificName: "Turdus migratorius",
    taxonGroup: "bird",
    description:
      "A familiar songbird with a warm orange breast and dark gray-brown back, often seen foraging on lawns.",
    seasonality: "Year-round in much of North America; more visible after spring migration.",
    observationCount: 1_204_887,
  },
];

export function getMockSpecies(slug: string): Species | undefined {
  return MOCK_SPECIES.find((s) => s.slug === slug);
}
