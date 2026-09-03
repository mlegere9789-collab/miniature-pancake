import { NextResponse } from "next/server";
import { ZipArchive } from "archiver";
import { getSessionUser } from "@/lib/server/session";
import { listObservationsForUser } from "@/lib/server/store";

const EXTENSION_BY_MIME: Record<string, string> = {
  "image/png": "png",
  "image/jpeg": "jpg",
  "image/webp": "webp",
  "image/gif": "gif",
};

function decodeDataUrl(dataUrl: string): { buffer: Buffer; extension: string } | null {
  const match = /^data:(image\/[a-z+.-]+);base64,(.+)$/i.exec(dataUrl);
  if (!match) return null;
  const [, mime, base64] = match;
  return { buffer: Buffer.from(base64, "base64"), extension: EXTENSION_BY_MIME[mime] ?? "bin" };
}

/**
 * Part D.1: a real media zip, not the JSON export's inline base64 data
 * URLs. Photos currently only exist as data URLs in the store (see
 * README) — this endpoint is the same data, decoded into real image
 * files inside a zip, one per observation.
 */
export async function GET() {
  const user = await getSessionUser();
  if (!user) return NextResponse.json({ error: "Not signed in." }, { status: 401 });

  const observations = listObservationsForUser(user.id);

  const archive = new ZipArchive({ zlib: { level: 9 } });
  const chunks: Buffer[] = [];
  archive.on("data", (chunk: Buffer) => chunks.push(chunk));
  const done = new Promise<void>((resolve, reject) => {
    archive.on("end", () => resolve());
    archive.on("error", reject);
  });

  const usedNames = new Set<string>();
  for (const observation of observations) {
    const decoded = decodeDataUrl(observation.photoDataUrl);
    if (!decoded) continue;
    const safeName = observation.commonName.replace(/[^a-z0-9]+/gi, "-").toLowerCase();
    let filename = `${observation.createdAt.slice(0, 10)}-${safeName}-${observation.id.slice(0, 8)}.${decoded.extension}`;
    // Defensive: names are already unique via the id suffix, but guard
    // against any future change to that scheme silently colliding.
    while (usedNames.has(filename)) {
      filename = `dup-${filename}`;
    }
    usedNames.add(filename);
    archive.append(decoded.buffer, { name: filename });
  }

  archive.finalize();
  await done;

  const zipBuffer = Buffer.concat(chunks);
  const filename = `wildkey-photos-${user.email.replace(/[^a-z0-9]/gi, "_")}.zip`;
  return new NextResponse(new Uint8Array(zipBuffer), {
    status: 200,
    headers: {
      "Content-Type": "application/zip",
      "Content-Disposition": `attachment; filename="${filename}"`,
    },
  });
}
