import { StubPage } from "@/components/stub-page";

export default function DataSettingsPage() {
  return (
    <StubPage
      title="Data & export"
      description="Full data portability from day one — no risk of losing your history, ever."
      planned={[
        "One-tap export: all observations, photos, and metadata (JSON + CSV + media zip)",
        "Account migration tooling for switching devices",
        "Account anonymization: keep contribution history, remove personal identity",
        "Account deletion, clearly explained and reversible for a grace period",
      ]}
    />
  );
}
