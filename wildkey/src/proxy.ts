import { NextResponse } from "next/server";
import type { NextRequest } from "next/server";

/**
 * CSRF hardening for every state-changing API request (Part G security
 * gate). SameSite=lax on the session cookie (src/lib/server/session.ts)
 * already blocks the classic form-based CSRF case — a cross-site POST
 * doesn't carry the cookie at all — but that's one browser behavior this
 * app is relying on, not a check the server itself makes. This is the
 * belt-and-suspenders version: reject any non-safe request whose Origin
 * (or, failing that, Referer) doesn't match this app's own origin,
 * checked here in one place rather than copy-pasted into every route.
 *
 * A real browser reliably sends Origin on same-origin POST/PATCH/DELETE
 * fetch/XHR calls (this app never submits a plain HTML form), so
 * requiring it — and rejecting anything missing both headers — costs
 * nothing for a legitimate client and closes off a cross-site attacker
 * forging a request without an Origin/Referer a browser wouldn't let them
 * spoof anyway.
 */
const SAFE_METHODS = new Set(["GET", "HEAD", "OPTIONS"]);

function requestOrigin(request: NextRequest): string | null {
  const origin = request.headers.get("origin");
  if (origin) return origin;

  const referer = request.headers.get("referer");
  if (!referer) return null;
  try {
    return new URL(referer).origin;
  } catch {
    return null;
  }
}

export function proxy(request: NextRequest) {
  if (SAFE_METHODS.has(request.method)) return NextResponse.next();

  const origin = requestOrigin(request);
  if (!origin || origin !== request.nextUrl.origin) {
    return NextResponse.json({ error: "Cross-site request blocked." }, { status: 403 });
  }

  return NextResponse.next();
}

export const config = {
  matcher: "/api/:path*",
};
