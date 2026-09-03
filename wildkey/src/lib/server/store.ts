import { randomBytes, randomUUID, scryptSync, timingSafeEqual } from "node:crypto";
import { mkdirSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";

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

export function listObservationsForUser(userId: string): ObservationWithGrade[] {
  const db = readDb();
  return db.observations
    .filter((o) => o.userId === userId)
    .sort((a, b) => b.createdAt.localeCompare(a.createdAt))
    .map((o) => withQualityGrade(o, db));
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

export function deleteObservationForUser(userId: string, id: string) {
  const db = readDb();
  db.observations = db.observations.filter((o) => !(o.id === id && o.userId === userId));
  db.comments = db.comments.filter((c) => c.observationId !== id);
  writeDb(db);
}

export function getObservationById(id: string): ObservationWithGrade | null {
  const db = readDb();
  const observation = db.observations.find((o) => o.id === id);
  return observation ? withQualityGrade(observation, db) : null;
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
