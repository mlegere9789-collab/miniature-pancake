import { StubPage } from "@/components/stub-page";

export default function ExplorePage() {
  return (
    <StubPage
      title="Explore"
      description="Search sightings by map, grid, or list — with the full filter set from Part C.3."
      planned={[
        "Map/grid/list toggle, all URL-shareable",
        "Satellite view default, sticky per user across every map surface",
        "Full filter set (taxon, place, date, quality grade, user)",
        "Custom taxon-group map pins (bird, plant, fungus, insect, mammal, reptile/amphibian)",
      ]}
    />
  );
}
