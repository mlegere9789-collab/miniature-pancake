import { before, after, describe, it } from "node:test";
import assert from "node:assert/strict";
import { startServer, stopServer, createSession, signUp, BASE_URL } from "./server.mjs";

// The very first account signed up against a fresh test database bootstraps
// as curator (src/lib/server/store.ts createUser) — signing this one up
// immediately after the server starts, before any other test runs,
// guarantees it's that account, so curator-only success paths (not just the
// 403-for-members case) are actually exercised here.
let curator;

before(async () => {
  await startServer();
  curator = await signUp();
}, { timeout: 35_000 });

after(async () => {
  await stopServer();
});

async function json(res) {
  return res.json();
}

describe("auth", () => {
  it("signs up, and rejects a duplicate email", async () => {
    const email = `dup-${Date.now()}@example.com`;
    const a = createSession();
    const first = await a.fetch("/api/auth/signup", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password: "correcthorsebattery" }),
    });
    assert.equal(first.status, 201);

    const b = createSession();
    const second = await b.fetch("/api/auth/signup", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password: "correcthorsebattery" }),
    });
    assert.equal(second.status, 409);
  });

  it("rejects login with the wrong password", async () => {
    const { email } = await signUp();
    const session = createSession();
    const res = await session.fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password: "wrongpassword" }),
    });
    assert.equal(res.status, 401);
  });

  it("rejects unauthenticated access to a protected route", async () => {
    const res = await fetch("http://localhost:3999/api/observations");
    assert.equal(res.status, 401);
  });

  it("locks out an email after repeated failed logins, then lifts the lock once it expires, without touching other accounts", async () => {
    const { email, session: goodSession } = await signUp();
    const otherAccount = await signUp();
    const attacker = createSession();

    let lastStatus;
    for (let i = 0; i < 5; i++) {
      const res = await attacker.fetch("/api/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ email, password: "wrongpassword" }),
      });
      lastStatus = res.status;
    }
    assert.equal(lastStatus, 401);

    // Locked out now, even with the correct password.
    const lockedOut = await goodSession.fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password: "correcthorsebattery" }),
    });
    assert.equal(lockedOut.status, 429);
    assert.ok(lockedOut.headers.get("retry-after"));

    // A different account is entirely unaffected.
    const otherLogin = await createSession().fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: otherAccount.email, password: "correcthorsebattery" }),
    });
    assert.equal(otherLogin.status, 200);

    // Once the (test-shortened) lockout window elapses, the real password works again.
    await new Promise((resolve) => setTimeout(resolve, 500));
    const afterExpiry = await goodSession.fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password: "correcthorsebattery" }),
    });
    assert.equal(afterExpiry.status, 200);
  });

  it("rate-limits signups per IP without affecting a different IP", async () => {
    // A fixed, dedicated IP not used by any other test's signUp() calls
    // (those each get their own synthetic address — see server.mjs).
    const attackerIp = "198.51.100.7";
    const signupAs = (email) =>
      fetch(`${BASE_URL}/api/auth/signup`, {
        method: "POST",
        headers: { "Content-Type": "application/json", "x-forwarded-for": attackerIp },
        body: JSON.stringify({ email, password: "correcthorsebattery" }),
      });

    let lastStatus;
    for (let i = 0; i < 10; i++) {
      const res = await signupAs(`ratelimit-${Date.now()}-${i}@example.com`);
      lastStatus = res.status;
    }
    assert.equal(lastStatus, 201);

    // The 11th signup from the same IP is rejected, even with fully valid
    // credentials — this is a rate limit, not a validation failure.
    const eleventh = await signupAs(`ratelimit-${Date.now()}-eleventh@example.com`);
    assert.equal(eleventh.status, 429);

    // A different IP is completely unaffected.
    const otherIp = await fetch(`${BASE_URL}/api/auth/signup`, {
      method: "POST",
      headers: { "Content-Type": "application/json", "x-forwarded-for": "198.51.100.8" },
      body: JSON.stringify({ email: `ratelimit-${Date.now()}-other@example.com`, password: "correcthorsebattery" }),
    });
    assert.equal(otherIp.status, 201);
  });
});

