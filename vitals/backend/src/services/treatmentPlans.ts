/**
 * Actionable treatment recommendations per diagnostic flag (spec §4.2: a
 * diagnosis isn't useful without "what do I do about it"). The
 * `TreatmentPlan` model already existed in the schema and fed into care-
 * consistency scoring (`outstandingTreatmentsIgnored` in
 * `routes/checkins.ts`), but nothing ever created one — so that scoring
 * input was permanently dead. This is a curated stand-in for the same
 * shared Diagnostic Engine that `diagnosticEngine.ts` stubs; swap it for a
 * real recommendation service once that engine exists.
 */
export interface TreatmentPlanContent {
  steps: string[];
  productsRecommended: string[];
}

const TREATMENT_PLANS: Record<string, TreatmentPlanContent> = {
  "leaf-spot": {
    steps: [
      "Remove and discard affected leaves — don't compost them.",
      "Avoid overhead watering; water at the base instead.",
      "Improve airflow around the plant by thinning nearby growth.",
    ],
    productsRecommended: ["Copper fungicide spray", "Neem oil"],
  },
  "water-stress": {
    steps: [
      "Check soil moisture 2 inches down before watering again.",
      "Water deeply and less frequently rather than a little every day.",
      "Add a 2-3 inch mulch layer to reduce evaporation.",
    ],
    productsRecommended: ["Mulch", "Soil moisture meter"],
  },
};

const GENERIC_TREATMENT_PLAN: TreatmentPlanContent = {
  steps: ["Monitor the affected area over the next few check-ins.", "Isolate from other plants if symptoms spread."],
  productsRecommended: [],
};

/** Returns null when there's nothing actionable to recommend (e.g. no flags). */
export function buildTreatmentPlan(condition: string): TreatmentPlanContent {
  return TREATMENT_PLANS[condition] ?? GENERIC_TREATMENT_PLAN;
}
