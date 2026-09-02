import { Router } from "express";
import { prisma } from "../db/client";
import { CreateCheckInSchema } from "../models/checkin";
import { runDiagnosticEngine } from "../services/diagnosticEngine";
import { computeGardenScore, computePlantScore, isOverdueForCheckin, isSeasonallyDormant } from "../services/scoring";
import { buildTreatmentPlan } from "../services/treatmentPlans";
import { applyWeatherPenalty, fetchWeatherSignals } from "../services/weatherService";

export const checkinsRouter = Router();

checkinsRouter.post("/", async (req, res) => {
  const parsed = CreateCheckInSchema.safeParse(req.body);
  if (!parsed.success) {
    return res.status(400).json({ error: parsed.error.flatten() });
  }
  const { plantId, photoUrl } = parsed.data;

  const plant = await prisma.plant.findUnique({
    where: { id: plantId },
    include: {
      checkIns: { orderBy: { timestamp: "desc" }, take: 1 },
      scoreHistory: { orderBy: { computedAt: "desc" }, take: 3 },
      garden: { select: { latitude: true, longitude: true } },
    },
  });
  if (!plant) return res.status(404).json({ error: "plant not found" });

  const lastCheckIn = plant.checkIns[0];
  const daysSinceLast = lastCheckIn
    ? (Date.now() - lastCheckIn.timestamp.getTime()) / (1000 * 60 * 60 * 24)
    : plant.checkinCadenceDays; // first check-in: treat as on-schedule
  const daysLateForCheckin = daysSinceLast - plant.checkinCadenceDays;

  const openTreatments = await prisma.treatmentPlan.findMany({
    where: { diagnosticFlag: { checkIn: { plantId } }, completed: false },
  });

  const engineOutput = await runDiagnosticEngine(photoUrl);

  // Environmental-fit is predictive, not just reactive (spec §3.1.3): a
  // frost/drought mismatch drags the score down even before visible symptoms.
  const { latitude, longitude } = plant.garden;
  if (latitude != null && longitude != null) {
    try {
      const weather = await fetchWeatherSignals(latitude, longitude);
      engineOutput.environmentalFit.score = applyWeatherPenalty(
        engineOutput.environmentalFit.score,
        weather,
        plant.frostSensitive,
      );
      engineOutput.environmentalFit.frostRiskDetected = weather.frostRiskTonight;
      engineOutput.environmentalFit.droughtStressDetected =
        engineOutput.environmentalFit.droughtStressDetected || weather.droughtStressDetected;
    } catch {
      // Weather lookup is best-effort; scoring proceeds on the engine's own estimate if it fails.
    }
  }

  const { finalScore, breakdown } = computePlantScore({
    engineOutput,
    daysLateForCheckin,
    outstandingTreatmentsIgnored: openTreatments.length > 0,
    recentScores: [...plant.scoreHistory].reverse().map((s) => s.score),
    isDormant: isSeasonallyDormant(plant.dormancyMonths),
  });

  const checkIn = await prisma.$transaction(async (tx) => {
    const created = await tx.checkIn.create({
      data: {
        plantId,
        photoUrl,
        engineOutputJson: engineOutput as unknown as object,
        computedScore: finalScore,
        subscoreBreakdownJson: breakdown as unknown as object,
        diagnosticFlags: {
          create: engineOutput.flags.map((f) => {
            const plan = buildTreatmentPlan(f.condition);
            return {
              condition: f.condition,
              confidence: f.confidence,
              severity: f.severity,
              urgency: f.urgency,
              trend: f.trend,
              treatmentPlan: { create: { steps: plan.steps, productsRecommended: plan.productsRecommended } },
            };
          }),
        },
      },
      include: { diagnosticFlags: { include: { treatmentPlan: true } } },
    });

    await tx.plant.update({
      where: { id: plantId },
      data: { scoreCurrent: finalScore },
    });

    await tx.plantScoreSnapshot.create({
      data: { plantId, score: finalScore },
    });

    return created;
  });

  await recomputeGardenScore(plant.gardenId);

  res.status(201).json({ checkIn, breakdown, priorScore: lastCheckIn?.computedScore ?? null });
});

async function recomputeGardenScore(gardenId: string) {
  const plants = await prisma.plant.findMany({
    where: { gardenId, active: true },
    include: { checkIns: { orderBy: { timestamp: "desc" }, take: 1 } },
  });

  const inputs = plants.map((p) => {
    const lastActivityAt = p.checkIns[0]?.timestamp ?? p.createdAt;
    return {
      score: p.scoreCurrent,
      importanceWeight: p.importanceWeight,
      speciesId: p.speciesId,
      isOverdueForCheckin: isOverdueForCheckin(lastActivityAt, p.checkinCadenceDays),
    };
  });

  const gardenScore = computeGardenScore(inputs);

  await prisma.$transaction([
    prisma.garden.update({ where: { id: gardenId }, data: { scoreCurrent: gardenScore } }),
    prisma.gardenScoreSnapshot.create({ data: { gardenId, score: gardenScore } }),
  ]);
}
