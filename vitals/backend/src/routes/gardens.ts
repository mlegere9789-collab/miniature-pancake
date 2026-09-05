import { Router } from "express";
import { z } from "zod";
import { prisma } from "../db/client";
import { computeLeaderboardRank } from "../services/comparison";
import { computeRisingStars } from "../services/dashboard";
import { detectOutbreaks } from "../services/outbreakDetection";
import { computeWeeklyReportCard } from "../services/reportCard";
import { defaultDormancyMonths } from "../services/scoring";
import { lookupSpeciesDormancy } from "../services/speciesDormancy";
import { fetchWeatherSignals } from "../services/weatherService";

export const gardensRouter = Router();

gardensRouter.get("/:id", async (req, res) => {
  const garden = await prisma.garden.findUnique({
    where: { id: req.params.id },
    include: {
      plants: { where: { active: true }, orderBy: { scoreCurrent: "asc" } },
      scoreHistory: { orderBy: { computedAt: "asc" }, take: 90 },
    },
  });
  if (!garden) return res.status(404).json({ error: "not found" });

  const needsAttention = [...garden.plants].sort((a, b) => a.scoreCurrent - b.scoreCurrent).slice(0, 10);
  const risingStars = await risingStarsFor(garden.plants.map((p) => p.id));
  const weatherAlert = await frostRiskAlert(garden.latitude, garden.longitude, garden.plants);
  const outbreakAlerts = await outbreakAlertsFor(garden.id, garden.userId);

  res.json({ ...garden, needsAttention, risingStars, weatherAlert, outbreakAlerts });
});

// Weekly Garden Report Card (spec §4.5): aggregates the last 7 days of
// score history into a headline, top-improving plants, and plants that need
// attention. Intended as the payload for a weekly push/email recap.
gardensRouter.get("/:id/report-card", async (req, res) => {
  const gardenId = req.params.id;
  const periodEnd = new Date();
  const periodStart = new Date(periodEnd.getTime() - 7 * 24 * 60 * 60 * 1000);

  const garden = await prisma.garden.findUnique({
    where: { id: gardenId },
    include: { plants: { where: { active: true } } },
  });
  if (!garden) return res.status(404).json({ error: "not found" });

  const gardenScoreStart = await scoreNearOrBefore(
    () => prisma.gardenScoreSnapshot.findFirst({ where: { gardenId, computedAt: { gte: periodStart } }, orderBy: { computedAt: "asc" } }),
    () => prisma.gardenScoreSnapshot.findFirst({ where: { gardenId, computedAt: { lt: periodStart } }, orderBy: { computedAt: "desc" } }),
    garden.scoreCurrent,
  );

  const plantSummaries = await Promise.all(
    garden.plants.map(async (plant) => {
      const scoreStart = await scoreNearOrBefore(
        () =>
          prisma.plantScoreSnapshot.findFirst({
            where: { plantId: plant.id, computedAt: { gte: periodStart } },
            orderBy: { computedAt: "asc" },
          }),
        () =>
          prisma.plantScoreSnapshot.findFirst({
            where: { plantId: plant.id, computedAt: { lt: periodStart } },
            orderBy: { computedAt: "desc" },
          }),
        plant.scoreCurrent,
      );
      return {
        plantId: plant.id,
        name: plant.nickname || plant.speciesName,
        scoreStart,
        scoreEnd: plant.scoreCurrent,
      };
    }),
  );

  const checkInsCompleted = await prisma.checkIn.count({
    where: { plant: { gardenId }, timestamp: { gte: periodStart, lte: periodEnd } },
  });

  const reportCard = computeWeeklyReportCard({
    periodStart,
    periodEnd,
    gardenScoreStart,
    gardenScoreEnd: garden.scoreCurrent,
    plantSummaries,
    checkInsCompleted,
  });

  res.json(reportCard);
});

// Default dormancy months for the "Dormant in winter" toggle on Add Plant
// (spec §4.7). When a `speciesId` query param matches the curated species
// dormancy table, the response reflects that species' actual habit
// (deciduous/evergreen/annual) and whether the toggle should default on;
// otherwise it falls back to the hemisphere-only heuristic.
gardensRouter.get("/:id/dormancy-defaults", async (req, res) => {
  const garden = await prisma.garden.findUnique({ where: { id: req.params.id }, select: { latitude: true } });
  if (!garden) return res.status(404).json({ error: "not found" });

  const speciesId = typeof req.query.speciesId === "string" ? req.query.speciesId : undefined;
  if (speciesId) {
    const lookup = lookupSpeciesDormancy(speciesId, garden.latitude);
    return res.json(lookup);
  }

  res.json({ known: false, habit: null, months: defaultDormancyMonths(garden.latitude), suggestDormant: false });
});

const SetYardMapPhotoSchema = z.object({ photoUrl: z.string().min(1) });

