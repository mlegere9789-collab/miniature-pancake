import type { QualityGrade } from "@/lib/server/store";

export function QualityGradeBadge({ grade }: { grade: QualityGrade }) {
  const isResearchGrade = grade === "research_grade";
  return (
    <span
      className="inline-block shrink-0 rounded-full px-2.5 py-1 text-xs font-semibold"
      style={{
        background: isResearchGrade ? "var(--color-accent)" : "var(--color-border)",
        // text-muted on a solid --color-border fill only reaches ~4.4:1,
        // short of WCAG AA's 4.5:1 for small text — full-contrast text
        // instead, checked not assumed.
        color: isResearchGrade ? "var(--color-accent-contrast)" : "var(--color-text)",
      }}
    >
      {isResearchGrade ? "Research Grade" : "Needs ID"}
    </span>
  );
}
