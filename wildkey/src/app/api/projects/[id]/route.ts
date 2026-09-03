import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import {
  deleteProject,
  getProject,
  isProjectMember,
  listObservationsMatchingProject,
  listProjectMembers,
} from "@/lib/server/store";

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  const { id } = await params;
  const project = getProject(id);
  if (!project) return NextResponse.json({ error: "Not found." }, { status: 404 });

  if (project.type === "collection") {
    return NextResponse.json({
      project,
      observations: listObservationsMatchingProject(project.taxonFilter, user?.id ?? null),
    });
  }

  return NextResponse.json({
    project,
    members: listProjectMembers(id),
    isMember: user ? isProjectMember(id, user.id) : false,
  });
}

export async function DELETE(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  deleteProject(user.id, id);
  return NextResponse.json({ ok: true });
}
