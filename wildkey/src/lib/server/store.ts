import { randomBytes, randomUUID, scryptSync, timingSafeEqual } from "node:crypto";
import { getMockSpecies } from "@/lib/mock-species";
import { db } from "@/lib/server/db";

/**
 * Real SQLite-backed persistence (see src/lib/server/db.ts for the schema
 * and the honest boundary on what "production database" means here). This
 * module is the only place that knows SQL — everything above it (API
 * routes, pages) calls these functions exactly as it did against the old
 * JSON-file store, so the swap required zero changes outside this file
 * and db.ts.
 */

type User = {
  id: string;
  email: string;
  passwordHash: string;
  passwordSalt: string;
  createdAt: string;
};

type UserRow = {
  id: string;
  email: string;
  password_hash: string;
  password_salt: string;
  created_at: string;
};

function userFromRow(row: UserRow): User {
  return {
    id: row.id,
    email: row.email,
    passwordHash: row.password_hash,
    passwordSalt: row.password_salt,
    createdAt: row.created_at,
  };
}

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

type ObservationRow = {
  id: string;
  user_id: string;
  created_at: string;
  photo_data_url: string;
  common_name: string;
  scientific_name: string;
  confidence: number;
  taxon_slug: string;
  sync_state: string;
  is_wild: number;
  location_name: string;
  notes: string;
};

function observationFromRow(row: ObservationRow): ServerObservation {
  return {
    id: row.id,
    userId: row.user_id,
    createdAt: row.created_at,
    photoDataUrl: row.photo_data_url,
    commonName: row.common_name,
    scientificName: row.scientific_name,
    confidence: row.confidence,
    taxonSlug: row.taxon_slug,
    syncState: row.sync_state as ServerObservation["syncState"],
    isWild: Boolean(row.is_wild),
    locationName: row.location_name,
    notes: row.notes,
  };
}

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

type CommentRow = {
  id: string;
  observation_id: string;
  user_id: string;
  user_email: string;
  body: string;
  kind: string;
  created_at: string;
};

function commentFromRow(row: CommentRow): ObservationComment {
  return {
    id: row.id,
    observationId: row.observation_id,
    userId: row.user_id,
    userEmail: row.user_email,
    body: row.body,
    kind: row.kind as ObservationComment["kind"],
    createdAt: row.created_at,
  };
}

function hashPassword(password: string, salt: string): string {
  return scryptSync(password, salt, 64).toString("hex");
}

export function createUser(email: string, password: string): User {
  const normalizedEmail = email.trim().toLowerCase();
  const passwordSalt = randomBytes(16).toString("hex");
  const user: User = {
    id: randomUUID(),
    email: normalizedEmail,
    passwordHash: hashPassword(password, passwordSalt),
    passwordSalt,
    createdAt: new Date().toISOString(),
  };
  try {
    db.prepare(
      `INSERT INTO users (id, email, password_hash, password_salt, created_at)
       VALUES (@id, @email, @passwordHash, @passwordSalt, @createdAt)`,
    ).run({
      id: user.id,
      email: user.email,
      passwordHash: user.passwordHash,
      passwordSalt: user.passwordSalt,
      createdAt: user.createdAt,
    });
  } catch (err) {
    if (err instanceof Error && err.message.includes("UNIQUE constraint failed")) {
      throw new Error("An account with this email already exists.");
    }
    throw err;
  }
  return user;
}

export function verifyCredentials(email: string, password: string): User | null {
  const row = db
    .prepare<[string], UserRow>("SELECT * FROM users WHERE email = ?")
    .get(email.trim().toLowerCase());
  if (!row) return null;
  const user = userFromRow(row);
  const candidateHash = hashPassword(password, user.passwordSalt);
  const a = Buffer.from(candidateHash, "hex");
  const b = Buffer.from(user.passwordHash, "hex");
  if (a.length !== b.length || !timingSafeEqual(a, b)) return null;
  return user;
}

export function createSession(userId: string): string {
  const token = randomBytes(32).toString("hex");
  db.prepare("INSERT INTO sessions (token, user_id, created_at) VALUES (?, ?, ?)").run(
    token,
    userId,
    new Date().toISOString(),
  );
  return token;
}

export function destroySession(token: string) {
  db.prepare("DELETE FROM sessions WHERE token = ?").run(token);
}

