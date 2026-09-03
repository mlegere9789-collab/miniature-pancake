import Link from "next/link";
import type { Species } from "@/lib/mock-species";

type IdResultCardProps = {
  species: Species | null;
  confidence: number; // 0-1
  /** The model's real top-1 label, shown honestly when it didn't map to a known species. */
  rawLabel?: string;
};

const CONFIDENT_THRESHOLD = 0.7;
const VOWEL_SOUND = /^[aeiou]/i;

export function IdResultCard({ species, confidence, rawLabel }: IdResultCardProps) {
  if (!species) {
    return (
      <div
        className="overflow-hidden rounded-lg border p-4"
        style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
      >
        <span
          className="rounded-full px-2.5 py-1 text-xs font-semibold"
          style={{ background: "var(--color-warning)", color: "var(--color-accent-contrast)" }}
        >
          No confident match
        </span>
        <h2 className="mt-3 font-display text-xl font-semibold">Not in Wildkey&rsquo;s sample catalog</h2>
        <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
          The identification model ran a real classification pass and didn&rsquo;t land on any of the
          species Wildkey currently recognizes{rawLabel ? ` — its closest general guess was “${rawLabel}”` : ""}.
          This is an honest result, not an error: Wildkey&rsquo;s sample species set is intentionally small
          right now (see the README), so most real-world photos won&rsquo;t match yet.
        </p>
      </div>
    );
  }

  const isConfident = confidence >= CONFIDENT_THRESHOLD;
  const pct = Math.round(confidence * 100);
  const article = VOWEL_SOUND.test(species.taxonGroup) ? "an" : "a";

  return (
    <div
      className="overflow-hidden rounded-lg border"
      style={{ borderColor: "var(--color-border)", background: "var(--color-surface)", boxShadow: "var(--shadow-1)" }}
    >
      <div className="flex items-center justify-between px-4 pt-4">
        <span
          className="rounded-full px-2.5 py-1 text-xs font-semibold"
          style={{
            background: isConfident ? "var(--color-accent)" : "var(--color-warning)",
            color: "var(--color-accent-contrast)",
          }}
        >
          {isConfident ? `${pct}% confident` : `Not confident — ${pct}%`}
        </span>
        {species.danger && (
          <span
            className="rounded-full px-2.5 py-1 text-xs font-semibold"
            style={{ background: "var(--color-danger)", color: "var(--color-accent-contrast)" }}
          >
            ⚠ Use caution
          </span>
        )}
      </div>

      <div className="p-4">
        {isConfident ? (
          <>
            <h2 className="font-display text-2xl font-semibold">{species.commonName}</h2>
            <p className="text-sm italic" style={{ color: "var(--color-text-muted)" }}>
              {species.scientificName}
            </p>
          </>
        ) : (
          <>
            <h2 className="font-display text-2xl font-semibold">
              Likely {article} {species.taxonGroup} — probably in this family
            </h2>
            <p className="mt-1 text-sm" style={{ color: "var(--color-text-muted)" }}>
              We&rsquo;re not confident enough to name an exact species. Our best guess is{" "}
              <strong>{species.commonName}</strong> ({species.scientificName}), but treat this as a
              starting point, not a confirmed ID.
            </p>
          </>
        )}

        <p className="mt-3 text-sm">{species.description}</p>
        {species.danger && (
          <p className="mt-3 text-sm font-medium" style={{ color: "var(--color-danger)" }}>
            {species.danger}
          </p>
        )}

        <div className="mt-4 flex items-center justify-between text-sm" style={{ color: "var(--color-text-muted)" }}>
          <span>{species.seasonality}</span>
          <span>{species.observationCount.toLocaleString()} sightings worldwide</span>
        </div>

        <Link
          href={`/species/${species.slug}`}
          className="mt-4 inline-block text-sm font-semibold"
          style={{ color: "var(--color-accent)" }}
        >
          View full species profile →
        </Link>
      </div>
    </div>
  );
}
