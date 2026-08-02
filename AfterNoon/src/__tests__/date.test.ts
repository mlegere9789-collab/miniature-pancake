import { dateKeyRange, toLocalDateKey } from '@/utils/date';

describe('date utils', () => {
  it('builds an inclusive range of day keys', () => {
    const keys = dateKeyRange('2026-06-01', '2026-06-03');
    expect(keys).toEqual(['2026-06-01', '2026-06-02', '2026-06-03']);
  });

  it('returns a single key when start === end', () => {
    expect(dateKeyRange('2026-06-01', '2026-06-01')).toEqual(['2026-06-01']);
  });

  it('formats a local date key as YYYY-MM-DD', () => {
    expect(toLocalDateKey('2026-06-05T13:00:00.000Z')).toMatch(/^\d{4}-\d{2}-\d{2}$/);
  });
});
