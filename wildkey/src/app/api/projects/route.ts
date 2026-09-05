import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { createProject, listProjects } from "@/lib/server/store";
import { getMockSpecies } from "@/lib/mock-species";
import { requiredString } from "@/lib/server/validate";

export async function GET() {
  return NextResponse.json({ projects: listProjects() });
}

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const body = await request.json().catch(() => null);
  const type = body?.type === "traditional" ? "traditional" : "collection";
  const title = requiredString(body?.title, 200);
  const description = requiredString(body?.description, 2000);
  const taxonFilter = Array.isArray(body?.taxonFilter)
    ? body.taxonFilter.filter((s: unknown) => typeof s === "string" && getMockSpecies(s))
    : [];

  if (!title || !description) {
    return NextResponse.json({ error: "Title and description are required, and within length limits." }, { status: 400 });
  }
  if (type === "collection" && taxonFilter.length === 0) {
    return NextResponse.json(
      { error: "Collection projects need at least one valid species in the filter." },
      { status: 400 },
    );
  }

  const project = createProject(user.id, { type, title, description, taxonFilter });
  return NextResponse.json({ project }, { status: 201 });
}
