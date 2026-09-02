import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { toPublicUser } from "@/lib/server/store";

export async function GET() {
  const user = await getSessionUser();
  return NextResponse.json({ user: user ? toPublicUser(user) : null });
}
