import { Router } from "express";
import { z } from "zod";
import { prisma } from "../db/client";

export const treatmentPlansRouter = Router();

const CompleteTreatmentPlanSchema = z.object({ completed: z.boolean() });

// Marks a treatment plan's steps as done (spec §4.2). Feeds directly into
// care-consistency scoring: an open (incomplete) treatment plan on a check-in
// counts as `outstandingTreatmentsIgnored` on the plant's next check-in.
treatmentPlansRouter.patch("/:id", async (req, res) => {
  const parsed = CompleteTreatmentPlanSchema.safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: parsed.error.flatten() });

  const plan = await prisma.treatmentPlan.update({
    where: { id: req.params.id },
    data: { completed: parsed.data.completed },
  });
  res.json(plan);
});
