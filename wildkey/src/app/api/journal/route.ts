import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { createJournalPost, listJournalPosts } from "@/lib/server/store";

export async function GET() {
  return NextResponse.json({ posts: listJournalPosts() });
}

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const body = await request.json().catch(() => null);
  const title = typeof body?.title === "string" ? body.title.trim() : "";
  const postBody = typeof body?.body === "string" ? body.body.trim() : "";
  if (!title || !postBody) {
    return NextResponse.json({ error: "Title and body are required." }, { status: 400 });
  }
  if (title.length > 200 || postBody.length > 20000) {
    return NextResponse.json({ error: "Title or body too long." }, { status: 400 });
  }

  const post = createJournalPost(user.id, { title, body: postBody });
  return NextResponse.json({ post }, { status: 201 });
}
