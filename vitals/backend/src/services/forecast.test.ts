import { describe, expect, it } from "vitest";
import { computeScoreForecast } from "./forecast";

function daysAgo(n: number): Date {
  return new Date(Date.now() - n * 24 * 60 * 60 * 1000);
}

describe("computeScoreForecast", () => {
  it("returns null with fewer than 3 snapshots", () => {
    expect(computeScoreForecast([{ score: 80, computedAt: daysAgo(1) }])).toBeNull();
    expect(
      computeScoreForecast([
        { score: 80, computedAt: daysAgo(2) },
        { score: 82, computedAt: daysAgo(1) },
      ]),
    ).toBeNull();
  });

  it("projects a declining trend downward and flags approaching the attention threshold", () => {
    const forecast = computeScoreForecast(
      [
        { score: 90, computedAt: daysAgo(20) },
        { score: 75, computedAt: daysAgo(10) },
        { score: 60, computedAt: daysAgo(0) },
      ],
      14,
    );
    expect(forecast).not.toBeNull();
    expect(forecast!.trend).toBe("declining");
    expect(forecast!.projectedScore).toBeLessThan(70);
    expect(forecast!.approachingAttentionThreshold).toBe(true);
  });

  it("projects an improving trend upward", () => {
    const forecast = computeScoreForecast([
      { score: 50, computedAt: daysAgo(20) },
      { score: 60, computedAt: daysAgo(10) },
      { score: 70, computedAt: daysAgo(0) },
    ]);
    expect(forecast!.trend).toBe("improving");
    expect(forecast!.projectedScore).toBeGreaterThan(70);
    expect(forecast!.approachingAttentionThreshold).toBe(false);
  });

  it("reports a flat trend for a stable score history", () => {
    const forecast = computeScoreForecast([
      { score: 80, computedAt: daysAgo(20) },
      { score: 80, computedAt: daysAgo(10) },
      { score: 80, computedAt: daysAgo(0) },
    ]);
    expect(forecast!.trend).toBe("flat");
    expect(forecast!.projectedScore).toBe(80);
  });

  it("does not flag approaching the threshold when already below it", () => {
    const forecast = computeScoreForecast([
      { score: 50, computedAt: daysAgo(20) },
      { score: 45, computedAt: daysAgo(10) },
      { score: 40, computedAt: daysAgo(0) },
    ]);
    expect(forecast!.approachingAttentionThreshold).toBe(false);
  });

  it("clamps the projected score to [0, 100]", () => {
    const forecast = computeScoreForecast(
      [
        { score: 20, computedAt: daysAgo(20) },
        { score: 10, computedAt: daysAgo(10) },
        { score: 2, computedAt: daysAgo(0) },
      ],
      60,
    );
    expect(forecast!.projectedScore).toBe(0);
  });
});