export function getUserBySessionToken(token: string | undefined): User | null {
  if (!token) return null;
  const row = db
    .prepare<[string], UserRow>(
      `SELECT users.* FROM users
       JOIN sessions ON sessions.user_id = users.id
       WHERE sessions.token = ?`,
    )
    .get(token);
  return row ? userFromRow(row) : null;
}

export function toPublicUser(user: User) {
  return { id: user.id, email: user.email, createdAt: user.createdAt };
}

export function getUserById(id: string): User | null {
  const row = db.prepare<[string], UserRow>("SELECT * FROM users WHERE id = ?").get(id);
  return row ? userFromRow(row) : null;
}

const agreeCountStmt = db.prepare<[string, string], { count: number }>(
  `SELECT COUNT(*) as count FROM comments
   WHERE observation_id = ? AND kind = 'agree' AND user_id != ?`,
);

function withQualityGrade(observation: ServerObservation): ObservationWithGrade {
  // Independent confirmations only — the observer's own agree on their own
  // observation doesn't count toward Research Grade.
  const agreeCount = agreeCountStmt.get(observation.id, observation.userId)!.count;
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
  const rows = db
    .prepare<[string], ObservationRow>(
      "SELECT * FROM observations WHERE user_id = ? ORDER BY created_at DESC",
    )
    .all(userId);
  return rows.map((row) => withQualityGrade(observationFromRow(row)));
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
  const rows = db
    .prepare<[string, string], ObservationRow>(
      `SELECT * FROM observations
       WHERE user_id != ?
         AND id NOT IN (
           SELECT observation_id FROM comments WHERE kind = 'agree' AND user_id = ?
         )
       ORDER BY created_at ASC`,
    )
    .all(excludeUserId, excludeUserId);

  const usersById = new Map(
    db
      .prepare<[], UserRow>("SELECT * FROM users")
      .all()
      .map((row) => [row.id, userFromRow(row)]),
  );

  return rows
    .map((row) => withQualityGrade(observationFromRow(row)))
    .filter((o) => o.qualityGrade === "needs_id")
    .map((o) => obscureLocationIfSensitive(o, excludeUserId))
    .map((o) => ({ ...o, observerEmail: usersById.get(o.userId)?.email ?? "unknown" }));
}

export function createObservationForUser(
  userId: string,
  input: Omit<ServerObservation, "id" | "userId" | "createdAt" | "syncState">,
): ServerObservation {
  const observation: ServerObservation = {
    ...input,
    id: randomUUID(),
    userId,
    createdAt: new Date().toISOString(),
    syncState: "confirmed",
  };
  db.prepare(
    `INSERT INTO observations
       (id, user_id, created_at, photo_data_url, common_name, scientific_name,
        confidence, taxon_slug, sync_state, is_wild, location_name, notes)
     VALUES
       (@id, @userId, @createdAt, @photoDataUrl, @commonName, @scientificName,
        @confidence, @taxonSlug, @syncState, @isWild, @locationName, @notes)`,
  ).run({
    id: observation.id,
    userId: observation.userId,
    createdAt: observation.createdAt,
    photoDataUrl: observation.photoDataUrl,
    commonName: observation.commonName,
    scientificName: observation.scientificName,
    confidence: observation.confidence,
    taxonSlug: observation.taxonSlug,
    syncState: observation.syncState,
    isWild: observation.isWild ? 1 : 0,
    locationName: observation.locationName,
    notes: observation.notes,
  });
  return observation;
}

/**
 * Permanent account deletion. This is full erasure, not the anonymization
 * option from Part D.1 (keep contribution history, drop identity) — that's
 * a separate, still-unbuilt feature. Deleting the user row cascades (via
 * SQLite foreign keys) to their sessions, every observation they own
 * (and, transitively, comments on those observations — otherwise
 * orphaned), and every comment they authored elsewhere.
 */
export function deleteUserAccount(userId: string) {
  db.prepare("DELETE FROM users WHERE id = ?").run(userId);
}

export function deleteObservationForUser(userId: string, id: string) {
  // Comments on this observation cascade automatically via the foreign key.
  db.prepare("DELETE FROM observations WHERE id = ? AND user_id = ?").run(id, userId);
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
  const rows = db
    .prepare<
      [string, string],
      CommentRow & { observation_common_name: string; observation_taxon_slug: string }
    >(
      `SELECT comments.*,
              observations.common_name AS observation_common_name,
              observations.taxon_slug AS observation_taxon_slug
       FROM comments
       JOIN observations ON observations.id = comments.observation_id
       WHERE observations.user_id = ? AND comments.user_id != ?
       ORDER BY comments.created_at DESC`,
    )
    .all(userId, userId);

  return rows.map((row) => ({
    ...commentFromRow(row),
    observationCommonName: row.observation_common_name,
    observationTaxonSlug: row.observation_taxon_slug,
  }));
}