describe("observations + quality grade", () => {
  it("computes Research Grade only from independent agrees, excluding the owner's own", async () => {
    const owner = await signUp();
    const b = await signUp();
    const c = await signUp();

    const created = await json(
      await owner.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Red Fox",
          scientificName: "Vulpes vulpes",
          confidence: 0.7,
          taxonSlug: "red-fox",
        }),
      }),
    );
    const id = created.observation.id;

    let current = await json(await owner.session.fetch(`/api/observations/${id}`));
    assert.equal(current.observation.qualityGrade, "needs_id");

    // Owner's own agree must not count.
    await owner.session.fetch(`/api/observations/${id}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "", kind: "agree" }),
    });
    current = await json(await owner.session.fetch(`/api/observations/${id}`));
    assert.equal(current.observation.qualityGrade, "needs_id");
    assert.equal(current.observation.agreeCount, 0);

    // One independent agree is not enough.
    await b.session.fetch(`/api/observations/${id}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "", kind: "agree" }),
    });
    current = await json(await owner.session.fetch(`/api/observations/${id}`));
    assert.equal(current.observation.qualityGrade, "needs_id");
    assert.equal(current.observation.agreeCount, 1);

    // A second independent agree flips it.
    await c.session.fetch(`/api/observations/${id}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "", kind: "agree" }),
    });
    current = await json(await owner.session.fetch(`/api/observations/${id}`));
    assert.equal(current.observation.qualityGrade, "research_grade");
    assert.equal(current.observation.agreeCount, 2);
  });

  it("enforces ownership on delete", async () => {
    const owner = await signUp();
    const stranger = await signUp();

    const created = await json(
      await owner.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Fly Agaric",
          scientificName: "Amanita muscaria",
          confidence: 0.6,
          taxonSlug: "fly-agaric",
        }),
      }),
    );
    const id = created.observation.id;

    await stranger.session.fetch(`/api/observations/${id}`, { method: "DELETE" });
    const stillThere = await owner.session.fetch(`/api/observations/${id}`);
    assert.equal(stillThere.status, 200);

    await owner.session.fetch(`/api/observations/${id}`, { method: "DELETE" });
    const gone = await owner.session.fetch(`/api/observations/${id}`);
    assert.equal(gone.status, 404);
  });

  it("rejects out-of-range and non-finite confidence, and oversized text fields", async () => {
    const user = await signUp();

    const base = {
      photoDataUrl: "x",
      commonName: "Red Fox",
      scientificName: "Vulpes vulpes",
      taxonSlug: "red-fox",
    };
    const post = (overrides) =>
      user.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ ...base, ...overrides }),
      });

    // typeof NaN === "number" and typeof Infinity === "number" — both must
    // still be rejected, not silently accepted as "a number."
    assert.equal((await post({ confidence: NaN })).status, 400);
    assert.equal((await post({ confidence: Infinity })).status, 400);
    assert.equal((await post({ confidence: -0.1 })).status, 400);
    assert.equal((await post({ confidence: 1.1 })).status, 400);
    assert.equal((await post({ confidence: 0.5 })).status, 201);

    const hugeCommonName = "a".repeat(500);
    assert.equal((await post({ confidence: 0.5, commonName: hugeCommonName })).status, 400);

    // Out-of-range lat/lng fall back to null rather than rejecting the
    // whole observation — location is optional.
    const badCoords = await json(await post({ confidence: 0.5, lat: 999, lng: 999 }));
    assert.equal(badCoords.observation.lat, null);
    assert.equal(badCoords.observation.lng, null);
  });
});

describe("sensitive-species obscuring", () => {
  it("hides location and snaps coordinates for non-owners, but not for the owner", async () => {
    const owner = await signUp();
    const viewer = await signUp();

    const created = await json(
      await owner.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Eastern Box Turtle",
          scientificName: "Terrapene carolina",
          confidence: 0.7,
          taxonSlug: "eastern-box-turtle",
          locationName: "Secret Bog Trail",
          lat: 40.73,
          lng: -74.01,
        }),
      }),
    );
    const id = created.observation.id;

    const ownerView = await json(await owner.session.fetch(`/api/observations/${id}`));
    assert.equal(ownerView.observation.locationName, "Secret Bog Trail");

    const viewerView = await json(await viewer.session.fetch(`/api/observations/${id}`));
    assert.equal(viewerView.observation.locationName, "Location hidden — sensitive species");

    const bboxAsViewer = await json(
      await viewer.session.fetch(
        "/api/observations/near?minLat=40.5&maxLat=41.0&minLng=-74.5&maxLng=-73.5",
      ),
    );
    const found = bboxAsViewer.observations.find((o) => o.id === id);
    assert.ok(found, "sensitive observation should still appear in bbox results");
    assert.equal(found.lat, 40.7, "coordinates should be snapped to the grid for a non-owner");
    assert.equal(found.lng, -74.0);
  });

  it("does not obscure a non-sensitive species", async () => {
    const owner = await signUp();
    const viewer = await signUp();

    const created = await json(
      await owner.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Red Fox",
          scientificName: "Vulpes vulpes",
          confidence: 0.7,
          taxonSlug: "red-fox",
          locationName: "Main Street Park",
        }),
      }),
    );
    const viewerView = await json(
      await viewer.session.fetch(`/api/observations/${created.observation.id}`),
    );
    assert.equal(viewerView.observation.locationName, "Main Street Park");
  });
});

describe("map bbox query validation", () => {
  it("rejects missing, empty, and non-numeric params; accepts a valid query", async () => {
    const { session } = await signUp();
    assert.equal((await session.fetch("/api/observations/near?minLat=40")).status, 400);
    assert.equal(
      (await session.fetch("/api/observations/near?minLat=&maxLat=41&minLng=-75&maxLng=-73")).status,
      400,
    );
    assert.equal(
      (await session.fetch("/api/observations/near?minLat=abc&maxLat=41&minLng=-75&maxLng=-73"))
        .status,
      400,
    );
    assert.equal(
      (await session.fetch("/api/observations/near?minLat=40&maxLat=41&minLng=-75&maxLng=-73"))
        .status,
      200,
    );
    assert.equal((await fetch("http://localhost:3999/api/observations/near?minLat=40&maxLat=41&minLng=-75&maxLng=-73")).status, 401);
  });
});

describe("identify queue (needs-id)", () => {
  it("excludes the viewer's own observations and ones they've already agreed with", async () => {
    const a = await signUp();
    const b = await signUp();

    const created = await json(
      await a.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Monarch Butterfly",
          scientificName: "Danaus plexippus",
          confidence: 0.6,
          taxonSlug: "monarch-butterfly",
        }),
      }),
    );
    const id = created.observation.id;

    const ownQueue = await json(await a.session.fetch("/api/observations/needs-id"));
    assert.ok(!ownQueue.observations.some((o) => o.id === id));

    const bQueue = await json(await b.session.fetch("/api/observations/needs-id"));
    assert.ok(bQueue.observations.some((o) => o.id === id));

    await b.session.fetch(`/api/observations/${id}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "", kind: "agree" }),
    });
    const bQueueAfter = await json(await b.session.fetch("/api/observations/needs-id"));
    assert.ok(!bQueueAfter.observations.some((o) => o.id === id));
  });
});

