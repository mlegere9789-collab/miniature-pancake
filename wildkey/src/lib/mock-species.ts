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
  {
    slug: "fly-agaric",
    commonName: "Fly Agaric",
    scientificName: "Amanita muscaria",
    taxonGroup: "fungus",
    description:
      "An iconic mushroom with a bright red cap covered in white spots, fruiting from a white stem and skirt-like ring.",
    seasonality: "Fruits late summer through fall, often near birch or pine.",
    observationCount: 64_552,
    danger: "Toxic if ingested — do not eat.",
  },
  {
    slug: "red-fox",
    commonName: "Red Fox",
    scientificName: "Vulpes vulpes",
    taxonGroup: "mammal",
    description:
      "A slender canid with a rust-red coat, black legs, and a bushy white-tipped tail, adaptable to both wild and urban edges.",
    seasonality: "Active year-round, most visible at dawn and dusk.",
    observationCount: 312_004,
  },
  {
    slug: "eastern-box-turtle",
    commonName: "Eastern Box Turtle",
    scientificName: "Terrapene carolina",
    taxonGroup: "reptile",
    description:
      "A terrestrial turtle with a high-domed, hinged shell that lets it close up almost completely for protection.",
    seasonality: "Active spring through fall; overwinters buried in leaf litter.",
    observationCount: 41_887,
  },
  {
    slug: "monarch-butterfly",
    commonName: "Monarch Butterfly",
    scientificName: "Danaus plexippus",
    taxonGroup: "insect",
    description:
      "A large butterfly with bold orange-and-black wings, known for its multi-generational migration across North America.",
    seasonality: "Breeds spring through summer; migrates south in fall.",
    observationCount: 597_213,
  },
];

export function getMockSpecies(slug: string): Species | undefined {
  return MOCK_SPECIES.find((s) => s.slug === slug);
}
