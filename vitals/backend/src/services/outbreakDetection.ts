/**
 * Regional outbreak detection (spec §4.3/§4.5, "Idea 4"). A dedicated
 * regional-outbreak feed (the "Blight Watch" companion app) is out of
 * scope here, so this treats Vitals' own diagnostic flags as the signal:
 * when the same condition is independently reported by several different
 * gardens in the same USDA zone within a short window, that's an outbreak
 * worth surfacing — e.g. "Powdery mildew reported in 4 nearby gardens this
 * week." Only a condition name and a garden count are ever exposed, never
 * which gardens reported it.
 */
export interface OutbreakReport {
  gardenId: string;
  condition: string;
}

export interface OutbreakAlert {
  condition: string;
  gardenCount: number;
}

const MIN_GARDENS_FOR_OUTBREAK = 3;

export function detectOutbreaks(reports: OutbreakReport[], minGardens = MIN_GARDENS_FOR_OUTBREAK): OutbreakAlert[] {
  const gardensByCondition = new Map<string, Set<string>>();
  for (const { gardenId, condition } of reports) {
    if (!gardensByCondition.has(condition)) gardensByCondition.set(condition, new Set());
    gardensByCondition.get(condition)!.add(gardenId);
  }

  const alerts: OutbreakAlert[] = [];
  for (const [condition, gardens] of gardensByCondition) {
    if (gardens.size >= minGardens) {
      alerts.push({ condition, gardenCount: gardens.size });
    }
  }

  return alerts.sort((a, b) => b.gardenCount - a.gardenCount);
}
