import { describe, expect, it } from "vitest";
import { computeGardenScore, computePlantScore } from "./scoring";
import { DiagnosticEngineOutput } from "../types/diagnosticEngine";

function healthyEngineOutput(): DiagnosticEngineOutput {
  return {
    visualVitality: {
      score: 95,
      chlorosisPct: 0,
      necroticCoveragePct: 0,
      wiltingDetected: false,
      newGrowthDetected: true,
      growthDensityDeltaPct: 5,
    },
    flags: [],
    environmentalFit: { score: 90, lightExposureMatch: true, droughtStressDetected: false, frostRiskDetected: false },
  };
}

describe("computePlantScore", () => {
  it("scores a healthy, on-time plant near 90+", () => {
    const { finalScore } = computePlantScore({
      engineOutput: healthyEngineOutput(),
      daysLateForCheckin: 0,
      outstandingTreatmentsIgnored: false,
      recentScores: [],
    });
    expect(finalScore).toBeGreaterThanOrEqual(85);
  });

  it("penalizes an urgent, worsening diagnostic flag heavily", () => {
    const engineOutput = healthyEngineOutput();
    engineOutput.flags.push({
      condition: "root-rot",
      confidence: 0.9,
      severity: "URGENT",
      urgency: "TREAT_TODAY",
      trend: "WORSENING",
    });
    const { finalScore, breakdown } = computePlantScore({
      engineOutput,
      daysLateForCheckin: 0,
      outstandingTreatmentsIgnored: false,
      recentScores: [],
    });
    expect(breakdown.diagnosticFlags).toBeLessThan(70);
    expect(finalScore).toBeLessThan(85);
  });

  it("applies a negative trend momentum modifier when score is declining", () => {
    const { breakdown } = computePlantScore({
      engineOutput: healthyEngineOutput(),
      daysLateForCheckin: 0,
      outstandingTreatmentsIgnored: false,
      recentScores: [95, 85], // was trending down before this check-in
    });
    // base score for a healthy plant is high, but prior scores were much higher
    expect(breakdown.trendMomentum).toBeLessThanOrEqual(0);
  });

  it("penalizes late check-ins and ignored treatments via care consistency", () => {
    const onTime = computePlantScore({
      engineOutput: healthyEngineOutput(),
      daysLateForCheckin: 0,
      outstandingTreatmentsIgnored: false,
      recentScores: [],
    });
    const late = computePlantScore({
      engineOutput: healthyEngineOutput(),
      daysLateForCheckin: 10,
      outstandingTreatmentsIgnored: true,
      recentScores: [],
    });
    expect(late.breakdown.careConsistency).toBeLessThan(onTime.breakdown.careConsistency);
  });

  it("clamps the final score to [0, 100]", () => {
    const engineOutput: DiagnosticEngineOutput = {
      visualVitality: {
        score: 0,
        chlorosisPct: 100,
        necroticCoveragePct: 100,
        wiltingDetected: true,
        newGrowthDetected: false,
        growthDensityDeltaPct: -50,
      },
      flags: [
        { condition: "blight", confidence: 1, severity: "URGENT", urgency: "TREAT_TODAY", trend: "WORSENING" },
      ],
      environmentalFit: { score: 0, lightExposureMatch: false, droughtStressDetected: true, frostRiskDetected: true },
    };
    const { finalScore } = computePlantScore({
      engineOutput,
      daysLateForCheckin: 60,
      outstandingTreatmentsIgnored: true,
      recentScores: [90, 80, 70],
    });
    expect(finalScore).toBeGreaterThanOrEqual(0);
    expect(finalScore).toBeLessThanOrEqual(100);
  });
});

describe("computeGardenScore", () => {
  it("returns 100 for an empty garden", () => {
    expect(computeGardenScore([])).toBe(100);
  });

  it("weights higher-importance plants more heavily", () => {
    const score = computeGardenScore([
      { score: 100, importanceWeight: 10, speciesId: "oak", isOverdueForCheckin: false },
      { score: 0, importanceWeight: 1, speciesId: "annual", isOverdueForCheckin: false },
    ]);
    expect(score).toBeGreaterThan(70);
  });

  it("applies a neglect penalty for overdue plants", () => {
    const upToDate = computeGardenScore([
      { score: 80, importanceWeight: 1, speciesId: "a", isOverdueForCheckin: false },
    ]);
    const overdue = computeGardenScore([
      { score: 80, importanceWeight: 1, speciesId: "a", isOverdueForCheckin: true },
    ]);
    expect(overdue).toBeLessThan(upToDate);
  });

  it("applies a diversity bonus for varied species", () => {
    const monoculture = computeGardenScore([
      { score: 80, importanceWeight: 1, speciesId: "tomato", isOverdueForCheckin: false },
      { score: 80, importanceWeight: 1, speciesId: "tomato", isOverdueForCheckin: false },
    ]);
    const diverse = computeGardenScore([
      { score: 80, importanceWeight: 1, speciesId: "tomato", isOverdueForCheckin: false },
      { score: 80, importanceWeight: 1, speciesId: "basil", isOverdueForCheckin: false },
    ]);
    expect(diverse).toBeGreaterThan(monoculture);
  });
});
