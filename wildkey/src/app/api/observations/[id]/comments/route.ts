import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import {
  addCommentToObservation,
  getObservationById,
  listCommentsForObservation,
} from "@/lib/server/store";

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  return NextResponse.json({ comments: listCommentsForObservation(id) });
}

export async function POST(
  request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  if (!getObservationById(id)) {
    return NextResponse.json({ error: "Not found." }, { status: 404 });
  }

  const body = await request.json().catch(() => null);
  const text = typeof body?.body === "string" ? body.body.trim() : "";
  const kind = body?.kind === "agree" ? "agree" : "comment";

  if (kind === "comment" && text.length === 0) {
    return NextResponse.json({ error: "Comment body can't be empty." }, { status: 400 });
  }
  if (text.length > 2000) {
    return NextResponse.json({ error: "Comment is too long." }, { status: 400 });
  }

  const comment = addCommentToObservation(
    id,
    { id: user.id, email: user.email },
    kind === "agree" && text.length === 0 ? "Agreed with this ID." : text,
    kind,
  );
  if (!comment) return NextResponse.json({ error: "Not found." }, { status: 404 });

  return NextResponse.json({ comment }, { status: 201 });
}
