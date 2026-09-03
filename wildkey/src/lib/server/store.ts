import { randomBytes, randomUUID, scryptSync, timingSafeEqual } from "node:crypto";
import { mkdirSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { getMockSpecies } from "@/lib/mock-species";

/**
 * A minimal, file-backed persistence layer standing in for a real database
 * during local development. It gives Naturalist Mode a genuine server-side
 * account + observation store instead of localStorage, so the "one app,
 * two modes" split (Part A.3) actually works end to end. Swap this module
 * for a real database (e.g. Postgres) before deploying anywhere with more
 * than one server process — writes here are not safe under concurrent
 * requests across multiple instances.
 */

type User = {
  id: string;
  email: string;
  passwordHash: string;
  passwordSalt: string;
  createdAt: string;
};

type Session = {
  token: string;
  userId: string;
  createdAt: string;
};

export type QualityGrade = "needs_id" | "research_grade";

export type ServerObservation = {
  id: string;
  userId: string;
  createdAt: string;
  photoDataUrl: string;
  commonName: string;
  scientificName: string;
  confidence: number;
  taxonSlug: string;
  syncState: "queued" | "uploading" | "confirmed" | "failed";
  /** Data Quality flag (Part C.2): wild vs. captive/cultivated. */
  isWild: boolean;
  /** Free-text place name — no map/geocoding yet, see Part G. */
  locationName: string;
  notes: string;
};

export type ObservationWithGrade = ServerObservation & {
  agreeCount: number;
  qualityGrade: QualityGrade;
};

/**
 * Research-Grade equivalent from Part C.2: 2+ agreeing community IDs.
 * The observer's own confidence isn't counted as an agreement — quality
 * grade reflects independent community consensus, not self-attestation.
 */
const RESEARCH_GRADE_AGREE_THRESHOLD = 2;

export type ObservationComment = {
  id: string;
  observationId: string;
  userId: string;
  userEmail: string;
  body: string;
  kind: "comment" | "agree";
  createdAt: string;
};

type DbShape = {
  users: User[];
  sessions: Session[];
  observations: ServerObservation[];
  comments: ObservationComment[];
};

const DB_PATH = path.join(process.cwd(), ".data", "db.json");

function emptyDb(): DbShape {
  return { users: [], sessions: [], observations: [], comments: [] };
}

function readDb(): DbShape {
  if (!existsSync(DB_PATH)) return emptyDb();
  try {
    const parsed = JSON.parse(readFileSync(DB_PATH, "utf-8")) as Partial<DbShape>;
    return { ...emptyDb(), ...parsed };
  } catch {
    return emptyDb();
  }
}

function writeDb(db: DbShape) {
  mkdirSync(path.dirname(DB_PATH), { recursive: true });
  writeFileSync(DB_PATH, JSON.stringify(db, null, 2));
}

function hashPassword(password: string, salt: string): string {
  return scryptSync(password, salt, 64).toString("hex");
}

export function createUser(email: string, password: string): User {
  const db = readDb();
  const normalizedEmail = email.trim().toLowerCase();
  if (db.users.some((u) => u.email === normalizedEmail)) {
    throw new Error("An account with this email already exists.");
  }
  const passwordSalt = randomBytes(16).toString("hex");
  const user: User = {
    id: randomUUID(),
    email: normalizedEmail,
    passwordHash: hashPassword(password, passwordSalt),
    passwordSalt,
    createdAt: new Date().toISOString(),
  };
  db.users.push(user);
  writeDb(db);
  return user;
}

export function verifyCredentials(email: string, password: string): User | null {
  const db = readDb();
  const user = db.users.find((u) => u.email === email.trim().toLowerCase());
  if (!user) return null;
  const candidateHash = hashPassword(password, user.passwordSalt);
  const a = Buffer.from(candidateHash, "hex");
  const b = Buffer.from(user.passwordHash, "hex");
  if (a.length !== b.length || !timingSafeEqual(a, b)) return null;
  return user;
}

export function createSession(userId: string): string {
  const db = readDb();
  const token = randomBytes(32).toString("hex");
  db.sessions.push({ token, userId, createdAt: new Date().toISOString() });
  writeDb(db);
  return token;
}

export function destroySession(token: string) {
  const db = readDb();
  db.sessions = db.sessions.filter((s) => s.token !== token);
  writeDb(db);
}

export function getUserBySessionToken(token: string | undefined): User | null {
  if (!token) return null;
  const db = readDb();
  const session = db.sessions.find((s) => s.token === token);
  if (!session) return null;
  return db.users.find((u) => u.id === session.userId) ?? null;
}

export function toPublicUser(user: User) {
  return { id: user.id, email: user.email, createdAt: user.createdAt };
}

export function getUserById(id: string): User | null {
  return readDb().users.find((u) => u.id === id) ?? null;
}

function withQualityGrade(observation: ServerObservation, db: DbShape): ObservationWithGrade {
  // Independent confirmations only — the observer's own agree on their own
  // observation doesn't count toward Research Grade.
  const agreeCount = db.comments.filter(
    (c) => c.observationId === observation.id && c.kind === "agree" && c.userId !== observation.userId,
  ).length;
  return {
    ...observation,
    agreeCount,
    qualityGrade: agreeCount >= RESEARCH_GRADE_AGREE_THRESHOLD ? "research_grade" : "needs_id",
  };
}

/**
 * Part C.2 sensitive-species obscuring, enforced server-side (not just
 * hidden in the UI) so it can't be bypassed by reading the API directly.
 * Automatic — no per-observation opt-in — and it never obscures for the
 * observer viewing their own observation.
 */
function obscureLocationIfSensitive<T extends { userId: string; taxonSlug: string; locationName: string }>(
  observation: T,
  viewerId: string | null,
): T {
  if (observation.userId === viewerId) return observation;
  if (!observation.locationName) return observation;
  const species = getMockSpecies(observation.taxonSlug);
  if (!species?.sensitive) return observation;
  return { ...observation, locationName: "Location hidden — sensitive species" };
}

export function listObservationsForUser(userId: string): ObservationWithGrade[] {
  const db = readDb();
  return db.observations
    .filter((o) => o.userId === userId)
    .sort((a, b) => b.createdAt.localeCompare(a.createdAt))
    .map((o) => withQualityGrade(o, db));
}

/**
 * The real Identify queue feed (Part C.2): every observation not needing
 * only the requesting user's own confirmation, still short of Research
 * Grade. Powers /identify once real community observations exist, instead
 * of the mock queue Quick ID users still see.
 */
export function listObservationsNeedingId(
  excludeUserId: string,
): (ObservationWithGrade & { observerEmail: string })[] {
  const db = readDb();
  const alreadyAgreedIds = new Set(
    db.comments
      .filter((c) => c.kind === "agree" && c.userId === excludeUserId)
      .map((c) => c.observationId),
  );
  const usersById = new Map(db.users.map((u) => [u.id, u]));
  return db.observations
    .filter((o) => o.userId !== excludeUserId && !alreadyAgreedIds.has(o.id))
    .map((o) => withQualityGrade(o, db))
    .filter((o) => o.qualityGrade === "needs_id")
    .sort((a, b) => a.createdAt.localeCompare(b.createdAt))
    .map((o) => obscureLocationIfSensitive(o, excludeUserId))
    .map((o) => ({ ...o, observerEmail: usersById.get(o.userId)?.email ?? "unknown" }));
}

export function createObservationForUser(
  userId: string,
  input: Omit<ServerObservation, "id" | "userId" | "createdAt" | "syncState">,
): ServerObservation {
  const db = readDb();
  const observation: ServerObservation = {
    ...input,
    id: randomUUID(),
    userId,
    createdAt: new Date().toISOString(),
    syncState: "confirmed",
  };
  db.observations.push(observation);
  writeDb(db);
  return observation;
}

/**
 * Permanent account deletion. This is full erasure, not the anonymization
 * option from Part D.1 (keep contribution history, drop identity) — that's
 * a separate, still-unbuilt feature. Deleting removes the account, its
 * sessions, every observation it owns (and comments on those, since they'd
 * be orphaned otherwise), and every comment it authored elsewhere.
 */
export function deleteUserAccount(userId: string) {
  const db = readDb();
  const ownedObservationIds = new Set(
    db.observations.filter((o) => o.userId === userId).map((o) => o.id),
  );
  db.users = db.users.filter((u) => u.id !== userId);
  db.sessions = db.sessions.filter((s) => s.userId !== userId);
  db.observations = db.observations.filter((o) => o.userId !== userId);
  db.comments = db.comments.filter(
    (c) => c.userId !== userId && !ownedObservationIds.has(c.observationId),
  );
  writeDb(db);
}

export function deleteObservationForUser(userId: string, id: string) {
  const db = readDb();
  db.observations = db.observations.filter((o) => !(o.id === id && o.userId === userId));
  db.comments = db.comments.filter((c) => c.observationId !== id);
  writeDb(db);
}

export type ActivityItem = ObservationComment & {
  observationCommonName: string;
  observationTaxonSlug: string;
};

/**
 * Notifications for the /activity screen: other people's comments and
 * agrees on observations this user owns. Excludes the user's own
 * comments on their own observations — that's not activity to notify
 * them about.
 */
export function listActivityForUser(userId: string): ActivityItem[] {
  const db = readDb();
  const ownedObservations = new Map(
    db.observations.filter((o) => o.userId === userId).map((o) => [o.id, o]),
  );
  return db.comments
    .filter((c) => ownedObservations.has(c.observationId) && c.userId !== userId)
    .sort((a, b) => b.createdAt.localeCompare(a.createdAt))
    .map((c) => {
      const observation = ownedObservations.get(c.observationId)!;
      return {
        ...c,
        observationCommonName: observation.commonName,
        observationTaxonSlug: observation.taxonSlug,
      };
    });
}

export function getObservationById(id: string, viewerId: string | null = null): ObservationWithGrade | null {
  const db = readDb();
  const observation = db.observations.find((o) => o.id === id);
  if (!observation) return null;
  return obscureLocationIfSensitive(withQualityGrade(observation, db), viewerId);
}

export function listCommentsByUser(userId: string): ObservationComment[] {
  return readDb()
    .comments.filter((c) => c.userId === userId)
    .sort((a, b) => a.createdAt.localeCompare(b.createdAt));
}

export function listCommentsForObservation(observationId: string): ObservationComment[] {
  return readDb()
    .comments.filter((c) => c.observationId === observationId)
    .sort((a, b) => a.createdAt.localeCompare(b.createdAt));
}

export function addCommentToObservation(
  observationId: string,
  user: Pick<User, "id" | "email">,
  body: string,
  kind: ObservationComment["kind"],
): ObservationComment | null {
  const db = readDb();
  if (!db.observations.some((o) => o.id === observationId)) return null;

  const comment: ObservationComment = {
    id: randomUUID(),
    observationId,
    userId: user.id,
    userEmail: user.email,
    body,
    kind,
    createdAt: new Date().toISOString(),
  };
  db.comments.push(comment);
  writeDb(db);
  return comment;
}
