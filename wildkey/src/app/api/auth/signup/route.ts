import { NextResponse } from "next/server";
import {
  createUser,
  createSession,
  toPublicUser,
  isSignupRateLimited,
  recordSignupAttempt,
} from "@/lib/server/store";
import { setSessionCookie } from "@/lib/server/session";

// RFC 5321's own limit on an email address, and a generous but bounded cap
// on password length — scrypt has no built-in limit on input size, so an
// unbounded password field is a real CPU/memory hazard, not just untidy.
const MAX_EMAIL_LENGTH = 254;
const MAX_PASSWORD_LENGTH = 256;

/**
 * Best-effort caller IP: only trustworthy behind a reverse proxy that sets
 * this header itself (overwriting anything the client sent), which is the
 * deployment this app assumes — see isSignupRateLimited's own honest
 * caveat about what this can't stop.
 */
function callerIp(request: Request): string {
  const forwardedFor = request.headers.get("x-forwarded-for");
  if (forwardedFor) return forwardedFor.split(",")[0].trim();
  return request.headers.get("x-real-ip") ?? "unknown";
}

export async function POST(request: Request) {
  const ip = callerIp(request);
  if (isSignupRateLimited(ip)) {
    return NextResponse.json(
      { error: "Too many accounts created recently. Try again later." },
      { status: 429 },
    );
  }
  recordSignupAttempt(ip);

  const body = await request.json().catch(() => null);
  const email = typeof body?.email === "string" ? body.email : "";
  const password = typeof body?.password === "string" ? body.password : "";

  if (
    !email.includes("@") ||
    email.length > MAX_EMAIL_LENGTH ||
    password.length < 8 ||
    password.length > MAX_PASSWORD_LENGTH
  ) {
    return NextResponse.json(
      { error: `Enter a valid email and a password between 8 and ${MAX_PASSWORD_LENGTH} characters.` },
      { status: 400 },
    );
  }

  try {
    const user = createUser(email, password);
    const token = createSession(user.id);
    await setSessionCookie(token);
    return NextResponse.json({ user: toPublicUser(user) }, { status: 201 });
  } catch (err) {
    const message = err instanceof Error ? err.message : "Could not create account.";
    return NextResponse.json({ error: message }, { status: 409 });
  }
}
