import { NextResponse } from "next/server";
import { getSessionUser } from "@/lib/server/session";
import { listObservationsForUser } from "@/lib/server/store";

const COLUMNS = [
  "id",
  "createdAt",
  "commonName",
  "scientificName",
  "taxonSlug",
  "confidence",
  "qualityGrade",
  "agreeCount",
  "isWild",
  "locationName",
  "lat",
  "lng",
  "notes",
] as const;

function csvEscape(value: unknown): string {
  const str = value === null || value === undefined ? "" : String(value);
  if (/[",\n]/.test(str)) {
    return `"${str.replace(/"/g, '""')}"`;
  }
  return str;
}

/**
 * Part D.1: CSV export, alongside the JSON export at /api/account/export.
 * Deliberately excludes photoDataUrl — a multi-megabyte base64 blob has no
 * sane place in a spreadsheet column; photos ship separately via
 * /api/account/export/media.zip.
 */
export async function GET() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const observations = listObservationsForUser(user.id);
  const rows = [
    COLUMNS.join(","),
    ...observations.map((o) => COLUMNS.map((col) => csvEscape(o[col as keyof typeof o])).join(",")),
  ];
  const csv = rows.join("\r\n");

  const filename = `wildkey-observations-${user.email.replace(/[^a-z0-9]/gi, "_")}.csv`;
  return new NextResponse(csv, {
    status: 200,
    headers: {
      "Content-Type": "text/csv; charset=utf-8",
      "Content-Disposition": `attachment; filename="${filename}"`,
    },
  });
}
