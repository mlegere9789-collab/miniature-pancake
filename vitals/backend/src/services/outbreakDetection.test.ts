import { describe, expect, it } from "vitest";
import { detectOutbreaks } from "./outbreakDetection";

describe("detectOutbreaks", () => {
  it("flags a condition reported by at least the minimum number of distinct gardens", () => {
    const alerts = detectOutbreaks([
      { gardenId: "a", condition: "powdery-mildew" },
      { gardenId: "b", condition: "powdery-mildew" },
      { gardenId: "c", condition: "powdery-mildew" },
    ]);
    expect(alerts).toEqual([{ condition: "powdery-mildew", gardenCount: 3 }]);
  });

  it("does not flag a condition below the minimum garden count", () => {
    const alerts = detectOutbreaks([
      { gardenId: "a", condition: "aphids" },
      { gardenId: "b", condition: "aphids" },
    ]);
    expect(alerts).toEqual([]);
  });

  it("counts distinct gardens, not raw report count", () => {
    const alerts = detectOutbreaks([
      { gardenId: "a", condition: "aphids" },
      { gardenId: "a", condition: "aphids" },
      { gardenId: "a", condition: "aphids" },
    ]);
    expect(alerts).toEqual([]);
  });

  it("sorts multiple outbreaks by garden count descending", () => {
    const alerts = detectOutbreaks([
      { gardenId: "a", condition: "aphids" },
      { gardenId: "b", condition: "aphids" },
      { gardenId: "c", condition: "aphids" },
      { gardenId: "a", condition: "blight" },
      { gardenId: "b", condition: "blight" },
      { gardenId: "c", condition: "blight" },
      { gardenId: "d", condition: "blight" },
    ]);
    expect(alerts).toEqual([
      { condition: "blight", gardenCount: 4 },
      { condition: "aphids", gardenCount: 3 },
    ]);
  });

  it("respects a custom minimum garden threshold", () => {
    const alerts = detectOutbreaks(
      [
        { gardenId: "a", condition: "aphids" },
        { gardenId: "b", condition: "aphids" },
      ],
      2,
    );
    expect(alerts).toEqual([{ condition: "aphids", gardenCount: 2 }]);
  });
});
