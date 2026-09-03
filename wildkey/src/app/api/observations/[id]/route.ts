import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import {
  deleteObservationForUser,
  getObservationById,
  getUserById,
  toPublicUser,
} from "@/lib/server/store";

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  const observation = getObservationById(id);
  if (!observation) return NextResponse.json({ error: "Not found." }, { status: 404 });

  const author = getUserById(observation.userId);
  return NextResponse.json({
    observation,
    author: author ? toPublicUser(author) : null,
  });
}

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
