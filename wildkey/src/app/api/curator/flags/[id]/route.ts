import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { resolveFlag } from "@/lib/server/store";

export async function POST(
  request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });
  if (user.role !== "curator") {
    return NextResponse.json({ error: "Curator access required." }, { status: 403 });
  }

  const { id } = await params;
  const body = await request.json().catch(() => null);
  const action = body?.action === "dismissed" ? "dismissed" : "resolved";
  const reason = typeof body?.reason === "string" ? body.reason.trim() : "";

  if (!reason) {
    return NextResponse.json(
      { error: "A written reason is required for any curator action." },
      { status: 400 },
    );
  }
  if (reason.length > 1000) {
    return NextResponse.json({ error: "Reason is too long." }, { status: 400 });
  }

  const ok = resolveFlag(id, user.id, action, reason);
  if (!ok) {
    return NextResponse.json({ error: "Flag not found or already resolved." }, { status: 404 });
  }
  return NextResponse.json({ ok: true });
}
