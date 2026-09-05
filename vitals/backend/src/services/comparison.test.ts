import { describe, expect, it } from "vitest";
import { computeLeaderboardRank, computeTwinComparison } from "./comparison";

describe("computeTwinComparison", () => {
  it("returns a neutral default with no cohort data", () => {
    const result = computeTwinComparison(80, []);
    expect(result.percentile).toBe(50);
    expect(result.cohortSize).toBe(0);
    expect(result.message).toMatch(/not enough/i);
  });

  it("computes the percentile of plants scoring lower", () => {
    // 80 beats 3 of the 5 cohort scores (60, 70, 75) => 60th percentile
    const result = computeTwinComparison(80, [60, 70, 75, 85, 90]);
    expect(result.percentile).toBe(60);
    expect(result.cohortSize).toBe(5);
    expect(result.message).toContain("60%");
  });

  it("scores at the top of the cohort as the 100th percentile", () => {
    const result = computeTwinComparison(100, [50, 60, 70]);
    expect(result.percentile).toBe(100);
  });

  it("scores at the bottom of the cohort as the 0th percentile", () => {
    const result = computeTwinComparison(10, [50, 60, 70]);
    expect(result.percentile).toBe(0);
  });
});

describe("computeLeaderboardRank", () => {
  it("ranks 1st when the highest score among participants", () => {
    const result = computeLeaderboardRank(95, [80, 70, 60]);
    expect(result.rank).toBe(1);
    expect(result.totalParticipants).toBe(4);
  });

  it("ranks last when the lowest score among participants", () => {
    const result = computeLeaderboardRank(10, [80, 70, 60]);
    expect(result.rank).toBe(4);
    expect(result.totalParticipants).toBe(4);
  });

  it("handles a solo participant as rank 1 of 1", () => {
    const result = computeLeaderboardRank(75, []);
    expect(result.rank).toBe(1);
    expect(result.totalParticipants).toBe(1);
    expect(result.percentile).toBe(50);
  });
});