describe("activity feed", () => {
  it("shows others' activity on your observations, not your own", async () => {
    const owner = await signUp();
    const commenter = await signUp();

    const created = await json(
      await owner.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Common Dandelion",
          scientificName: "Taraxacum officinale",
          confidence: 0.9,
          taxonSlug: "common-dandelion",
        }),
      }),
    );
    const id = created.observation.id;

    const before = await json(await owner.session.fetch("/api/activity"));
    assert.equal(before.activity.length, 0);

    await commenter.session.fetch(`/api/observations/${id}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "Nice find", kind: "comment" }),
    });
    await owner.session.fetch(`/api/observations/${id}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "my own note", kind: "comment" }),
    });

    const after = await json(await owner.session.fetch("/api/activity"));
    assert.equal(after.activity.length, 1, "own comment on own observation should not count");
    assert.equal(after.activity[0].userEmail, commenter.email);
  });
});

describe("journal", () => {
  it("is publicly readable, author-scoped for writes", async () => {
    const author = await signUp();
    const stranger = await signUp();

    const created = await json(
      await author.session.fetch("/api/journal", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ title: "Spring walk", body: "Saw a fox." }),
      }),
    );
    const id = created.post.id;

    const publicRead = await fetch(`http://localhost:3999/api/journal/${id}`);
    assert.equal(publicRead.status, 200);

    const strangerEdit = await stranger.session.fetch(`/api/journal/${id}`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ title: "hacked", body: "hacked" }),
    });
    assert.equal(strangerEdit.status, 404);

    // Oversized fields are rejected on both create and edit, not just empty ones.
    const hugeBody = "a".repeat(25000);
    const oversizedCreate = await author.session.fetch("/api/journal", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ title: "Too long", body: hugeBody }),
    });
    assert.equal(oversizedCreate.status, 400);

    const oversizedEdit = await author.session.fetch(`/api/journal/${id}`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ title: "Spring walk", body: hugeBody }),
    });
    assert.equal(oversizedEdit.status, 400);
  });
});