// Yard map (spec §4.1): the wide yard photo that Plant.locationPin
// coordinates are relative to. Set once, then plants are pinned onto it
// via PATCH /plants/:id/location.
gardensRouter.patch("/:id/yard-map", async (req, res) => {
  const parsed = SetYardMapPhotoSchema.safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: parsed.error.flatten() });

  const garden = await prisma.garden.update({
    where: { id: req.params.id },
    data: { yardMapPhotoUrl: parsed.data.photoUrl },
  });
  res.json(garden);
});

const SetLeaderboardOptInSchema = z.object({ leaderboardOptIn: z.boolean() });

// Opt-in only (spec §4.6). Toggling this on/off is the entire consent flow —
// no other setting exposes a garden's score to the leaderboard.
gardensRouter.patch("/:id/leaderboard-opt-in", async (req, res) => {
  const parsed = SetLeaderboardOptInSchema.safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: parsed.error.flatten() });

  const garden = await prisma.garden.update({
    where: { id: req.params.id },
    data: { leaderboardOptIn: parsed.data.leaderboardOptIn },
  });
  res.json(garden);
});

// Anonymized neighborhood leaderboard (spec §4.6): rank among other
// opted-in gardens in the same USDA zone. Requires this garden to have
// opted in itself — you can't see the leaderboard without joining it.
gardensRouter.get("/:id/leaderboard", async (req, res) => {
  const garden = await prisma.garden.findUnique({
    where: { id: req.params.id },
    include: { user: { select: { usdaZone: true } } },
  });
  if (!garden) return res.status(404).json({ error: "not found" });
  if (!garden.leaderboardOptIn) {
    return res.status(403).json({ error: "this garden has not opted in to the leaderboard" });
  }
  if (!garden.user.usdaZone) {
    return res.status(400).json({ error: "set a USDA zone to join the leaderboard" });
  }

  const others = await prisma.garden.findMany({
    where: {
      id: { not: garden.id },
      leaderboardOptIn: true,
      user: { usdaZone: garden.user.usdaZone },
    },
    select: { scoreCurrent: true },
  });

  res.json(computeLeaderboardRank(garden.scoreCurrent, others.map((g) => g.scoreCurrent)));
});

async function scoreNearOrBefore(
  findWithinPeriod: () => Promise<{ score: number } | null>,
  findBeforePeriod: () => Promise<{ score: number } | null>,
  fallback: number,
): Promise<number> {
  const withinPeriod = await findWithinPeriod();
  if (withinPeriod) return withinPeriod.score;
  const beforePeriod = await findBeforePeriod();
  return beforePeriod?.score ?? fallback;
}

// Weather-aware "Frost tonight" banner (spec §4.3): null when no risk, or
// there's no garden location on file yet.
async function frostRiskAlert(
  latitude: number | null,
  longitude: number | null,
  plants: { id: string; frostSensitive: boolean }[],
) {
  if (latitude == null || longitude == null) return null;

  try {
    const weather = await fetchWeatherSignals(latitude, longitude);
    if (!weather.frostRiskTonight) return null;

    const affectedPlantIds = plants.filter((p) => p.frostSensitive).map((p) => p.id);
    if (affectedPlantIds.length === 0) return null;

    return { type: "frost", minTempTonightC: weather.minTempTonightC, affectedPlantIds };
  } catch {
    return null;
  }
}

// Regional outbreak alerts (spec §4.3/§4.5, "Idea 4"): flags conditions
// independently reported by several gardens in the same USDA zone in the
// last 14 days. Never exposes which gardens reported — only the condition
// name and how many gardens are seeing it.
async function outbreakAlertsFor(gardenId: string, userId: string) {
  const user = await prisma.user.findUnique({ where: { id: userId }, select: { usdaZone: true } });
  if (!user?.usdaZone) return [];

  const since = new Date(Date.now() - 14 * 24 * 60 * 60 * 1000);
  const flags = await prisma.diagnosticFlag.findMany({
    where: {
      status: { in: ["OPEN", "MONITORING"] },
      checkIn: {
        timestamp: { gte: since },
        plant: { garden: { user: { usdaZone: user.usdaZone } } },
      },
    },
    select: { condition: true, checkIn: { select: { plant: { select: { gardenId: true } } } } },
  });

  return detectOutbreaks(flags.map((f) => ({ gardenId: f.checkIn.plant.gardenId, condition: f.condition })));
}

async function risingStarsFor(plantIds: string[]) {
  // One query per plant, but run concurrently rather than sequentially —
  // fine at Phase-1 scale, worth revisiting with a single grouped query if
  // gardens grow into the hundreds of plants.
  const histories = await Promise.all(
    plantIds.map(async (plantId) => {
      const snapshots = await prisma.plantScoreSnapshot.findMany({
        where: { plantId },
        orderBy: { computedAt: "desc" },
        take: 3,
      });
      return { plantId, recentScores: snapshots.map((s) => s.score) };
    }),
  );
  return computeRisingStars(histories);
}
