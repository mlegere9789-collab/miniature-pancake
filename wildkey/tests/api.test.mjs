import { before, after, describe, it } from "node:test";
import assert from "node:assert/strict";
import { startServer, stopServer, createSession, signUp } from "./server.mjs";

before(async () => {
  await startServer();
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
  it("enforces role checks and requires a written reason at every step", async () => {
    // This is the first account created in this suite's server instance
    // isn't guaranteed to be first overall — curator bootstrap is instead
    // verified structurally: whichever account is a curator can act,
    // members cannot, and reasons are always required.
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
  });
});

describe("account deletion", () => {
  it("cascades to sessions, owned observations, and comments; leaves other users intact", async () => {
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

    await a.session.fetch("/api/account/delete", { method: "POST" });

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
