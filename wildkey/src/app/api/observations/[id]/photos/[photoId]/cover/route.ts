import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { setObservationCoverPhoto } from "@/lib/server/store";

export async function POST(
  _request: Request,
  { params }: { params: Promise<{ id: string; photoId: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id, photoId } = await params;
  const ok = setObservationCoverPhoto(user.id, id, photoId);
  if (!ok) return NextResponse.json({ error: "Not found or not yours." }, { status: 404 });
  return NextResponse.json({ ok: true });
}
