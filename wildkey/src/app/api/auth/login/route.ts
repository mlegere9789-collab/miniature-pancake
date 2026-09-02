import { NextResponse } from "next/server";
import { verifyCredentials, createSession, toPublicUser } from "@/lib/server/store";
import { setSessionCookie } from "@/lib/server/session";

export async function POST(request: Request) {
  const body = await request.json().catch(() => null);
  const email = typeof body?.email === "string" ? body.email : "";
  const password = typeof body?.password === "string" ? body.password : "";

  const user = verifyCredentials(email, password);
  if (!user) {
    return NextResponse.json({ error: "Incorrect email or password." }, { status: 401 });
  }

  const token = createSession(user.id);
  await setSessionCookie(token);
  return NextResponse.json({ user: toPublicUser(user) });
}
