import { StubPage } from "@/components/stub-page";

export default function IdentifyPage() {
  return (
    <StubPage
      title="Identify queue"
      description="A high-throughput screen for community identifiers — available on mobile and web, unlike the current iNaturalist app."
      planned={[
        "Rapid swipe-style queue with keyboard shortcuts on web",
        "Agree / suggest a different ID / skip",
        "Filter queue by taxon group, place, or needing-ID status",
        "Visible CV confidence % shown alongside each observation",
      ]}
    />
  );
}
