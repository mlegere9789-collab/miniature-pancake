/**
 * Staging self-assessment.
 *
 * This is an educational, non-diagnostic questionnaire loosely modelled on the
 * kind of stage buckets used in menopause self-assessment (premenopause ->
 * perimenopause -> postmenopause). It is NOT a clinical instrument; copy and
 * scoring should be reviewed against the product spec before release. The
 * result is stored so onboarding never has to be repeated and so the Doctor
 * Report can include the user's self-identified stage as context.
 */

export type MenopauseStage =
  | 'premenopause'
  | 'perimenopause_early'
  | 'perimenopause_late'
  | 'postmenopause'
  | 'unsure';

export interface StageInfo {
  stage: MenopauseStage;
  title: string;
  description: string;
}

export const STAGE_INFO: Record<MenopauseStage, StageInfo> = {
  premenopause: {
    stage: 'premenopause',
    title: 'Premenopause',
    description: 'Regular cycles with few or no menopause-related symptoms.',
  },
  perimenopause_early: {
    stage: 'perimenopause_early',
    title: 'Early perimenopause',
    description: 'Cycles are becoming less predictable and symptoms are appearing.',
  },
  perimenopause_late: {
    stage: 'perimenopause_late',
    title: 'Late perimenopause',
    description: 'Cycles are often skipped and symptoms are more noticeable.',
  },
  postmenopause: {
    stage: 'postmenopause',
    title: 'Postmenopause',
    description: 'It has been 12+ months since your last period.',
  },
  unsure: {
    stage: 'unsure',
    title: 'Still figuring it out',
    description: "That's completely okay — tracking will help clarify the picture.",
  },
};

export interface StagingQuestion {
  id: string;
  prompt: string;
  options: { label: string; value: number }[];
}

/**
 * Each answer contributes points; the total maps to a stage bucket in
 * `scoreToStage`. Deliberately small so it can be tuned against the spec.
 */
export const STAGING_QUESTIONS: readonly StagingQuestion[] = [
  {
    id: 'cycle_regularity',
    prompt: 'Over the last year, how would you describe your periods?',
    options: [
      { label: 'Regular and predictable', value: 0 },
      { label: 'Somewhat irregular', value: 1 },
      { label: 'Often skipped or very unpredictable', value: 2 },
      { label: "I haven't had a period in 12+ months", value: 4 },
    ],
  },
  {
    id: 'vasomotor',
    prompt: 'How often do you get hot flashes or night sweats?',
    options: [
      { label: 'Never', value: 0 },
      { label: 'Occasionally', value: 1 },
      { label: 'Most weeks', value: 2 },
    ],
  },
  {
    id: 'age_band',
    prompt: 'Which age range are you in?',
    options: [
      { label: 'Under 40', value: 0 },
      { label: '40–45', value: 1 },
      { label: '46–52', value: 2 },
      { label: 'Over 52', value: 3 },
    ],
  },
] as const;

export const MAX_STAGING_SCORE = STAGING_QUESTIONS.reduce(
  (sum, q) => sum + Math.max(...q.options.map((o) => o.value)),
  0,
);

/**
 * Maps a raw score to a stage. If the "no period in 12+ months" answer was
 * chosen it dominates -> postmenopause, regardless of the rest.
 */
export function scoreToStage(score: number, answers: Record<string, number>): MenopauseStage {
  if (answers.cycle_regularity === 4) return 'postmenopause';
  if (score <= 1) return 'premenopause';
  if (score <= 4) return 'perimenopause_early';
  return 'perimenopause_late';
}
