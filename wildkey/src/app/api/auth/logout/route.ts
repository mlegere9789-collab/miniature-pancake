import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { destroySession } from "@/lib/server/store";
import { SESSION_COOKIE, clearSessionCookie } from "@/lib/server/session";

export async function POST() {
  const store = await cookies();
  const token = store.get(SESSION_COOKIE)?.value;
  if (token) destroySession(token);
  await clearSessionCookie();
  return NextResponse.json({ ok: true });
}
