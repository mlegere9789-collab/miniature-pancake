import { MAX_STAGING_SCORE, scoreToStage } from '@/constants/staging';

describe('staging scoreToStage', () => {
  it('maps a no-period-in-12-months answer straight to postmenopause', () => {
    const answers = { cycle_regularity: 4, vasomotor: 0, age_band: 0 };
    expect(scoreToStage(4, answers)).toBe('postmenopause');
  });

  it('classifies a low score as premenopause', () => {
    const answers = { cycle_regularity: 0, vasomotor: 0, age_band: 1 };
    expect(scoreToStage(1, answers)).toBe('premenopause');
  });

  it('classifies a mid score as early perimenopause', () => {
    const answers = { cycle_regularity: 1, vasomotor: 1, age_band: 2 };
    expect(scoreToStage(4, answers)).toBe('perimenopause_early');
  });

  it('classifies a high score as late perimenopause', () => {
    const answers = { cycle_regularity: 2, vasomotor: 2, age_band: 3 };
    expect(scoreToStage(7, answers)).toBe('perimenopause_late');
  });

  it('has a sane maximum score', () => {
    expect(MAX_STAGING_SCORE).toBeGreaterThan(0);
  });
});