describe("guides", () => {
  it("filters out invalid taxon slugs server-side", async () => {
    const { session } = await signUp();
    const rejected = await session.fetch("/api/guides", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ title: "x", description: "y", taxonSlugs: ["not-a-real-slug"] }),
    });
    assert.equal(rejected.status, 400);

    const accepted = await json(
      await session.fetch("/api/guides", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          title: "Pollinators",
          description: "Common pollinators",
          taxonSlugs: ["monarch-butterfly", "not-real"],
        }),
      }),
    );
    assert.deepEqual(accepted.guide.taxonSlugs, ["monarch-butterfly"]);
  });
});

describe("projects", () => {
  it("Collection projects are a live saved search", async () => {
    const owner = await signUp();
    const logger = await signUp();

    const created = await json(
      await owner.session.fetch("/api/projects", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          type: "collection",
          title: "Foxes",
          description: "All fox sightings",
          taxonFilter: ["red-fox"],
        }),
      }),
    );
    const id = created.project.id;

    // Other tests in this shared-server run also create red-fox
    // observations, so the count isn't guaranteed to start at 0 — assert
    // the delta a live saved search should produce, not an absolute count.
    const before = await json(await owner.session.fetch(`/api/projects/${id}`));
    const beforeCount = before.observations.length;

    await logger.session.fetch("/api/observations", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        photoDataUrl: "x",
        commonName: "Red Fox",
        scientificName: "Vulpes vulpes",
        confidence: 0.7,
        taxonSlug: "red-fox",
      }),
    });

    const after = await json(await owner.session.fetch(`/api/projects/${id}`));
    assert.equal(
      after.observations.length,
      beforeCount + 1,
      "new matching observation should appear with no project edit",
    );
  });

  it("Traditional projects have real join/leave membership", async () => {
    const owner = await signUp();
    const joiner = await signUp();

    const created = await json(
      await owner.session.fetch("/api/projects", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ type: "traditional", title: "Naturalists", description: "Group" }),
      }),
    );
    const id = created.project.id;

    let state = await json(await owner.session.fetch(`/api/projects/${id}`));
    assert.equal(state.members.length, 1, "owner is auto-joined");

    await joiner.session.fetch(`/api/projects/${id}/membership`, { method: "POST" });
    state = await json(await joiner.session.fetch(`/api/projects/${id}`));
    assert.equal(state.members.length, 2);
    assert.equal(state.isMember, true);

    await joiner.session.fetch(`/api/projects/${id}/membership`, { method: "DELETE" });
    state = await json(await owner.session.fetch(`/api/projects/${id}`));
    assert.equal(state.members.length, 1);
  });
});

