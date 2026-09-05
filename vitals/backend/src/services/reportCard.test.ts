import { describe, expect, it } from "vitest";
import { computeWeeklyReportCard } from "./reportCard";

const periodStart = new Date("2026-08-22T00:00:00Z");
const periodEnd = new Date("2026-08-29T00:00:00Z");

describe("computeWeeklyReportCard", () => {
  it("reports a no-activity week", () => {
    const card = computeWeeklyReportCard({
      periodStart,
      periodEnd,
      gardenScoreStart: 80,
      gardenScoreEnd: 80,
      plantSummaries: [],
      checkInsCompleted: 0,
    });
    expect(card.headline).toMatch(/no check-ins/i);
    expect(card.gardenScoreDelta).toBe(0);
  });

  it("surfaces the top-improving plant in the headline on an up week", () => {
    const card = computeWeeklyReportCard({
      periodStart,
      periodEnd,
      gardenScoreStart: 70,
      gardenScoreEnd: 82,
      plantSummaries: [
        { plantId: "1", name: "Tomato", scoreStart: 60, scoreEnd: 85 },
        { plantId: "2", name: "Basil", scoreStart: 90, scoreEnd: 88 },
      ],
      checkInsCompleted: 4,
    });
    expect(card.gardenScoreDelta).toBe(12);
    expect(card.topPlants[0].name).toBe("Tomato");
    expect(card.headline).toContain("Tomato");
    expect(card.plantsNeedingAttention.map((p) => p.plantId)).toContain("2");
  });

  it("points at Needs Attention on a down week", () => {
    const card = computeWeeklyReportCard({
      periodStart,
      periodEnd,
      gardenScoreStart: 85,
      gardenScoreEnd: 74,
      plantSummaries: [{ plantId: "1", name: "Rose", scoreStart: 90, scoreEnd: 55 }],
      checkInsCompleted: 2,
    });
    expect(card.gardenScoreDelta).toBe(-11);
    expect(card.headline).toMatch(/down 11 pts/i);
    expect(card.plantsNeedingAttention[0].plantId).toBe("1");
  });

  it("caps top plants and attention lists at 3", () => {
    const plantSummaries = Array.from({ length: 5 }, (_, i) => ({
      plantId: String(i),
      name: `Plant ${i}`,
      scoreStart: 50,
      scoreEnd: 50 + i * 5,
    }));
    const card = computeWeeklyReportCard({
      periodStart,
      periodEnd,
      gardenScoreStart: 50,
      gardenScoreEnd: 65,
      plantSummaries,
      checkInsCompleted: 5,
    });
    expect(card.topPlants.length).toBeLessThanOrEqual(3);
  });
});
