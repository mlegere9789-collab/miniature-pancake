import { describe, expect, it } from "vitest";
import { computeRisingStars } from "./dashboard";

describe("computeRisingStars", () => {
  it("excludes plants with fewer than 2 snapshots", () => {
    const result = computeRisingStars([{ plantId: "1", recentScores: [90] }]);
    expect(result).toEqual([]);
  });

  it("excludes plants that are flat or declining", () => {
    const result = computeRisingStars([
      { plantId: "flat", recentScores: [80, 80, 80] },
      { plantId: "declining", recentScores: [70, 75, 80] }, // most recent (70) < oldest (80)
    ]);
    expect(result).toEqual([]);
  });

  it("includes and ranks plants trending up by delta, descending", () => {
    const result = computeRisingStars([
      { plantId: "small-gain", recentScores: [85, 80] }, // +5
      { plantId: "big-gain", recentScores: [90, 60] }, // +30
    ]);
    expect(result.map((r) => r.plantId)).toEqual(["big-gain", "small-gain"]);
    expect(result[0].delta).toBe(30);
    expect(result[1].delta).toBe(5);
  });

  it("caps results at the given limit", () => {
    const histories = Array.from({ length: 8 }, (_, i) => ({
      plantId: String(i),
      recentScores: [50 + i, 50],
    }));
    const result = computeRisingStars(histories, 5);
    expect(result).toHaveLength(5);
    // highest deltas kept
    expect(result[0].plantId).toBe("7");
  });
});
