import Link from "next/link";

const TABS = [
  { href: "/camera", label: "Camera" },
  { href: "/explore", label: "Explore" },
  { href: "/observations", label: "Observations" },
  { href: "/activity", label: "Activity" },
  { href: "/me", label: "Me" },
];

export function BottomNav() {
  return (
    <nav
      className="sticky bottom-0 z-10 grid grid-cols-5 border-t sm:hidden"
      style={{ borderColor: "var(--color-border)", background: "var(--color-surface)" }}
      aria-label="Primary"
    >
      {TABS.map((tab) => (
        <Link
          key={tab.href}
          href={tab.href}
          className="flex flex-col items-center gap-1 py-2 text-xs font-medium"
          style={{ color: "var(--color-text-muted)" }}
        >
          {tab.label}
        </Link>
      ))}
    </nav>
  );
}
