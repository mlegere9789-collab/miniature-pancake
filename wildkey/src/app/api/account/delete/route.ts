import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { scheduleAccountDeletion } from "@/lib/server/store";

/**
 * Schedules deletion 14 days out instead of erasing immediately — the
 * grace period / undo window this app previously documented as not built.
 * The account stays fully intact and signed in; cancelling (POST
 * /api/account/delete/cancel) is the undo path.
 */
export async function POST() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const purgeAt = scheduleAccountDeletion(user.id);
  return NextResponse.json({ ok: true, purgeAt });
}
