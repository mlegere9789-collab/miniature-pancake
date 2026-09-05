import { DiagnosticEngineOutput } from "../types/diagnosticEngine";

/**
 * Stand-in for the shared Plant ID & Diagnostic Engine (Idea 5), which is
 * built as a separate service. This stub applies simple deterministic
 * heuristics so the scoring pipeline and API can be built, tested, and
 * demoed end-to-end today. Swap the body of `runDiagnosticEngine` for a
 * real API call once that engine exists — the contract (DiagnosticEngineOutput)
 * is designed to match its expected response shape.
 */
export async function runDiagnosticEngine(
  photoUrl: string,
): Promise<DiagnosticEngineOutput> {
  // Deterministic pseudo-randomness seeded from the photo URL so repeated
  // calls in tests/demos are stable, without pretending this is real CV.
  const seed = hashString(photoUrl);
  const rand = mulberry32(seed);

  const chlorosisPct = Math.round(rand() * 15);
  const necroticCoveragePct = Math.round(rand() * 10);
  const wiltingDetected = rand() < 0.1;
  const newGrowthDetected = rand() > 0.5;
  const growthDensityDeltaPct = Math.round((rand() - 0.4) * 20);

  const visualScore = clamp(
    100 - chlorosisPct * 2 - necroticCoveragePct * 3 - (wiltingDetected ? 15 : 0) + (newGrowthDetected ? 5 : 0),
    0,
    100,
  );

  const flags = [];
  if (necroticCoveragePct > 6) {
    flags.push({
      condition: "leaf-spot",
      confidence: 0.72,
      severity: "MODERATE" as const,
      urgency: "THIS_WEEK" as const,
      trend: "NEW" as const,
    });
  }
  if (wiltingDetected) {
    flags.push({
      condition: "water-stress",
      confidence: 0.65,
      severity: "URGENT" as const,
      urgency: "TREAT_TODAY" as const,
      trend: "NEW" as const,
    });
  }

  const environmentalScore = clamp(90 - Math.round(rand() * 20), 0, 100);

  return {
    visualVitality: {
      score: visualScore,
      chlorosisPct,
      necroticCoveragePct,
      wiltingDetected,
      newGrowthDetected,
      growthDensityDeltaPct,
    },
    flags,
    environmentalFit: {
      score: environmentalScore,
      lightExposureMatch: rand() > 0.2,
      droughtStressDetected: rand() < 0.15,
      frostRiskDetected: false,
    },
  };
}

function clamp(n: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, n));
}

function hashString(s: string): number {
  let h = 0;
  for (let i = 0; i < s.length; i++) {
    h = (h << 5) - h + s.charCodeAt(i);
    h |= 0;
  }
  return h >>> 0;
}

function mulberry32(a: number) {
  return function () {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
