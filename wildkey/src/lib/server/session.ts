import { cookies } from "next/headers";
import { getUserBySessionToken } from "@/lib/server/store";

export const SESSION_COOKIE = "wildkey_session";

export async function getSessionUser() {
  const store = await cookies();
  return getUserBySessionToken(store.get(SESSION_COOKIE)?.value);
}

export async function setSessionCookie(token: string) {
  const store = await cookies();
  store.set(SESSION_COOKIE, token, {
    httpOnly: true,
    sameSite: "lax",
    secure: process.env.NODE_ENV === "production",
    path: "/",
    maxAge: 60 * 60 * 24 * 30,
  });
}

export async function clearSessionCookie() {
  const store = await cookies();
  store.delete(SESSION_COOKIE);
}
