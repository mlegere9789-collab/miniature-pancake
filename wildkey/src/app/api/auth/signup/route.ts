import { NextResponse } from "next/server";
import { createUser, createSession, toPublicUser } from "@/lib/server/store";
import { setSessionCookie } from "@/lib/server/session";

export async function POST(request: Request) {
  const body = await request.json().catch(() => null);
  const email = typeof body?.email === "string" ? body.email : "";
  const password = typeof body?.password === "string" ? body.password : "";

  if (!email.includes("@") || password.length < 8) {
    return NextResponse.json(
      { error: "Enter a valid email and a password of at least 8 characters." },
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
