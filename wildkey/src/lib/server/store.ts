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

export type UserRole = "member" | "curator";

type User = {
  id: string;
  email: string;
  passwordHash: string;
  passwordSalt: string;
  role: UserRole;
  createdAt: string;
  pendingDeletionAt: string | null;
};

type UserRow = {
  id: string;
  email: string;
  password_hash: string;
  password_salt: string;
  role: string;
  created_at: string;
  pending_deletion_at: string | null;
};

function userFromRow(row: UserRow): User {
  return {
    id: row.id,
    email: row.email,
    passwordHash: row.password_hash,
    passwordSalt: row.password_salt,
    role: row.role as UserRole,
    createdAt: row.created_at,
    pendingDeletionAt: row.pending_deletion_at,
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
  /** Free-text place name — see Part G for the honest boundary on this. */
  locationName: string;
  notes: string;
  /**
   * Map data layer (Part G/C.3): coordinates with no rendered basemap yet
   * — see docs/remaining-systems-design.md for why. Optional: only set
   * when the observer granted browser geolocation on save.
   */
  lat: number | null;
  lng: number | null;
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
  lat: number | null;
  lng: number | null;
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
    lat: row.lat,
    lng: row.lng,
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
  // Demo-only bootstrap: the very first account on a fresh database becomes
  // a curator, so there's always at least one curator able to use the real
  // promote/demote flow (promoteToCurator/demoteCurator below) to grant or
  // revoke the role for anyone else from there.
  // Every account after that starts as a plain member.
  const isFirstAccount = (db.prepare("SELECT COUNT(*) as count FROM users").get() as { count: number }).count === 0;
  const user: User = {
    id: randomUUID(),
    email: normalizedEmail,
    passwordHash: hashPassword(password, passwordSalt),
    passwordSalt,
    role: isFirstAccount ? "curator" : "member",
    createdAt: new Date().toISOString(),
    pendingDeletionAt: null,
  };
  try {
    db.prepare(
      `INSERT INTO users (id, email, password_hash, password_salt, role, created_at)
       VALUES (@id, @email, @passwordHash, @passwordSalt, @role, @createdAt)`,
    ).run({
      id: user.id,
      email: user.email,
      passwordHash: user.passwordHash,
      passwordSalt: user.passwordSalt,
      role: user.role,
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

// Real brute-force protection on login — previously there was none at all:
// an attacker could try passwords against an email address as fast as the
// server could hash them, with no limit. Locks the *email*, not the caller
// (no reliable client IP in every deployment target this could run
// behind), for LOGIN_MAX_ATTEMPTS failures within LOGIN_ATTEMPT_WINDOW_MS,
// for LOGIN_LOCKOUT_MS. Both durations are overridable via env so the test
// suite can verify a lockout actually expires without waiting the real 15
// minutes (same pattern as DELETION_GRACE_PERIOD_MS above) — never set
// these in a real deployment.
const LOGIN_MAX_ATTEMPTS = 5;
const LOGIN_ATTEMPT_WINDOW_MS =
  Number(process.env.WILDKEY_LOGIN_ATTEMPT_WINDOW_MS) || 15 * 60 * 1000;
const LOGIN_LOCKOUT_MS = Number(process.env.WILDKEY_LOGIN_LOCKOUT_MS) || 15 * 60 * 1000;

type LoginLockoutRow = {
  email: string;
  failed_count: number;
  last_failed_at: string | null;
  locked_until: string | null;
};

/** Returns how many seconds remain locked, or null if the email isn't currently locked out. */
export function getLoginLockoutSeconds(email: string): number | null {
  const normalized = email.trim().toLowerCase();
  const row = db
    .prepare<[string], LoginLockoutRow>("SELECT * FROM login_lockouts WHERE email = ?")
    .get(normalized);
  if (!row?.locked_until) return null;
  const remainingMs = new Date(row.locked_until).getTime() - Date.now();
  return remainingMs > 0 ? Math.ceil(remainingMs / 1000) : null;
}

export function registerLoginFailure(email: string): void {
  const normalized = email.trim().toLowerCase();
  const now = new Date();
  const row = db
    .prepare<[string], LoginLockoutRow>("SELECT * FROM login_lockouts WHERE email = ?")
    .get(normalized);

  // A failure long after the last one starts a fresh window rather than
  // accumulating forever.
  const withinWindow =
    row?.last_failed_at && now.getTime() - new Date(row.last_failed_at).getTime() < LOGIN_ATTEMPT_WINDOW_MS;
  const failedCount = (withinWindow ? (row?.failed_count ?? 0) : 0) + 1;
  const lockedUntil =
    failedCount >= LOGIN_MAX_ATTEMPTS ? new Date(now.getTime() + LOGIN_LOCKOUT_MS).toISOString() : null;

  db.prepare(
    `INSERT INTO login_lockouts (email, failed_count, last_failed_at, locked_until)
     VALUES (?, ?, ?, ?)
     ON CONFLICT(email) DO UPDATE SET failed_count = excluded.failed_count,
       last_failed_at = excluded.last_failed_at, locked_until = excluded.locked_until`,
  ).run(normalized, failedCount, now.toISOString(), lockedUntil);
}

export function registerLoginSuccess(email: string): void {
  db.prepare("DELETE FROM login_lockouts WHERE email = ?").run(email.trim().toLowerCase());
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
  // Opportunistic sweep: this runs on effectively every authenticated
  // request, so it's the mechanism that actually carries out a deletion
  // once its grace period elapses (see scheduleAccountDeletion below) —
  // there's no real background job scheduler in this sandbox to run a
  // proper cron-style purge instead. A production deploy should run
  // purgeDueAccounts() on an actual schedule so overdue accounts are
  // purged even with no incoming traffic.
  purgeDueAccounts();
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
  return {
    id: user.id,
    email: user.email,
    role: user.role,
    createdAt: user.createdAt,
    pendingDeletionAt: user.pendingDeletionAt,
  };
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

/** ~11km grid cell at the equator — coarse enough to blur the exact site
 *  a sensitive species was found at, precise enough to still place it on
 *  a map region. */
const SENSITIVE_COORDINATE_GRID = 0.1;

function snapToGrid(value: number): number {
  return Math.round(value / SENSITIVE_COORDINATE_GRID) * SENSITIVE_COORDINATE_GRID;
}

/**
 * Coordinate counterpart to obscureLocationIfSensitive: rather than
 * dropping coordinates entirely for a sensitive species (which would
 * defeat the point of a map layer), snap them to a coarse grid cell so
 * the observation still places on a map, just not precisely enough to
 * find the specimen.
 */
function obscureCoordinatesIfSensitive<T extends { userId: string; taxonSlug: string; lat: number | null; lng: number | null }>(
  observation: T,
  viewerId: string | null,
): T {
  if (observation.userId === viewerId) return observation;
  if (observation.lat === null || observation.lng === null) return observation;
  const species = getMockSpecies(observation.taxonSlug);
  if (!species?.sensitive) return observation;
  return { ...observation, lat: snapToGrid(observation.lat), lng: snapToGrid(observation.lng) };
}

/**
 * The map data layer's read path (Part G/C.3): every observation with
 * coordinates inside a bounding box. No rendered basemap consumes this
 * yet — see docs/remaining-systems-design.md — but the query and the
 * grid-cell obscuring for sensitive species are real.
 */
export function listObservationsInBounds(
  bounds: { minLat: number; maxLat: number; minLng: number; maxLng: number },
  viewerId: string | null,
): ObservationWithGrade[] {
  const rows = db
    .prepare<[number, number, number, number], ObservationRow>(
      `SELECT * FROM observations
       WHERE lat IS NOT NULL AND lng IS NOT NULL
         AND lat BETWEEN ? AND ? AND lng BETWEEN ? AND ?`,
    )
    .all(bounds.minLat, bounds.maxLat, bounds.minLng, bounds.maxLng);
  return rows
    .map((row) => withQualityGrade(observationFromRow(row)))
    .map((o) => obscureLocationIfSensitive(o, viewerId))
    .map((o) => obscureCoordinatesIfSensitive(o, viewerId));
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
        confidence, taxon_slug, sync_state, is_wild, location_name, notes, lat, lng)
     VALUES
       (@id, @userId, @createdAt, @photoDataUrl, @commonName, @scientificName,
        @confidence, @taxonSlug, @syncState, @isWild, @locationName, @notes, @lat, @lng)`,
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
    lat: observation.lat,
    lng: observation.lng,
  });
  return observation;
}

/**
 * Permanent, immediate account deletion — the actual erasure step behind
 * the grace period below, and the one purgeDueAccounts() calls once a
 * scheduled deletion comes due. Full erasure, not the anonymization option
 * below it (keep contribution history, drop identity). Deleting the user
 * row cascades (via SQLite foreign keys) to their sessions, every
 * observation they own (and, transitively, comments on those observations
 * — otherwise orphaned), and every comment they authored elsewhere.
 */
export function deleteUserAccount(userId: string) {
  db.prepare("DELETE FROM users WHERE id = ?").run(userId);
}

// Overridable so the test suite can verify the actual purge path (see
// tests/server.mjs) without waiting 14 real days — not something a real
// deployment would ever set.
const DELETION_GRACE_PERIOD_MS =
  Number(process.env.WILDKEY_DELETION_GRACE_PERIOD_MS) || 14 * 24 * 60 * 60 * 1000;

/**
 * The real grace-period/undo window this app previously documented as
 * "not built yet." Deletion isn't immediate: it's scheduled 14 days out,
 * and the account (and its data) stays fully intact and usable until then
 * — signing back in and cancelling is the undo path. Actual erasure
 * happens in purgeDueAccounts(), called opportunistically from
 * getUserBySessionToken on real request traffic (see the comment there
 * for the honest limit of that approach in this sandbox).
 */
export function scheduleAccountDeletion(userId: string): string {
  const purgeAt = new Date(Date.now() + DELETION_GRACE_PERIOD_MS).toISOString();
  db.prepare("UPDATE users SET pending_deletion_at = ? WHERE id = ?").run(purgeAt, userId);
  return purgeAt;
}

export function cancelAccountDeletion(userId: string): boolean {
  const result = db
    .prepare("UPDATE users SET pending_deletion_at = NULL WHERE id = ? AND pending_deletion_at IS NOT NULL")
    .run(userId);
  return result.changes > 0;
}

/** Permanently erases every account whose scheduled deletion date has passed. Returns how many were purged. */
export function purgeDueAccounts(): number {
  const due = db
    .prepare<[string], { id: string }>("SELECT id FROM users WHERE pending_deletion_at IS NOT NULL AND pending_deletion_at <= ?")
    .all(new Date().toISOString());
  for (const { id } of due) {
    deleteUserAccount(id);
  }
  return due.length;
}

/**
 * Part D.1: keep the contribution history, drop the identity — the
 * opposite tradeoff from deleteUserAccount above. Every observation,
 * comment, journal post, guide, and project this account created stays
 * exactly where it is, still attributed to the account, but the account
 * itself becomes unreachable: the email is replaced with an anonymous
 * placeholder, the password is overwritten with a random value nobody
 * (including this server) retains, and every existing session is
 * destroyed. There is deliberately no path back — this is irreversible
 * from the user's side by design, same as the real thing would be.
 */
export function anonymizeUserAccount(userId: string): string {
  const anonymizedEmail = `deleted-user-${userId.slice(0, 8)}@anonymized.wildkey`;
  const passwordSalt = randomBytes(16).toString("hex");
  const passwordHash = hashPassword(randomBytes(32).toString("hex"), passwordSalt);

  const transaction = db.transaction(() => {
    db.prepare("UPDATE users SET email = ?, password_hash = ?, password_salt = ? WHERE id = ?").run(
      anonymizedEmail,
      passwordHash,
      passwordSalt,
      userId,
    );
    db.prepare("DELETE FROM sessions WHERE user_id = ?").run(userId);
    // Comments denormalize the author's email for display — keep it in
    // sync so old comments don't keep showing the real address.
    db.prepare("UPDATE comments SET user_email = ? WHERE user_id = ?").run(anonymizedEmail, userId);
  });
  transaction();

  return anonymizedEmail;
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

// ---------------------------------------------------------------------
// Guides (Part C.2)
// ---------------------------------------------------------------------

export type Guide = {
  id: string;
  curatorId: string;
  curatorEmail: string;
  title: string;
  description: string;
  taxonSlugs: string[];
  createdAt: string;
};

type GuideRow = {
  id: string;
  curator_id: string;
  curator_email: string;
  title: string;
  description: string;
  taxon_slugs: string;
  created_at: string;
};

function guideFromRow(row: GuideRow): Guide {
  let taxonSlugs: string[] = [];
  try {
    taxonSlugs = JSON.parse(row.taxon_slugs);
  } catch {
    taxonSlugs = [];
  }
  return {
    id: row.id,
    curatorId: row.curator_id,
    curatorEmail: row.curator_email,
    title: row.title,
    description: row.description,
    taxonSlugs,
    createdAt: row.created_at,
  };
}

const GUIDE_SELECT = `
  SELECT guides.*, users.email AS curator_email
  FROM guides
  JOIN users ON users.id = guides.curator_id
`;

export function listGuides(): Guide[] {
  return db
    .prepare<[], GuideRow>(`${GUIDE_SELECT} ORDER BY guides.created_at DESC`)
    .all()
    .map(guideFromRow);
}

export function getGuide(id: string): Guide | null {
  const row = db.prepare<[string], GuideRow>(`${GUIDE_SELECT} WHERE guides.id = ?`).get(id);
  return row ? guideFromRow(row) : null;
}

export function createGuide(
  curatorId: string,
  input: { title: string; description: string; taxonSlugs: string[] },
): Guide {
  const id = randomUUID();
  db.prepare(
    `INSERT INTO guides (id, curator_id, title, description, taxon_slugs, created_at)
     VALUES (?, ?, ?, ?, ?, ?)`,
  ).run(id, curatorId, input.title, input.description, JSON.stringify(input.taxonSlugs), new Date().toISOString());
  return getGuide(id)!;
}

export function deleteGuide(curatorId: string, id: string) {
  db.prepare("DELETE FROM guides WHERE id = ? AND curator_id = ?").run(id, curatorId);
}

// ---------------------------------------------------------------------
// Projects (Part C.2): Collection (saved search) + Traditional (opt-in
// membership) types.
// ---------------------------------------------------------------------

export type ProjectType = "collection" | "traditional";

export type Project = {
  id: string;
  ownerId: string;
  ownerEmail: string;
  type: ProjectType;
  title: string;
  description: string;
  taxonFilter: string[];
  memberCount: number;
  createdAt: string;
};

type ProjectRow = {
  id: string;
  owner_id: string;
  owner_email: string;
  type: string;
  title: string;
  description: string;
  taxon_filter: string;
  member_count: number;
  created_at: string;
};

function projectFromRow(row: ProjectRow): Project {
  let taxonFilter: string[] = [];
  try {
    taxonFilter = JSON.parse(row.taxon_filter);
  } catch {
    taxonFilter = [];
  }
  return {
    id: row.id,
    ownerId: row.owner_id,
    ownerEmail: row.owner_email,
    type: row.type as ProjectType,
    title: row.title,
    description: row.description,
    taxonFilter,
    memberCount: row.member_count,
    createdAt: row.created_at,
  };
}

const PROJECT_SELECT = `
  SELECT projects.*, users.email AS owner_email,
         (SELECT COUNT(*) FROM project_members WHERE project_members.project_id = projects.id) AS member_count
  FROM projects
  JOIN users ON users.id = projects.owner_id
`;

export function listProjects(): Project[] {
  return db
    .prepare<[], ProjectRow>(`${PROJECT_SELECT} ORDER BY projects.created_at DESC`)
    .all()
    .map(projectFromRow);
}

export function getProject(id: string): Project | null {
  const row = db.prepare<[string], ProjectRow>(`${PROJECT_SELECT} WHERE projects.id = ?`).get(id);
  return row ? projectFromRow(row) : null;
}

export function createProject(
  ownerId: string,
  input: { type: ProjectType; title: string; description: string; taxonFilter: string[] },
): Project {
  const id = randomUUID();
  db.prepare(
    `INSERT INTO projects (id, owner_id, type, title, description, taxon_filter, created_at)
     VALUES (?, ?, ?, ?, ?, ?, ?)`,
  ).run(id, ownerId, input.type, input.title, input.description, JSON.stringify(input.taxonFilter), new Date().toISOString());

  if (input.type === "traditional") {
    db.prepare(
      "INSERT INTO project_members (project_id, user_id, joined_at) VALUES (?, ?, ?)",
    ).run(id, ownerId, new Date().toISOString());
  }
  return getProject(id)!;
}

export function deleteProject(ownerId: string, id: string) {
  db.prepare("DELETE FROM projects WHERE id = ? AND owner_id = ?").run(id, ownerId);
}

export function isProjectMember(projectId: string, userId: string): boolean {
  return Boolean(
    db
      .prepare("SELECT 1 FROM project_members WHERE project_id = ? AND user_id = ?")
      .get(projectId, userId),
  );
}

export function joinProject(projectId: string, userId: string) {
  db.prepare(
    "INSERT OR IGNORE INTO project_members (project_id, user_id, joined_at) VALUES (?, ?, ?)",
  ).run(projectId, userId, new Date().toISOString());
}

export function leaveProject(projectId: string, userId: string) {
  db.prepare("DELETE FROM project_members WHERE project_id = ? AND user_id = ?").run(
    projectId,
    userId,
  );
}

export function listProjectMembers(projectId: string): { userId: string; email: string; joinedAt: string }[] {
  return db
    .prepare<
      [string],
      { user_id: string; email: string; joined_at: string }
    >(
      `SELECT project_members.user_id, users.email, project_members.joined_at
       FROM project_members
       JOIN users ON users.id = project_members.user_id
       WHERE project_members.project_id = ?
       ORDER BY project_members.joined_at ASC`,
    )
    .all(projectId)
    .map((r) => ({ userId: r.user_id, email: r.email, joinedAt: r.joined_at }));
}

/**
 * Collection-type projects are a saved search, not a static list: matching
 * observations are whatever currently has one of the project's taxon
 * slugs, computed live — exactly the "Collection (saved search)" spec
 * from Part C.2, not a snapshot taken at creation time.
 */
export function listObservationsMatchingProject(
  taxonFilter: string[],
  viewerId: string | null,
): ObservationWithGrade[] {
  if (taxonFilter.length === 0) return [];
  const placeholders = taxonFilter.map(() => "?").join(",");
  const rows = db
    .prepare<string[], ObservationRow>(
      `SELECT * FROM observations WHERE taxon_slug IN (${placeholders}) ORDER BY created_at DESC`,
    )
    .all(...taxonFilter);
  return rows
    .map((row) => withQualityGrade(observationFromRow(row)))
    .map((o) => obscureLocationIfSensitive(o, viewerId));
}

// ---------------------------------------------------------------------
// Curator / moderation tools (Part C.2, F): flagging + a documented,
// auditable resolution — every curator action is logged with a required
// written reason, never a silent state flip.
// ---------------------------------------------------------------------

export type FlagStatus = "open" | "resolved" | "dismissed";

export type ObservationFlag = {
  id: string;
  observationId: string;
  reporterId: string;
  reporterEmail: string;
  reason: string;
  status: FlagStatus;
  createdAt: string;
  observationCommonName: string;
  observationTaxonSlug: string;
};

type FlagRow = {
  id: string;
  observation_id: string;
  reporter_id: string;
  reporter_email: string;
  reason: string;
  status: string;
  created_at: string;
  observation_common_name: string;
  observation_taxon_slug: string;
};

function flagFromRow(row: FlagRow): ObservationFlag {
  return {
    id: row.id,
    observationId: row.observation_id,
    reporterId: row.reporter_id,
    reporterEmail: row.reporter_email,
    reason: row.reason,
    status: row.status as FlagStatus,
    createdAt: row.created_at,
    observationCommonName: row.observation_common_name,
    observationTaxonSlug: row.observation_taxon_slug,
  };
}

const FLAG_SELECT = `
  SELECT observation_flags.*, users.email AS reporter_email,
         observations.common_name AS observation_common_name,
         observations.taxon_slug AS observation_taxon_slug
  FROM observation_flags
  JOIN users ON users.id = observation_flags.reporter_id
  JOIN observations ON observations.id = observation_flags.observation_id
`;

export function flagObservation(
  observationId: string,
  reporterId: string,
  reason: string,
): ObservationFlag | null {
  const exists = db.prepare("SELECT 1 FROM observations WHERE id = ?").get(observationId);
  if (!exists) return null;

  const id = randomUUID();
  db.prepare(
    `INSERT INTO observation_flags (id, observation_id, reporter_id, reason, status, created_at)
     VALUES (?, ?, ?, ?, 'open', ?)`,
  ).run(id, observationId, reporterId, reason, new Date().toISOString());

  const row = db.prepare<[string], FlagRow>(`${FLAG_SELECT} WHERE observation_flags.id = ?`).get(id);
  return row ? flagFromRow(row) : null;
}

export function listOpenFlags(): ObservationFlag[] {
  return db
    .prepare<[], FlagRow>(`${FLAG_SELECT} WHERE observation_flags.status = 'open' ORDER BY observation_flags.created_at ASC`)
    .all()
    .map(flagFromRow);
}

/**
 * Resolving or dismissing a flag always requires a written reason, logged
 * as a curator_action row — "any account action includes a clear,
 * appealable written reason" (Part F), not just a status flip nobody can
 * see the justification for.
 */
export function resolveFlag(
  flagId: string,
  curatorId: string,
  action: "resolved" | "dismissed",
  reason: string,
): boolean {
  const flag = db.prepare<[string], { status: string }>(
    "SELECT status FROM observation_flags WHERE id = ?",
  ).get(flagId);
  if (!flag || flag.status !== "open") return false;

  const transaction = db.transaction(() => {
    db.prepare("UPDATE observation_flags SET status = ? WHERE id = ?").run(action, flagId);
    db.prepare(
      `INSERT INTO curator_actions (id, flag_id, curator_id, action, reason, created_at)
       VALUES (?, ?, ?, ?, ?, ?)`,
    ).run(randomUUID(), flagId, curatorId, action, reason, new Date().toISOString());
  });
  transaction();
  return true;
}

export type CuratorAction = {
  id: string;
  flagId: string;
  curatorEmail: string;
  action: string;
  reason: string;
  createdAt: string;
};

export function listCuratorActionsForFlag(flagId: string): CuratorAction[] {
  return db
    .prepare<
      [string],
      { id: string; flag_id: string; curator_email: string; action: string; reason: string; created_at: string }
    >(
      `SELECT curator_actions.id, curator_actions.flag_id, users.email AS curator_email,
              curator_actions.action, curator_actions.reason, curator_actions.created_at
       FROM curator_actions
       JOIN users ON users.id = curator_actions.curator_id
       WHERE curator_actions.flag_id = ?
       ORDER BY curator_actions.created_at ASC`,
    )
    .all(flagId)
    .map((r) => ({
      id: r.id,
      flagId: r.flag_id,
      curatorEmail: r.curator_email,
      action: r.action,
      reason: r.reason,
      createdAt: r.created_at,
    }));
}

export type RoleChange = {
  id: string;
  targetEmail: string;
  changedByEmail: string;
  oldRole: UserRole;
  newRole: UserRole;
  reason: string;
  createdAt: string;
};

/**
 * The curator role has had exactly one way in since Part F was built: the
 * first account on a fresh database bootstraps as curator, documented as
 * a demo-only mechanism with "no invite flow exists yet." This is that
 * invite flow — an existing curator can promote another account by email,
 * with the same written-reason audit discipline as flag resolution
 * (Part F: "any account action includes a clear, appealable written
 * reason").
 */
export function promoteToCurator(
  actingCuratorId: string,
  targetEmail: string,
  reason: string,
): { ok: true } | { ok: false; error: string } {
  const target = db
    .prepare<[string], UserRow>("SELECT * FROM users WHERE email = ?")
    .get(targetEmail.trim().toLowerCase());
  if (!target) return { ok: false, error: "No account with that email." };
  if (target.role === "curator") return { ok: false, error: "That account is already a curator." };

  const transaction = db.transaction(() => {
    db.prepare("UPDATE users SET role = 'curator' WHERE id = ?").run(target.id);
    db.prepare(
      `INSERT INTO role_changes (id, target_user_id, changed_by_id, old_role, new_role, reason, created_at)
       VALUES (?, ?, ?, 'member', 'curator', ?, ?)`,
    ).run(randomUUID(), target.id, actingCuratorId, reason, new Date().toISOString());
  });
  transaction();
  return { ok: true };
}

/**
 * The mirror of promoteToCurator, same audit discipline. A curator can't
 * revoke their own role here (self-service demotion would let the last
 * curator on a database lock everyone out of moderation with no recovery
 * path) — that has to come from another curator.
 */
export function demoteCurator(
  actingCuratorId: string,
  targetEmail: string,
  reason: string,
): { ok: true } | { ok: false; error: string } {
  const target = db
    .prepare<[string], UserRow>("SELECT * FROM users WHERE email = ?")
    .get(targetEmail.trim().toLowerCase());
  if (!target) return { ok: false, error: "No account with that email." };
  if (target.role !== "curator") return { ok: false, error: "That account isn't a curator." };
  if (target.id === actingCuratorId) {
    return { ok: false, error: "You can't remove your own curator role — ask another curator to." };
  }

  const transaction = db.transaction(() => {
    db.prepare("UPDATE users SET role = 'member' WHERE id = ?").run(target.id);
    db.prepare(
      `INSERT INTO role_changes (id, target_user_id, changed_by_id, old_role, new_role, reason, created_at)
       VALUES (?, ?, ?, 'curator', 'member', ?, ?)`,
    ).run(randomUUID(), target.id, actingCuratorId, reason, new Date().toISOString());
  });
  transaction();
  return { ok: true };
}

export function listCurators(): { id: string; email: string }[] {
  return db
    .prepare<[], { id: string; email: string }>("SELECT id, email FROM users WHERE role = 'curator' ORDER BY email ASC")
    .all();
}

export function listRoleChanges(): RoleChange[] {
  return db
    .prepare<
      [],
      {
        id: string;
        target_email: string;
        changed_by_email: string;
        old_role: string;
        new_role: string;
        reason: string;
        created_at: string;
      }
    >(
      `SELECT role_changes.id, target.email AS target_email, changer.email AS changed_by_email,
              role_changes.old_role, role_changes.new_role, role_changes.reason, role_changes.created_at
       FROM role_changes
       JOIN users AS target ON target.id = role_changes.target_user_id
       JOIN users AS changer ON changer.id = role_changes.changed_by_id
       ORDER BY role_changes.created_at DESC`,
    )
    .all()
    .map((r) => ({
      id: r.id,
      targetEmail: r.target_email,
      changedByEmail: r.changed_by_email,
      oldRole: r.old_role as UserRole,
      newRole: r.new_role as UserRole,
      reason: r.reason,
      createdAt: r.created_at,
    }));
}
