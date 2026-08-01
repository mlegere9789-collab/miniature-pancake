/**
 * AfterNoon design tokens.
 *
 * A warm, calm "late-afternoon" palette suited to a privacy-first
 * perimenopause/menopause companion. These are placeholder-quality tokens that
 * intentionally mirror the structure of the Claude Design prototype (warm
 * neutrals + a terracotta accent + a muted sage support color) so the exact
 * hex values can be swapped to match the prototype without touching screens.
 */

export const colors = {
  // Surfaces
  background: '#FBF6F0',
  surface: '#FFFFFF',
  surfaceMuted: '#F3EAE0',

  // Text
  text: '#2E2A26',
  textMuted: '#6B625A',
  textInverse: '#FFFFFF',

  // Brand accent (terracotta / dusk)
  accent: '#C2694B',
  accentSoft: '#E9C4B5',
  accentPressed: '#A8543A',

  // Support (sage)
  support: '#6E8A73',
  supportSoft: '#D6E1D5',

  // Severity scale (1 = mild -> 5 = severe)
  severity: ['#DCE7DA', '#E7E0C4', '#F1D2A6', '#E9B089', '#D9795E'] as const,

  // Feedback
  border: '#E7DCCF',
  danger: '#B84A3C',
  success: '#4F7A54',

  // Privacy / trust cues
  trust: '#4A6B7A',
  trustSoft: '#DCE7EC',
} as const;

export const spacing = {
  xs: 4,
  sm: 8,
  md: 16,
  lg: 24,
  xl: 32,
  xxl: 48,
} as const;

export const radius = {
  sm: 8,
  md: 12,
  lg: 20,
  pill: 999,
} as const;

export const typography = {
  display: { fontSize: 30, fontWeight: '700' as const, lineHeight: 36 },
  title: { fontSize: 22, fontWeight: '700' as const, lineHeight: 28 },
  heading: { fontSize: 18, fontWeight: '600' as const, lineHeight: 24 },
  body: { fontSize: 16, fontWeight: '400' as const, lineHeight: 22 },
  label: { fontSize: 14, fontWeight: '600' as const, lineHeight: 18 },
  caption: { fontSize: 13, fontWeight: '400' as const, lineHeight: 18 },
} as const;

export const theme = { colors, spacing, radius, typography };
export type Theme = typeof theme;
