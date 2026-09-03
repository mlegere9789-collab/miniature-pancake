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

export async function POST(request: Request) {
  const body = await request.json().catch(() => null);
  const email = typeof body?.email === "string" ? body.email : "";
  const password = typeof body?.password === "string" ? body.password : "";

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
