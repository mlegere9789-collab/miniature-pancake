import { PrismaClient } from "@prisma/client";

const prisma = new PrismaClient();

/**
 * Seeds the single demo garden the mobile app points at in Phase 1
 * (EXPO_PUBLIC_VITALS_DEMO_GARDEN_ID) — see mobile/App.tsx. Latitude/longitude
 * default to Portland, OR so the weather integration has something to query
 * out of the box; override via env vars for a different test location.
 */
async function main() {
  const latitude = Number(process.env.SEED_GARDEN_LAT ?? 45.5152);
  const longitude = Number(process.env.SEED_GARDEN_LON ?? -122.6784);

  const user = await prisma.user.upsert({
    where: { email: "demo@vitals.app" },
    update: {},
    create: { name: "Demo Gardener", email: "demo@vitals.app", usdaZone: "8b" },
  });

  const garden = await prisma.garden.upsert({
    where: { id: process.env.SEED_GARDEN_ID ?? "00000000-0000-0000-0000-000000000001" },
    update: { latitude, longitude },
    create: {
      id: process.env.SEED_GARDEN_ID ?? "00000000-0000-0000-0000-000000000001",
      userId: user.id,
      name: "Demo Garden",
      latitude,
      longitude,
    },
  });

  console.log(`Seeded garden ${garden.id} for user ${user.email} at (${latitude}, ${longitude})`);
  console.log(`Set EXPO_PUBLIC_VITALS_DEMO_GARDEN_ID=${garden.id} in vitals/mobile/.env`);
}

main()
  .catch((err) => {
    console.error(err);
    process.exit(1);
  })
  .finally(async () => {
    await prisma.$disconnect();
  });
