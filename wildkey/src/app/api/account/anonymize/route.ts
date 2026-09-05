import { NextResponse } from "next/server";
import { getSessionUser, clearSessionCookie } from "@/lib/server/session";
import { anonymizeUserAccount } from "@/lib/server/store";

export async function POST() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const anonymizedEmail = anonymizeUserAccount(user.id);
  await clearSessionCookie();
  return NextResponse.json({ ok: true, anonymizedEmail });
}
