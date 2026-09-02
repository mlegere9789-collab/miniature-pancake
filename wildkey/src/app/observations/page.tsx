import { StubPage } from "@/components/stub-page";

export default function ObservationsPage() {
  return (
    <StubPage
      title="My Observations"
      description="Your personal collection — local-first, auto-synced to the cloud once you have an account, sortable and searchable."
      planned={[
        "Sortable, searchable list/grid — an information-dense view by default",
        "Visible sync state per item: Queued → Uploading → Confirmed / Failed (tap to retry)",
        "Local upload log — nothing silently vanishes",
        "One-tap transfer of Quick ID history into a full Naturalist account",
        "One-tap full data export (JSON + CSV + media zip)",
      ]}
    />
  );
}
