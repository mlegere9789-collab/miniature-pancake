import { describe, expect, it } from "vitest";
import { applyWeatherPenalty, WeatherSignals } from "./weatherService";

function signals(overrides: Partial<WeatherSignals> = {}): WeatherSignals {
  return {
    frostRiskTonight: false,
    minTempTonightC: 15,
    droughtStressDetected: false,
    precipLast7DaysMm: 20,
    ...overrides,
  };
}

describe("applyWeatherPenalty", () => {
  it("leaves the score unchanged with no weather risk", () => {
    expect(applyWeatherPenalty(90, signals(), false)).toBe(90);
  });

  it("penalizes frost risk only for frost-sensitive plants", () => {
    const frostNight = signals({ frostRiskTonight: true });
    expect(applyWeatherPenalty(90, frostNight, true)).toBe(70);
    expect(applyWeatherPenalty(90, frostNight, false)).toBe(90);
  });

  it("penalizes drought stress regardless of frost sensitivity", () => {
    const dry = signals({ droughtStressDetected: true });
    expect(applyWeatherPenalty(90, dry, false)).toBe(80);
  });

  it("clamps the result to [0, 100]", () => {
    const harsh = signals({ frostRiskTonight: true, droughtStressDetected: true });
    expect(applyWeatherPenalty(10, harsh, true)).toBe(0);
  });
});
