import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import {
  createObservationForUser,
  listObservationsForUser,
  OBSERVATION_LICENSES,
  DEFAULT_OBSERVATION_LICENSE,
  type ObservationLicense,
} from "@/lib/server/store";
import { requiredString, optionalString, boundedNumber } from "@/lib/server/validate";

function parseLicense(value: unknown): ObservationLicense {
  return (OBSERVATION_LICENSES as readonly unknown[]).includes(value)
    ? (value as ObservationLicense)
    : DEFAULT_OBSERVATION_LICENSE;
}

export async function GET() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });
  return NextResponse.json({ observations: listObservationsForUser(user.id) });
}

// A base64 photo data URL this size decodes to roughly 15MB of image bytes
// — generous for a phone camera photo, bounded against someone sending an
// arbitrarily large blob just because the field is a string.
const MAX_PHOTO_DATA_URL_LENGTH = 20_000_000;

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const body = await request.json().catch(() => null);
  const photoDataUrl = requiredString(body?.photoDataUrl, MAX_PHOTO_DATA_URL_LENGTH);
  const commonName = requiredString(body?.commonName, 200);
  const scientificName = requiredString(body?.scientificName, 200);
  const taxonSlug = requiredString(body?.taxonSlug, 100);
  // Confidence is a model probability — must be a real, finite number in [0, 1],
  // not just anything `typeof x === "number"` (which NaN and Infinity both pass).
  const confidence = boundedNumber(body?.confidence, 0, 1);

  if (photoDataUrl === null || commonName === null || scientificName === null || taxonSlug === null || confidence === null) {
    return NextResponse.json({ error: "Missing or invalid observation fields." }, { status: 400 });
  }

  const isWild = typeof body?.isWild === "boolean" ? body.isWild : true;
  const locationName = optionalString(body?.locationName, 200);
  const notes = optionalString(body?.notes, 2000);
  const lat = boundedNumber(body?.lat, -90, 90);
  const lng = boundedNumber(body?.lng, -180, 180);
  const license = parseLicense(body?.license);

  const observation = createObservationForUser(user.id, {
    photoDataUrl,
    commonName,
    scientificName,
    confidence,
    taxonSlug,
    isWild,
    locationName,
    notes,
    lat,
    lng,
    license,
  });
  return NextResponse.json({ observation }, { status: 201 });
}
