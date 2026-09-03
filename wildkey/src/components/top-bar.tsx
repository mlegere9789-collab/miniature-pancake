"use client";

import Link from "next/link";
import { ModeToggle } from "@/components/mode-toggle";
import { useAuth } from "@/lib/auth-context";

const NAV_LINKS = [
  { href: "/explore", label: "Explore" },
  { href: "/identify", label: "Identify" },
  { href: "/observations", label: "My Observations" },
  { href: "/journal", label: "Journal" },
  { href: "/guides", label: "Guides" },
  { href: "/projects", label: "Projects" },
];

export function TopBar() {
  const { user } = useAuth();
  const links = user?.role === "curator" ? [...NAV_LINKS, { href: "/curator", label: "Curator" }] : NAV_LINKS;

  return (
    <header
      className="sticky top-0 z-10 border-b backdrop-blur"
      style={{ borderColor: "var(--color-border)", background: "color-mix(in srgb, var(--color-bg) 92%, transparent)" }}
    >
      <div className="mx-auto flex max-w-5xl items-center justify-between gap-4 px-4 py-3 sm:px-6">
        <Link href="/" className="font-display text-xl font-semibold tracking-tight">
          Wildkey
        </Link>
        <nav className="hidden gap-5 text-sm font-medium sm:flex" style={{ color: "var(--color-text-muted)" }}>
          {links.map((link) => (
            <Link key={link.href} href={link.href} className="hover:text-current">
              {link.label}
            </Link>
          ))}
        </nav>
        <ModeToggle />
      </div>
    </header>
  );
}
