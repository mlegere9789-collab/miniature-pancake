import { NextResponse } from "next/server";
import {
  verifyCredentials,
  createSession,
  toPublicUser,
  getLoginLockoutSeconds,
  registerLoginFailure,
  registerLoginSuccess,
} from "@/lib/server/store";
import { setSessionCookie } from "@/lib/server/session";

// Same cap signup enforces — scrypt has no built-in input-size limit, so an
// unbounded password field submitted to login is the same CPU/memory
// hazard, checked here before any hashing runs.
const MAX_EMAIL_LENGTH = 254;
const MAX_PASSWORD_LENGTH = 256;

export async function POST(request: Request) {
  const body = await request.json().catch(() => null);
  const rawEmail = typeof body?.email === "string" ? body.email : "";
  const rawPassword = typeof body?.password === "string" ? body.password : "";
  const email = rawEmail.slice(0, MAX_EMAIL_LENGTH);
  const password = rawPassword.slice(0, MAX_PASSWORD_LENGTH);

  if (rawEmail.length > MAX_EMAIL_LENGTH || rawPassword.length > MAX_PASSWORD_LENGTH) {
    return NextResponse.json({ error: "Incorrect email or password." }, { status: 401 });
  }

  // Real brute-force protection: too many recent failures against this
  // email locks it out for a while, checked before touching the password
  // hash at all (previously there was no limit whatsoever on login
  // attempts).
  const lockedSeconds = getLoginLockoutSeconds(email);
  if (lockedSeconds !== null) {
    return NextResponse.json(
      { error: "Too many failed attempts. Try again later." },
      { status: 429, headers: { "Retry-After": String(lockedSeconds) } },
    );
  }

  const user = verifyCredentials(email, password);
  if (!user) {
    registerLoginFailure(email);
    return NextResponse.json({ error: "Incorrect email or password." }, { status: 401 });
  }
  registerLoginSuccess(email);

  const token = createSession(user.id);
  await setSessionCookie(token);
  return NextResponse.json({ user: toPublicUser(user) });
}
