import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import {
  deleteObservationForUser,
  getObservationById,
  getUserById,
  toPublicUser,
  updateObservationLicense,
  OBSERVATION_LICENSES,
  type ObservationLicense,
} from "@/lib/server/store";

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  const observation = getObservationById(id, user.id);
  if (!observation) return NextResponse.json({ error: "Not found." }, { status: 404 });

  const author = getUserById(observation.userId);
  return NextResponse.json({
    observation,
    author: author ? toPublicUser(author) : null,
  });
}

export async function PATCH(
  request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  const body = await request.json().catch(() => null);
  const license = body?.license;
  if (!(OBSERVATION_LICENSES as readonly unknown[]).includes(license)) {
    return NextResponse.json(
      { error: `License must be one of: ${OBSERVATION_LICENSES.join(", ")}.` },
      { status: 400 },
    );
  }

  const ok = updateObservationLicense(user.id, id, license as ObservationLicense);
  if (!ok) return NextResponse.json({ error: "Not found or not yours." }, { status: 404 });
  return NextResponse.json({ ok: true });
}

export async function DELETE(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const { id } = await params;
  deleteObservationForUser(user.id, id);
  return NextResponse.json({ ok: true });
}
