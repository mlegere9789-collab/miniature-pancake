// Vitals design tokens (spec §6): Oura/Apple Health-inspired — generous
// whitespace, one dominant number per screen, restrained palette.
export const theme = {
  color: {
    forestGreen: "#1B3A2B",
    forestGreenLight: "#2F5B41",
    soilBrown: "#4A3728",
    cream: "#FAF7F0",
    gold: "#C9A24B",
    danger: "#B23A2E",
    textPrimary: "#1B2420",
    textSecondary: "#6B7268",
    border: "#E4E0D6",
  },
  spacing: (n: number) => n * 8,
  radius: {
    sm: 8,
    md: 16,
    lg: 28,
  },
  font: {
    heroSize: 72,
    titleSize: 22,
    bodySize: 16,
    captionSize: 13,
  },
} as const;

export function scoreColor(score: number): string {
  if (score >= 80) return theme.color.forestGreenLight;
  if (score >= 55) return theme.color.gold;
  return theme.color.danger;
}
