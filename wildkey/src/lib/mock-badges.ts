export type Badge = {
  slug: string;
  label: string;
  description: string;
  earned: boolean;
};

export const MOCK_BADGES: Badge[] = [
  { slug: "first-id", label: "First ID", description: "Identify your first organism.", earned: true },
  { slug: "week-streak", label: "7-Day Streak", description: "Log an observation 7 days in a row.", earned: true },
  { slug: "five-groups", label: "All-Rounder", description: "Identify species from 5 different taxon groups.", earned: false },
  { slug: "hundred-obs", label: "Century", description: "Log 100 observations.", earned: false },
  { slug: "night-owl", label: "Night Owl", description: "Log an observation after dark.", earned: true },
  { slug: "first-help", label: "Helping Hand", description: "Confirm your first community ID.", earned: false },
];