export function getObservationById(id: string, viewerId: string | null = null): ObservationWithGrade | null {
  const row = db.prepare<[string], ObservationRow>("SELECT * FROM observations WHERE id = ?").get(id);
  if (!row) return null;
  return obscureLocationIfSensitive(withQualityGrade(observationFromRow(row)), viewerId);
}

export function listCommentsByUser(userId: string): ObservationComment[] {
  return db
    .prepare<[string], CommentRow>("SELECT * FROM comments WHERE user_id = ? ORDER BY created_at ASC")
    .all(userId)
    .map(commentFromRow);
}

export function listCommentsForObservation(observationId: string): ObservationComment[] {
  return db
    .prepare<[string], CommentRow>(
      "SELECT * FROM comments WHERE observation_id = ? ORDER BY created_at ASC",
    )
    .all(observationId)
    .map(commentFromRow);
}

export function addCommentToObservation(
  observationId: string,
  user: Pick<User, "id" | "email">,
  body: string,
  kind: ObservationComment["kind"],
): ObservationComment | null {
  const observationExists = db
    .prepare("SELECT 1 FROM observations WHERE id = ?")
    .get(observationId);
  if (!observationExists) return null;

  const comment: ObservationComment = {
    id: randomUUID(),
    observationId,
    userId: user.id,
    userEmail: user.email,
    body,
    kind,
    createdAt: new Date().toISOString(),
  };
  db.prepare(
    `INSERT INTO comments (id, observation_id, user_id, user_email, body, kind, created_at)
     VALUES (@id, @observationId, @userId, @userEmail, @body, @kind, @createdAt)`,
  ).run(comment);
  return comment;
}

// ---------------------------------------------------------------------
// Journals (Part C.2)
// ---------------------------------------------------------------------

export type JournalPost = {
  id: string;
  authorId: string;
  authorEmail: string;
  title: string;
  body: string;
  createdAt: string;
  updatedAt: string;
};

type JournalPostRow = {
  id: string;
  author_id: string;
  author_email: string;
  title: string;
  body: string;
  created_at: string;
  updated_at: string;
};

function journalPostFromRow(row: JournalPostRow): JournalPost {
  return {
    id: row.id,
    authorId: row.author_id,
    authorEmail: row.author_email,
    title: row.title,
    body: row.body,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}

const JOURNAL_SELECT = `
  SELECT journal_posts.*, users.email AS author_email
  FROM journal_posts
  JOIN users ON users.id = journal_posts.author_id
`;

export function listJournalPosts(): JournalPost[] {
  return db
    .prepare<[], JournalPostRow>(`${JOURNAL_SELECT} ORDER BY journal_posts.created_at DESC`)
    .all()
    .map(journalPostFromRow);
}

export function getJournalPost(id: string): JournalPost | null {
  const row = db
    .prepare<[string], JournalPostRow>(`${JOURNAL_SELECT} WHERE journal_posts.id = ?`)
    .get(id);
  return row ? journalPostFromRow(row) : null;
}

export function createJournalPost(
  authorId: string,
  input: { title: string; body: string },
): JournalPost {
  const id = randomUUID();
  const now = new Date().toISOString();
  db.prepare(
    `INSERT INTO journal_posts (id, author_id, title, body, created_at, updated_at)
     VALUES (?, ?, ?, ?, ?, ?)`,
  ).run(id, authorId, input.title, input.body, now, now);
  return getJournalPost(id)!;
}

export function updateJournalPost(
  authorId: string,
  id: string,
  input: { title: string; body: string },
): JournalPost | null {
  const result = db
    .prepare(
      `UPDATE journal_posts SET title = ?, body = ?, updated_at = ?
       WHERE id = ? AND author_id = ?`,
    )
    .run(input.title, input.body, new Date().toISOString(), id, authorId);
  if (result.changes === 0) return null;
  return getJournalPost(id);
}

export function deleteJournalPost(authorId: string, id: string) {
  db.prepare("DELETE FROM journal_posts WHERE id = ? AND author_id = ?").run(id, authorId);
}
