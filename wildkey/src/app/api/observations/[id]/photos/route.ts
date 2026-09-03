import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { addObservationPhotos, MAX_PHOTOS_PER_OBSERVATION } from "@/lib/server/store";
import { requiredString } from "@/lib/server/validate";

// Matches the cap enforced in /api/observations for the initial upload.
const MAX_PHOTO_DATA_URL_LENGTH = 20_000_000;

export async function POST(
  request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  const body = await request.json().catch(() => null);
  const raw = Array.isArray(body?.photoDataUrls) ? body.photoDataUrls : [];
  if (raw.length === 0) {
    return NextResponse.json({ error: "No photos provided." }, { status: 400 });
  }

  const photoDataUrls: string[] = [];
  for (const item of raw) {
    const url = requiredString(item, MAX_PHOTO_DATA_URL_LENGTH);
    if (url === null) {
      return NextResponse.json({ error: "One of the photos is invalid." }, { status: 400 });
    }
    photoDataUrls.push(url);
  }

  const result = addObservationPhotos(user.id, id, photoDataUrls);
  if (!result.ok) {
    if (result.reason === "not_found") {
      return NextResponse.json({ error: "Not found or not yours." }, { status: 404 });
    }
    return NextResponse.json(
      { error: `An observation can have at most ${MAX_PHOTOS_PER_OBSERVATION} photos.` },
      { status: 400 },
    );
  }
  return NextResponse.json({ ok: true }, { status: 201 });
}
