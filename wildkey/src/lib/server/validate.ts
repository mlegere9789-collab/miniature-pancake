/**
 * Shared request-body validation helpers. Before this, most API routes
 * only checked `typeof value === "string"`/`"number"` and trusted
 * everything else — no length caps on free text (a journal post body,
 * a project description, a comment could be stored at any size an
 * attacker cared to send), and numeric fields accepted `NaN`/`Infinity`/
 * out-of-range values as long as `typeof x === "number"` passed (it does
 * for all three). Centralizing the checks here instead of re-deriving
 * ad hoc limits in every route keeps them actually consistent.
 */

export function requiredString(value: unknown, maxLength: number): string | null {
  if (typeof value !== "string") return null;
  const trimmed = value.trim();
  if (trimmed.length === 0 || trimmed.length > maxLength) return null;
  return trimmed;
}

export function optionalString(value: unknown, maxLength: number): string {
  if (typeof value !== "string") return "";
  return value.trim().slice(0, maxLength);
}

/** Rejects NaN, +/-Infinity, and anything outside [min, max] — not just "is it a number." */
export function boundedNumber(value: unknown, min: number, max: number): number | null {
  if (typeof value !== "number" || !Number.isFinite(value)) return null;
  if (value < min || value > max) return null;
  return value;
}
