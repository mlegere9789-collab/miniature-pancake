import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { deleteGuide, getGuide } from "@/lib/server/store";

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const { id } = await params;
  const guide = getGuide(id);
  if (!guide) return NextResponse.json({ error: "Not found." }, { status: 404 });
  return NextResponse.json({ guide });
}

export async function DELETE(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  deleteGuide(user.id, id);
  return NextResponse.json({ ok: true });
}
