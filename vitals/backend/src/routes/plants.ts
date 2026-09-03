import { Router } from "express";
import { z } from "zod";
import { prisma } from "../db/client";
import { CreatePlantSchema, UpdatePlantSchema } from "../models/plant";
import { computeTwinComparison } from "../services/comparison";
import { computeScoreForecast } from "../services/forecast";

export const plantsRouter = Router();

plantsRouter.post("/", async (req, res) => {
  const parsed = CreatePlantSchema.safeParse(req.body);
  if (!parsed.success) {
    return res.status(400).json({ error: parsed.error.flatten() });
  }

  const plant = await prisma.plant.create({ data: parsed.data });
  res.status(201).json(plant);
});

// Archived plants for a garden, so an accidental archive is recoverable
// instead of a one-way gate. Must be registered before GET /:id — otherwise
// "archived" would be captured as an :id and never reach this handler.
plantsRouter.get("/archived", async (req, res) => {
  const gardenId = req.query.gardenId as string | undefined;
  if (!gardenId) return res.status(400).json({ error: "gardenId is required" });

  const plants = await prisma.plant.findMany({
    where: { gardenId, active: false },
    orderBy: { createdAt: "desc" },
  });
  res.json(plants);
});

plantsRouter.get("/:id", async (req, res) => {
  const plant = await prisma.plant.findUnique({
    where: { id: req.params.id },
    include: {
      scoreHistory: { orderBy: { computedAt: "asc" } },
      checkIns: {
        orderBy: { timestamp: "desc" },
        include: { diagnosticFlags: { include: { treatmentPlan: true } } },
      },
    },
  });

  if (!plant) return res.status(404).json({ error: "not found" });

  const forecast = computeScoreForecast(
    plant.scoreHistory.map((s) => ({ score: s.score, computedAt: s.computedAt })),
  );

  res.json({ ...plant, forecast });
});

// Edit an existing plant's details (spec §4.1) — everything but species and
// garden, which are set once at creation.
plantsRouter.patch("/:id", async (req, res) => {
  const parsed = UpdatePlantSchema.safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: parsed.error.flatten() });

  const plant = await prisma.plant.update({
    where: { id: req.params.id },
    data: parsed.data,
  });
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

const SetLocationPinSchema = z.object({ x: z.number().min(0).max(1), y: z.number().min(0).max(1) });

// Yard map pin placement (spec §4.1): x/y are relative (0-1) coordinates
// on the garden's yardMapPhotoUrl, so the pin still lines up if the image
// is displayed at any size.
plantsRouter.patch("/:id/location", async (req, res) => {
  const parsed = SetLocationPinSchema.safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: parsed.error.flatten() });

  const plant = await prisma.plant.update({
    where: { id: req.params.id },
    data: { locationPin: parsed.data },
  });
  res.json(plant);
});

// Archive a plant (e.g. it died, or was removed from the garden) rather
// than hard-deleting it — its check-in history and score snapshots stay
// intact for the record, it just drops out of the active Garden Score
// roll-up, dashboard, and yard map.
plantsRouter.patch("/:id/archive", async (req, res) => {
  const plant = await prisma.plant.update({
    where: { id: req.params.id },
    data: { active: false },
  });
  res.json(plant);
});

// Undo an archive — brings the plant back into the active Garden Score
// roll-up, dashboard, and yard map.
plantsRouter.patch("/:id/unarchive", async (req, res) => {
  const plant = await prisma.plant.update({
    where: { id: req.params.id },
    data: { active: true },
  });
  res.json(plant);
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
