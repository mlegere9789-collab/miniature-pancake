import type { QualityGrade } from "@/lib/server/store";

export function QualityGradeBadge({ grade }: { grade: QualityGrade }) {
  const isResearchGrade = grade === "research_grade";
  return (
    <span
      className="inline-block shrink-0 rounded-full px-2.5 py-1 text-xs font-semibold"
      style={{
        background: isResearchGrade ? "var(--color-accent)" : "var(--color-border)",
        color: isResearchGrade ? "var(--color-accent-contrast)" : "var(--color-text-muted)",
      }}
    >
      {isResearchGrade ? "Research Grade" : "Needs ID"}
    </span>
  );
}
