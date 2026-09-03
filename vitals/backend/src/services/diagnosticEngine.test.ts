import { describe, expect, it } from "vitest";
import { runDiagnosticEngine } from "./diagnosticEngine";

describe("runDiagnosticEngine", () => {
  it("is deterministic for the same photo URL", async () => {
    const a = await runDiagnosticEngine("/uploads/photo-123.jpg");
    const b = await runDiagnosticEngine("/uploads/photo-123.jpg");
    expect(a).toEqual(b);
  });

  it("produces different output for different photo URLs", async () => {
    const a = await runDiagnosticEngine("/uploads/photo-a.jpg");
    const b = await runDiagnosticEngine("/uploads/photo-b.jpg");
    expect(a).not.toEqual(b);
  });

  it("keeps every score within [0, 100]", async () => {
    for (const url of ["/a.jpg", "/b.jpg", "/c.jpg", "/d.jpg", "/e.jpg"]) {
      const output = await runDiagnosticEngine(url);
      expect(output.visualVitality.score).toBeGreaterThanOrEqual(0);
      expect(output.visualVitality.score).toBeLessThanOrEqual(100);
      expect(output.environmentalFit.score).toBeGreaterThanOrEqual(0);
      expect(output.environmentalFit.score).toBeLessThanOrEqual(100);
    }
  });

  it("flags leaf-spot only when necrotic coverage exceeds the threshold", async () => {
    const output = await runDiagnosticEngine("/uploads/photo-123.jpg");
    const hasLeafSpotFlag = output.flags.some((f) => f.condition === "leaf-spot");
    expect(hasLeafSpotFlag).toBe(output.visualVitality.necroticCoveragePct > 6);
  });

  it("flags water-stress only when wilting is detected", async () => {
    const output = await runDiagnosticEngine("/uploads/photo-123.jpg");
    const hasWaterStressFlag = output.flags.some((f) => f.condition === "water-stress");
    expect(hasWaterStressFlag).toBe(output.visualVitality.wiltingDetected);
  });

  it("never returns frostRiskDetected true (that's derived from real weather data, not this stub)", async () => {
    const output = await runDiagnosticEngine("/uploads/photo-123.jpg");
    expect(output.environmentalFit.frostRiskDetected).toBe(false);
  });
});
