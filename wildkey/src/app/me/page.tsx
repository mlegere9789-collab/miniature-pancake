import Link from "next/link";
import { StubPage } from "@/components/stub-page";

export default function MePage() {
  return (
    <div className="flex flex-1 flex-col">
      <StubPage
        title="Me"
        description="Profile, badges, and settings — including the mode toggle, mirrored here for reachability."
        planned={[
          "Profile stats and badges (non-predatory, no dark patterns)",
          "Account anonymization option (keep contribution history, remove identity)",
          "Accessibility settings",
        ]}
      />
      <div className="mx-auto -mt-2 flex w-full max-w-2xl gap-3 px-4 pb-8 sm:px-6">
        <Link
          href="/settings/lite-mode"
          className="rounded-full border px-4 py-2 text-sm font-semibold"
          style={{ borderColor: "var(--color-border)" }}
        >
          Lite Mode
        </Link>
        <Link
          href="/settings/data"
          className="rounded-full border px-4 py-2 text-sm font-semibold"
          style={{ borderColor: "var(--color-border)" }}
        >
          Data & export
        </Link>
      </div>
    </div>
  );
}
