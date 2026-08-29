import { Router } from "express";
import { prisma } from "../db/client";
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

  res.json({ ...garden, needsAttention, risingStars, weatherAlert });
});

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

async function risingStarsFor(plantIds: string[]) {
  const rising = [];
  for (const plantId of plantIds) {
    const history = await prisma.plantScoreSnapshot.findMany({
      where: { plantId },
      orderBy: { computedAt: "desc" },
      take: 3,
    });
    if (history.length >= 2 && history[0].score > history[history.length - 1].score) {
      rising.push({ plantId, delta: history[0].score - history[history.length - 1].score });
    }
  }
  return rising.sort((a, b) => b.delta - a.delta).slice(0, 5);
}
