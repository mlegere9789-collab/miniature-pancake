import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { createJournalPost, listJournalPosts } from "@/lib/server/store";
import { requiredString } from "@/lib/server/validate";

export async function GET() {
  return NextResponse.json({ posts: listJournalPosts() });
}

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const body = await request.json().catch(() => null);
  const title = requiredString(body?.title, 200);
  const postBody = requiredString(body?.body, 20000);
  if (!title || !postBody) {
    return NextResponse.json({ error: "Title and body are required, and within length limits." }, { status: 400 });
  }

  const post = createJournalPost(user.id, { title, body: postBody });
  return NextResponse.json({ post }, { status: 201 });
}
