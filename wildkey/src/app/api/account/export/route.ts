import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { listCommentsByUser, listObservationsForUser, toPublicUser } from "@/lib/server/store";

/**
 * Part D.1: one-tap full data export. Everything this account owns —
 * profile, observations, and authored comments — as a single JSON file
 * the user can keep, migrate with, or audit. No partial exports: if this
 * account has data anywhere in the store, it's in here.
 */
export async function GET() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const payload = {
    exportedAt: new Date().toISOString(),
    account: toPublicUser(user),
    observations: listObservationsForUser(user.id),
    comments: listCommentsByUser(user.id),
  };

  const filename = `wildkey-export-${user.email.replace(/[^a-z0-9]/gi, "_")}.json`;
  return new NextResponse(JSON.stringify(payload, null, 2), {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "Content-Disposition": `attachment; filename="${filename}"`,
    },
  });
}