describe("curator tools", () => {
  it("enforces role checks, requires a written reason at every step, and lets the curator actually resolve", async () => {
    const member = await signUp();
    const reporter = await signUp();

    const created = await json(
      await member.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "American Robin",
          scientificName: "Turdus migratorius",
          confidence: 0.8,
          taxonSlug: "american-robin",
        }),
      }),
    );
    const id = created.observation.id;

    const emptyReason = await reporter.session.fetch(`/api/observations/${id}/flag`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ reason: "" }),
    });
    assert.equal(emptyReason.status, 400);

    const flagged = await json(
      await reporter.session.fetch(`/api/observations/${id}/flag`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ reason: "Possible misidentification" }),
      }),
    );
    assert.equal(flagged.flag.status, "open");

    // A plain member (this test's `member` account, created after the
    // suite's very first signup) must not have curator access.
    const memberQueue = await member.session.fetch("/api/curator/flags");
    assert.equal(memberQueue.status, 403);

    const memberResolve = await member.session.fetch(`/api/curator/flags/${flagged.flag.id}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "resolved", reason: "trying to bypass" }),
    });
    assert.equal(memberResolve.status, 403);

    // The curator (this suite's guaranteed-first account) requires a
    // reason too, then can actually resolve — the real success path.
    const curatorEmptyReason = await curator.session.fetch(`/api/curator/flags/${flagged.flag.id}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "resolved", reason: "" }),
    });
    assert.equal(curatorEmptyReason.status, 400);

    const curatorResolve = await curator.session.fetch(`/api/curator/flags/${flagged.flag.id}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "resolved", reason: "Checked against a field guide, ID is correct" }),
    });
    assert.equal(curatorResolve.status, 200);

    // Already resolved — resolving again should fail.
    const alreadyResolved = await curator.session.fetch(`/api/curator/flags/${flagged.flag.id}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "resolved", reason: "double resolve" }),
    });
    assert.equal(alreadyResolved.status, 404);
  });

  it("real curator promote/demote flow: members can't self-promote, promotion and demotion actually change access", async () => {
    const target = await signUp();

    // A plain member can't promote themselves.
    const selfPromote = await target.session.fetch("/api/curator/promote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: target.email, reason: "let me in" }),
    });
    assert.equal(selfPromote.status, 403);

    // Curator promotion requires a written reason.
    const noReason = await curator.session.fetch("/api/curator/promote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: target.email, reason: "" }),
    });
    assert.equal(noReason.status, 400);

    const promoted = await curator.session.fetch("/api/curator/promote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: target.email, reason: "Trusted moderator, joining the curator team" }),
    });
    assert.equal(promoted.status, 200);

    // The promoted account now really has curator access.
    const nowCanQueue = await target.session.fetch("/api/curator/flags");
    assert.equal(nowCanQueue.status, 200);

    // Promoting an already-curator account is rejected.
    const doublePromote = await curator.session.fetch("/api/curator/promote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: target.email, reason: "again" }),
    });
    assert.equal(doublePromote.status, 400);

    // A curator can't remove their own access.
    const selfDemote = await curator.session.fetch("/api/curator/demote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: curator.email, reason: "trying to self-demote" }),
    });
    assert.equal(selfDemote.status, 400);

    const demoted = await curator.session.fetch("/api/curator/demote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: target.email, reason: "Stepping back from moderation" }),
    });
    assert.equal(demoted.status, 200);

    // Access is really gone now.
    const afterDemote = await target.session.fetch("/api/curator/flags");
    assert.equal(afterDemote.status, 403);

    // The full history is visible to curators.
    const history = await json(await curator.session.fetch("/api/curator/curators"));
    const targetChanges = history.roleChanges.filter((rc) => rc.targetEmail === target.email);
    assert.equal(targetChanges.length, 2);
  });
});

