import { Router } from "express";
import { prisma } from "../db/client";
import { CreatePlantSchema } from "../models/plant";
import { computeTwinComparison } from "../services/comparison";

export const plantsRouter = Router();

plantsRouter.post("/", async (req, res) => {
  const parsed = CreatePlantSchema.safeParse(req.body);
  if (!parsed.success) {
    return res.status(400).json({ error: parsed.error.flatten() });
  }

  const plant = await prisma.plant.create({ data: parsed.data });
  res.status(201).json(plant);
});

plantsRouter.get("/:id", async (req, res) => {
  const plant = await prisma.plant.findUnique({
    where: { id: req.params.id },
    include: {
      scoreHistory: { orderBy: { computedAt: "asc" } },
      checkIns: {
        orderBy: { timestamp: "desc" },
        include: { diagnosticFlags: true },
      },
    },
  });

  if (!plant) return res.status(404).json({ error: "not found" });
  res.json(plant);
});

plantsRouter.get("/", async (req, res) => {
  const gardenId = req.query.gardenId as string | undefined;
  const plants = await prisma.plant.findMany({
    where: gardenId ? { gardenId, active: true } : { active: true },
    orderBy: { scoreCurrent: "asc" },
  });
  res.json(plants);
});

// "Twin plants near you" (spec §4.4): anonymized percentile comparison
// against other active plants of the same species in the same USDA zone.
// Never exposes which garden/user the cohort scores came from.
plantsRouter.get("/:id/twin-comparison", async (req, res) => {
  const plant = await prisma.plant.findUnique({
    where: { id: req.params.id },
    include: { garden: { include: { user: { select: { usdaZone: true } } } } },
  });
  if (!plant) return res.status(404).json({ error: "not found" });

  const usdaZone = plant.garden.user.usdaZone;
  if (!usdaZone) {
    return res.json({ percentile: 50, cohortSize: 0, message: "Set your USDA zone to compare with nearby plants." });
  }

  const cohort = await prisma.plant.findMany({
    where: {
      id: { not: plant.id },
      speciesId: plant.speciesId,
      active: true,
      garden: { user: { usdaZone } },
    },
    select: { scoreCurrent: true },
  });

  res.json(computeTwinComparison(plant.scoreCurrent, cohort.map((p) => p.scoreCurrent)));
});
