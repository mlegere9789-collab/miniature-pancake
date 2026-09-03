import { NextResponse } from "next/server";
import { getSessionUser, clearSessionCookie } from "@/lib/server/session";
import { deleteUserAccount } from "@/lib/server/store";

export async function POST() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  deleteUserAccount(user.id);
  await clearSessionCookie();
  return NextResponse.json({ ok: true });
}
