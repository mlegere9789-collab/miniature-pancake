import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { flagObservation } from "@/lib/server/store";

export async function POST(
  request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  const body = await request.json().catch(() => null);
  const reason = typeof body?.reason === "string" ? body.reason.trim() : "";
  if (!reason) {
    return NextResponse.json({ error: "A reason is required to flag an observation." }, { status: 400 });
  }
  if (reason.length > 1000) {
    return NextResponse.json({ error: "Reason is too long." }, { status: 400 });
  }

  const flag = flagObservation(id, user.id, reason);
  if (!flag) return NextResponse.json({ error: "Observation not found." }, { status: 404 });
  return NextResponse.json({ flag }, { status: 201 });
}
