import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import {
  deleteObservationForUser,
  getObservationById,
  getUserById,
  toPublicUser,
  updateObservationLicense,
  updateObservationAnnotations,
  OBSERVATION_LICENSES,
  LIFE_STAGES,
  SEXES,
  PHENOLOGIES,
  type ObservationLicense,
  type LifeStage,
  type Sex,
  type Phenology,
} from "@/lib/server/store";
import { getMockSpecies } from "@/lib/mock-species";
import { annotationsApplicableFor } from "@/lib/observation-annotations";

type AnnotationField<T extends string> =
  | { present: false }
  | { present: true; valid: true; value: T | null }
  | { present: true; valid: false };

/** A key absent from the body means "leave unchanged"; present means it must be null (clear it) or one of the allowed values. */
function parseOptionalAnnotation<T extends string>(
  body: Record<string, unknown>,
  key: string,
  allowed: readonly T[],
): AnnotationField<T> {
  if (!(key in body)) return { present: false };
  const raw = body[key];
  if (raw === null) return { present: true, valid: true, value: null };
  if ((allowed as readonly unknown[]).includes(raw)) return { present: true, valid: true, value: raw as T };
  return { present: true, valid: false };
}

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
  const body = (await request.json().catch(() => null)) ?? {};

  const hasLicense = "license" in body;
  if (hasLicense && !(OBSERVATION_LICENSES as readonly unknown[]).includes(body.license)) {
    return NextResponse.json(
      { error: `License must be one of: ${OBSERVATION_LICENSES.join(", ")}.` },
      { status: 400 },
    );
  }

  const lifeStage = parseOptionalAnnotation<LifeStage>(body, "lifeStage", LIFE_STAGES);
  const sex = parseOptionalAnnotation<Sex>(body, "sex", SEXES);
  const phenology = parseOptionalAnnotation<Phenology>(body, "phenology", PHENOLOGIES);
  for (const [name, field] of [
    ["lifeStage", lifeStage],
    ["sex", sex],
    ["phenology", phenology],
  ] as const) {
    if (field.present && !field.valid) {
      return NextResponse.json({ error: `Invalid value for ${name}.` }, { status: 400 });
    }
  }

  // A non-null annotation value must actually apply to this observation's
  // taxon group (a plant can't have a "sex," an animal has no "phenology").
  if (lifeStage.present || sex.present || phenology.present) {
    const existing = getObservationById(id);
    if (!existing) return NextResponse.json({ error: "Not found or not yours." }, { status: 404 });
    const applicable = annotationsApplicableFor(getMockSpecies(existing.taxonSlug)?.taxonGroup ?? "fungus");
    if (lifeStage.present && lifeStage.valid && lifeStage.value !== null && !applicable.lifeStage) {
      return NextResponse.json({ error: "Life stage doesn't apply to this species." }, { status: 400 });
    }
    if (sex.present && sex.valid && sex.value !== null && !applicable.sex) {
      return NextResponse.json({ error: "Sex doesn't apply to this species." }, { status: 400 });
    }
    if (phenology.present && phenology.valid && phenology.value !== null && !applicable.phenology) {
      return NextResponse.json({ error: "Phenology doesn't apply to this species." }, { status: 400 });
    }
  }

  if (!hasLicense && !lifeStage.present && !sex.present && !phenology.present) {
    return NextResponse.json({ error: "Nothing to update." }, { status: 400 });
  }

  let ok = true;
  if (hasLicense) {
    ok = updateObservationLicense(user.id, id, body.license as ObservationLicense) && ok;
  }
  if (lifeStage.present || sex.present || phenology.present) {
    ok =
      updateObservationAnnotations(user.id, id, {
        ...(lifeStage.present && lifeStage.valid ? { lifeStage: lifeStage.value } : {}),
        ...(sex.present && sex.valid ? { sex: sex.value } : {}),
        ...(phenology.present && phenology.valid ? { phenology: phenology.value } : {}),
      }) && ok;
  }

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
