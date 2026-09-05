"use client";

import Link from "next/link";
import { ModeToggle } from "@/components/mode-toggle";
import { LocaleSwitcher } from "@/components/locale-switcher";
import { useAuth } from "@/lib/auth-context";
import { useLocale } from "@/lib/locale-context";
import type { TranslationKey } from "@/lib/i18n/dictionary";

const NAV_LINKS: { href: string; key: TranslationKey }[] = [
  { href: "/explore", key: "nav.explore" },
  { href: "/identify", key: "nav.identify" },
  { href: "/observations", key: "nav.observations" },
  { href: "/journal", key: "nav.journal" },
  { href: "/guides", key: "nav.guides" },
  { href: "/projects", key: "nav.projects" },
];

export function TopBar() {
  const { user } = useAuth();
  const { t } = useLocale();
  const links =
    user?.role === "curator" ? [...NAV_LINKS, { href: "/curator", key: "nav.curator" as const }] : NAV_LINKS;

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
              {t(link.key)}
            </Link>
          ))}
        </nav>
        <div className="flex items-center gap-2">
          <LocaleSwitcher />
          <ModeToggle />
        </div>
      </div>
    </header>
  );
}
