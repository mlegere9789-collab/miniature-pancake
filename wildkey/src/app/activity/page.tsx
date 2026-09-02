import { StubPage } from "@/components/stub-page";

export default function ActivityPage() {
  return (
    <StubPage
      title="Activity"
      description="Notifications, community replies, and ID requests in one place."
      planned={[
        "Community ID activity: agree / disagree / comment threads",
        "Badge and challenge notifications (no push spam, opt-in cadence)",
        "Curator/moderation notices with a clear, appealable written reason",
      ]}
    />
  );
}
