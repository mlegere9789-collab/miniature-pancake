import { scoreColor, theme } from "./theme";

describe("scoreColor", () => {
  it("returns the healthy green for a high score", () => {
    expect(scoreColor(80)).toBe(theme.color.forestGreenLight);
    expect(scoreColor(100)).toBe(theme.color.forestGreenLight);
  });

  it("returns the caution gold for a mid-range score", () => {
    expect(scoreColor(55)).toBe(theme.color.gold);
    expect(scoreColor(79)).toBe(theme.color.gold);
  });

  it("returns the danger red for a low score", () => {
    expect(scoreColor(54)).toBe(theme.color.danger);
    expect(scoreColor(0)).toBe(theme.color.danger);
  });
});
