import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { deleteJournalPost, getJournalPost, updateJournalPost } from "@/lib/server/store";
import { requiredString } from "@/lib/server/validate";

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const { id } = await params;
  const post = getJournalPost(id);
  if (!post) return NextResponse.json({ error: "Not found." }, { status: 404 });
  return NextResponse.json({ post });
}

export async function PATCH(
  request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  const body = await request.json().catch(() => null);
  const title = requiredString(body?.title, 200);
  const postBody = requiredString(body?.body, 20000);
  if (!title || !postBody) {
    return NextResponse.json({ error: "Title and body are required, and within length limits." }, { status: 400 });
  }

  const post = updateJournalPost(user.id, id, { title, body: postBody });
  if (!post) return NextResponse.json({ error: "Not found or not yours." }, { status: 404 });
  return NextResponse.json({ post });
}

export async function DELETE(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  deleteJournalPost(user.id, id);
  return NextResponse.json({ ok: true });
}
