import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { deleteObservationForUser } from "@/lib/server/store";

export async function DELETE(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  deleteObservationForUser(user.id, id);
  return NextResponse.json({ ok: true });
}
