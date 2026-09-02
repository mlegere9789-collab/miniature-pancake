import { StubPage } from "@/components/stub-page";

export default function LiteModePage() {
  return (
    <StubPage
      title="Lite Mode"
      description="A first-class mode for old devices and slow connections — not a stripped 'lesser' experience."
      planned={[
        "No background sync, no live map tiles, explicit 'sync now' only",
        "No auto-downloading of full-res images",
        "CV suggestions off by default",
        "Usable on a 6-year-old low-end Android device on throttled 2G",
      ]}
    />
  );
}
