import { MOCK_SPECIES, type Species } from "@/lib/mock-species";

export type QueueItem = {
  id: string;
  species: Species;
  suggestedConfidence: number;
  observerName: string;
  place: string;
};

const OBSERVERS = ["mkestrel", "trailrunner_j", "backyard_botanist", "riverside_naturalist"];
const PLACES = ["Cedar Ridge Park", "Willow Creek Trail", "Sunset Marsh Preserve", "Blue Hill Reserve"];

export function generateMockQueue(count = 8): QueueItem[] {
  return Array.from({ length: count }, (_, i) => {
    const species = MOCK_SPECIES[i % MOCK_SPECIES.length];
    return {
      id: `queue-${i}`,
      species,
      suggestedConfidence: 0.4 + Math.random() * 0.55,
      observerName: OBSERVERS[i % OBSERVERS.length],
      place: PLACES[i % PLACES.length],
    };
  });
}
