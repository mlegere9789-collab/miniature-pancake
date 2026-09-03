import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { getProject, joinProject, leaveProject } from "@/lib/server/store";

export async function POST(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  const project = getProject(id);
  if (!project) return NextResponse.json({ error: "Not found." }, { status: 404 });
  if (project.type !== "traditional") {
    return NextResponse.json({ error: "Collection projects don't have membership." }, { status: 400 });
  }

  joinProject(id, user.id);
  return NextResponse.json({ ok: true });
}

export async function DELETE(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  leaveProject(id, user.id);
  return NextResponse.json({ ok: true });
}
