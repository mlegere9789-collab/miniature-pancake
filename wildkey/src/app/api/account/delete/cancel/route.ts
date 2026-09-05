import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { cancelAccountDeletion } from "@/lib/server/store";

export async function POST() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const cancelled = cancelAccountDeletion(user.id);
  if (!cancelled) {
    return NextResponse.json({ error: "No deletion is scheduled for this account." }, { status: 400 });
  }
  return NextResponse.json({ ok: true });
}
