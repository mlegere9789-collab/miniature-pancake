import { spawn } from "node:child_process";
import { rm } from "node:fs/promises";
import path from "node:path";

const PROJECT_ROOT = path.resolve(import.meta.dirname, "..");
const PORT = process.env.WILDKEY_TEST_PORT ?? "3999";
export const BASE_URL = `http://localhost:${PORT}`;

let serverProcess;

/**
 * Boots a real `next start` server (against the already-built .next output
 * — run `npm run build` first) on a dedicated test port, against a fresh
 * SQLite database, and waits for it to accept requests. Every test in this
 * suite runs against the real HTTP API — no mocking of the store, the
 * routes, or the database.
 */
export async function startServer() {
  await rm(path.join(PROJECT_ROOT, ".data"), { recursive: true, force: true });

  // Invoke the local `next` binary directly rather than through `npx`:
  // npx interposes a wrapper process, and a SIGTERM to that wrapper does
  // not reliably reach the actual next-server grandchild it spawns,
  // leaking an orphaned server on every run.
  serverProcess = spawn(path.join(PROJECT_ROOT, "node_modules", ".bin", "next"), ["start", "-p", PORT], {
    cwd: PROJECT_ROOT,
    stdio: "pipe",
    detached: true,
    // Shrinks the account-deletion grace period from its real 14 days, and
    // the login-lockout window/duration from their real 15 minutes, down to
    // near-instant so the test suite can verify the actual purge/expiry
    // paths, not just the scheduling/locking APIs — see store.ts's
    // DELETION_GRACE_PERIOD_MS and LOGIN_LOCKOUT_MS.
    env: {
      ...process.env,
      WILDKEY_DELETION_GRACE_PERIOD_MS: "200",
      WILDKEY_LOGIN_LOCKOUT_MS: "300",
      WILDKEY_LOGIN_ATTEMPT_WINDOW_MS: "60000",
    },
  });
  // Never leave stdout/stderr un-drained: once the pipe buffer fills, the
  // child blocks on write() and everything downstream silently stalls —
  // caught the hard way running this suite for the first time.
  serverProcess.stdout.on("data", () => {});
  serverProcess.stderr.on("data", () => {});

  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    try {
      const res = await fetch(`${BASE_URL}/api/auth/me`);
      if (res.ok) return;
    } catch {
      // not up yet
    }
    await new Promise((r) => setTimeout(r, 300));
  }
  throw new Error("Server did not become ready within 30s");
}

export async function stopServer() {
  if (!serverProcess) return;
  // Kill the whole process group (negative pid), not just the immediate
  // child — `next start` itself can spawn further workers, and a plain
  // kill() on the child alone leaves those orphaned.
  try {
    process.kill(-serverProcess.pid, "SIGTERM");
  } catch {
    serverProcess.kill("SIGTERM");
  }
  await new Promise((resolve) => {
    serverProcess.once("exit", resolve);
    setTimeout(resolve, 3000);
  });
  try {
    process.kill(-serverProcess.pid, "SIGKILL");
  } catch {
    // already gone
  }
  await rm(path.join(PROJECT_ROOT, ".data"), { recursive: true, force: true });
}

/** A tiny cookie jar so each test account keeps its own session. */
export function createSession() {
  let cookie = "";
  return {
    async fetch(pathname, options = {}) {
      const headers = new Headers(options.headers ?? {});
      if (cookie) headers.set("cookie", cookie);
      // A real browser sends Origin on every same-origin POST/PATCH/DELETE
      // fetch — Node's fetch doesn't, so this stands in for that default,
      // the way any legitimate client of this API would behave. The CSRF
      // check in src/proxy.ts requires it on every state-changing request.
      if (!headers.has("origin")) headers.set("origin", BASE_URL);
      const res = await fetch(`${BASE_URL}${pathname}`, { ...options, headers, redirect: "manual" });
      const setCookie = res.headers.get("set-cookie");
      if (setCookie) cookie = setCookie.split(";")[0];
      return res;
    },
  };
}

let accountCounter = 0;

/**
 * Every test signup gets its own synthetic x-forwarded-for so the suite's
 * ~30+ signups (all from this one real loopback caller) never share a
 * single IP bucket against the real signup-rate-limit in store.ts — that
 * limit is exercised deliberately, on a fixed shared IP, by the "signup
 * rate limiting" test instead.
 */
export async function signUp(email) {
  const session = createSession();
  accountCounter += 1;
  const uniqueEmail = email ?? `test-${Date.now()}-${accountCounter}@example.com`;
  const res = await session.fetch("/api/auth/signup", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "x-forwarded-for": `10.${(accountCounter >> 16) & 255}.${(accountCounter >> 8) & 255}.${accountCounter & 255}`,
    },
    body: JSON.stringify({ email: uniqueEmail, password: "correcthorsebattery" }),
  });
  if (!res.ok) throw new Error(`Signup failed: ${res.status} ${await res.text()}`);
  const { user } = await res.json();
  return { session, user, email: uniqueEmail };
}
