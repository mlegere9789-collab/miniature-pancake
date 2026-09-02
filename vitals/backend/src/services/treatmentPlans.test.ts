import { describe, expect, it } from "vitest";
import { buildTreatmentPlan } from "./treatmentPlans";

describe("buildTreatmentPlan", () => {
  it("returns curated steps and products for a known condition", () => {
    const plan = buildTreatmentPlan("leaf-spot");
    expect(plan.steps.length).toBeGreaterThan(0);
    expect(plan.productsRecommended).toContain("Copper fungicide spray");
  });

  it("returns different curated content for a different known condition", () => {
    const plan = buildTreatmentPlan("water-stress");
    expect(plan.steps.length).toBeGreaterThan(0);
    expect(plan.productsRecommended).toContain("Mulch");
  });

  it("falls back to a generic monitoring plan for an unrecognized condition", () => {
    const plan = buildTreatmentPlan("some-unknown-condition");
    expect(plan.steps.length).toBeGreaterThan(0);
    expect(plan.productsRecommended).toEqual([]);
  });
});
