import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { listObservationsNeedingId } from "@/lib/server/store";

export async function GET() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });
  return NextResponse.json({ observations: listObservationsNeedingId(user.id) });
}