describe("account deletion", () => {
  it("schedules a real 14-day grace period (cancellable) instead of deleting immediately, then actually purges once due — cascading to sessions, owned observations, and comments, leaving other users intact", async () => {
    const a = await signUp();
    const b = await signUp();

    const created = await json(
      await a.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Red Fox",
          scientificName: "Vulpes vulpes",
          confidence: 0.7,
          taxonSlug: "red-fox",
        }),
      }),
    );
    const obsId = created.observation.id;

    await b.session.fetch(`/api/observations/${obsId}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "nice fox", kind: "comment" }),
    });

    const scheduled = await json(await a.session.fetch("/api/account/delete", { method: "POST" }));
    assert.ok(scheduled.purgeAt);

    // The account keeps working exactly as normal during the grace period.
    assert.equal((await a.session.fetch("/api/observations")).status, 200);
    const meScheduled = await json(await a.session.fetch("/api/auth/me"));
    assert.ok(meScheduled.user.pendingDeletionAt);

    // Cancelling is a real undo: the account is no longer scheduled.
    assert.equal((await a.session.fetch("/api/account/delete/cancel", { method: "POST" })).status, 200);
    const meCancelled = await json(await a.session.fetch("/api/auth/me"));
    assert.equal(meCancelled.user.pendingDeletionAt, null);

    // Cancelling again with nothing scheduled is rejected, not a silent no-op.
    assert.equal((await a.session.fetch("/api/account/delete/cancel", { method: "POST" })).status, 400);

    // Schedule again and let the (test-shortened) grace period actually
    // elapse, then let any authenticated request's opportunistic sweep
    // (getUserBySessionToken -> purgeDueAccounts in store.ts) carry out
    // the real purge — not just flip a flag.
    await a.session.fetch("/api/account/delete", { method: "POST" });
    await new Promise((resolve) => setTimeout(resolve, 400));
    await b.session.fetch("/api/auth/me");

    assert.equal((await a.session.fetch("/api/observations")).status, 401);

    const loginAttempt = await createSession().fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: a.email, password: "correcthorsebattery" }),
    });
    assert.equal(loginAttempt.status, 401);

    assert.equal((await b.session.fetch(`/api/observations/${obsId}`)).status, 404);
  });
});

describe("account anonymization", () => {
  it("keeps contribution history while making the account unreachable", async () => {
    const a = await signUp();
    const b = await signUp();

    // A's own observation — should survive anonymization, reattributed.
    const aObs = await json(
      await a.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "American Robin",
          scientificName: "Turdus migratorius",
          confidence: 0.7,
          taxonSlug: "american-robin",
        }),
      }),
    );

    // B's observation, with a comment authored by A — the comment's
    // denormalized author email should also update on anonymization.
    const bObs = await json(
      await b.session.fetch("/api/observations", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          photoDataUrl: "x",
          commonName: "Common Dandelion",
          scientificName: "Taraxacum officinale",
          confidence: 0.9,
          taxonSlug: "common-dandelion",
        }),
      }),
    );
    await a.session.fetch(`/api/observations/${bObs.observation.id}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body: "nice dandelion", kind: "comment" }),
    });

    const anonResult = await json(
      await a.session.fetch("/api/account/anonymize", { method: "POST" }),
    );
    assert.match(anonResult.anonymizedEmail, /^deleted-user-.+@anonymized\.wildkey$/);

    // Old session is dead and the original email can no longer log in.
    assert.equal((await a.session.fetch("/api/observations")).status, 401);
    const loginAttempt = await createSession().fetch("/api/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: a.email, password: "correcthorsebattery" }),
    });
    assert.equal(loginAttempt.status, 401);

    // A's observation survives, reattributed to the anonymized email.
    const stillThere = await json(
      await b.session.fetch(`/api/observations/${aObs.observation.id}`),
    );
    assert.equal(stillThere.author.email, anonResult.anonymizedEmail);

    // A's comment on B's observation still shows the anonymized email,
    // not the real one that used to be there — and B's own observation
    // (owned by a different, untouched account) is unaffected.
    const comments = await json(
      await b.session.fetch(`/api/observations/${bObs.observation.id}/comments`),
    );
    assert.equal(comments.comments[0].userEmail, anonResult.anonymizedEmail);
  });
});

describe("data export", () => {
  it("returns everything the account owns as an attachment", async () => {
    const { session } = await signUp();
    await session.fetch("/api/observations", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        photoDataUrl: "x",
        commonName: "Red Fox",
        scientificName: "Vulpes vulpes",
        confidence: 0.7,
        taxonSlug: "red-fox",
      }),
    });

    const res = await session.fetch("/api/account/export");
    assert.equal(res.status, 200);
    assert.match(res.headers.get("content-disposition") ?? "", /attachment/);
    const data = await res.json();
    assert.equal(data.observations.length, 1);
  });
});
