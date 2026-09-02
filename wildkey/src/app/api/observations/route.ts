import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { createObservationForUser, listObservationsForUser } from "@/lib/server/store";

export async function GET() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });
  return NextResponse.json({ observations: listObservationsForUser(user.id) });
}

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const body = await request.json().catch(() => null);
  const { photoDataUrl, commonName, scientificName, confidence, taxonSlug } = body ?? {};
  if (
    typeof photoDataUrl !== "string" ||
    typeof commonName !== "string" ||
    typeof scientificName !== "string" ||
    typeof confidence !== "number" ||
    typeof taxonSlug !== "string"
  ) {
    return NextResponse.json({ error: "Missing or invalid observation fields." }, { status: 400 });
  }

  const observation = createObservationForUser(user.id, {
    photoDataUrl,
    commonName,
    scientificName,
    confidence,
    taxonSlug,
  });
  return NextResponse.json({ observation }, { status: 201 });
}
