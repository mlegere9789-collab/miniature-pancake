import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { createGuide, listGuides } from "@/lib/server/store";
import { getMockSpecies } from "@/lib/mock-species";

export async function GET() {
  return NextResponse.json({ guides: listGuides() });
}

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const body = await request.json().catch(() => null);
  const title = typeof body?.title === "string" ? body.title.trim() : "";
  const description = typeof body?.description === "string" ? body.description.trim() : "";
  const taxonSlugs = Array.isArray(body?.taxonSlugs)
    ? body.taxonSlugs.filter((s: unknown) => typeof s === "string" && getMockSpecies(s))
    : [];

  if (!title || !description || taxonSlugs.length === 0) {
    return NextResponse.json(
      { error: "Title, description, and at least one valid species are required." },
      { status: 400 },
    );
  }

  const guide = createGuide(user.id, { title, description, taxonSlugs });
  return NextResponse.json({ guide }, { status: 201 });
}
