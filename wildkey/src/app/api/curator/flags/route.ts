import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { listOpenFlags } from "@/lib/server/store";

export async function GET() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });
  if (user.role !== "curator") {
    return NextResponse.json({ error: "Curator access required." }, { status: 403 });
  }
  return NextResponse.json({ flags: listOpenFlags() });
}
