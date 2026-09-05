import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { listObservationsInBounds } from "@/lib/server/store";

/**
 * Map data layer read endpoint (Part G/C.3) — bounding-box query over
 * observation coordinates. No map UI calls this yet (no basemap available
 * in this environment, see docs/remaining-systems-design.md), but the
 * query itself is real and ready for one.
 */
export async function GET(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const url = new URL(request.url);
  const rawParams = {
    minLat: url.searchParams.get("minLat"),
    maxLat: url.searchParams.get("maxLat"),
    minLng: url.searchParams.get("minLng"),
    maxLng: url.searchParams.get("maxLng"),
  };

  // Number(null) is 0, not NaN — a missing param must be checked for
  // explicitly, not just left to coerce silently into a valid-looking 0.
  if (Object.values(rawParams).some((v) => v === null || v.trim() === "")) {
    return NextResponse.json(
      { error: "minLat, maxLat, minLng, and maxLng query params are all required." },
      { status: 400 },
    );
  }

  const minLat = Number(rawParams.minLat);
  const maxLat = Number(rawParams.maxLat);
  const minLng = Number(rawParams.minLng);
  const maxLng = Number(rawParams.maxLng);

  if ([minLat, maxLat, minLng, maxLng].some((n) => Number.isNaN(n))) {
    return NextResponse.json(
      { error: "minLat, maxLat, minLng, and maxLng must be valid numbers." },
      { status: 400 },
    );
  }

  return NextResponse.json({
    observations: listObservationsInBounds({ minLat, maxLat, minLng, maxLng }, user.id),
  });
}
