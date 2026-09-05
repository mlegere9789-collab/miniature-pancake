import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { demoteCurator } from "@/lib/server/store";

export async function POST(request: Request) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });
  if (user.role !== "curator") {
    return NextResponse.json({ error: "Curator access required." }, { status: 403 });
  }

  const body = await request.json().catch(() => null);
  const email = typeof body?.email === "string" ? body.email.trim() : "";
  const reason = typeof body?.reason === "string" ? body.reason.trim() : "";

  if (!email) return NextResponse.json({ error: "An email is required." }, { status: 400 });
  if (!reason) {
    return NextResponse.json(
      { error: "A written reason is required for any curator action." },
      { status: 400 },
    );
  }
  if (reason.length > 1000) {
    return NextResponse.json({ error: "Reason is too long." }, { status: 400 });
  }

  const result = demoteCurator(user.id, email, reason);
  if (!result.ok) {
    return NextResponse.json({ error: result.error }, { status: 400 });
  }
  return NextResponse.json({ ok: true });
}
