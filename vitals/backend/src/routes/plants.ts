import { Router } from "express";
import { prisma } from "../db/client";
import { CreatePlantSchema } from "../models/plant";

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
