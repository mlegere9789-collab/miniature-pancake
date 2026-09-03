"use client";

import { useLocale } from "@/lib/locale-context";
import { LOCALES, LOCALE_LABELS } from "@/lib/i18n/dictionary";

export function LocaleSwitcher() {
  const { locale, setLocale } = useLocale();
  return (
    <select
      aria-label="Language"
      value={locale}
      onChange={(e) => setLocale(e.target.value as typeof locale)}
      className="rounded border bg-transparent px-2 py-1 text-xs font-medium"
      style={{ borderColor: "var(--color-border)", color: "var(--color-text-muted)" }}
    >
      {LOCALES.map((l) => (
        <option key={l} value={l}>
          {LOCALE_LABELS[l]}
        </option>
      ))}
    </select>
  );
}
