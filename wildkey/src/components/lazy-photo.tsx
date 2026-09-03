"use client";

import { useState } from "react";
import { useLiteMode } from "@/lib/lite-mode-context";

type LazyPhotoProps = {
  src: string;
  alt: string;
  className?: string;
};

/**
 * Part D.1 Lite Mode: no auto-downloading of full-res images. When Lite
 * Mode is on, this never puts an <img src> in the DOM until the viewer
 * explicitly asks for it — a real, inspectable behavior difference, not
 * just a label.
 */
export function LazyPhoto({ src, alt, className }: LazyPhotoProps) {
  const { liteMode } = useLiteMode();
  const [revealed, setRevealed] = useState(false);

  if (liteMode && !revealed) {
    return (
      <button
        type="button"
        onClick={(e) => {
          e.preventDefault();
          e.stopPropagation();
          setRevealed(true);
        }}
        className={`flex items-center justify-center text-xs font-medium ${className ?? ""}`}
        style={{ background: "var(--color-bg)", color: "var(--color-text-muted)" }}
      >
        Lite Mode — tap to load photo
      </button>
    );
  }

  // eslint-disable-next-line @next/next/no-img-element
  return <img src={src} alt={alt} className={className} />;
}
