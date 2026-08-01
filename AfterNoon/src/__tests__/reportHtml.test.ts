import { buildReportHtml } from '@/features/reports/reportHtml';
import type { ReportData } from '@/data/reportData';

function baseData(overrides: Partial<ReportData> = {}): ReportData {
  return {
    startIso: '2026-06-01T00:00:00.000Z',
    endIso: '2026-06-30T23:59:59.999Z',
    startDateKey: '2026-06-01',
    endDateKey: '2026-06-30',
    generatedAt: '2026-07-01T09:00:00.000Z',
    staging: null,
    stagingLabel: null,
    logs: [],
    cycleNotes: [],
    daily: [],
    categorySummaries: [],
    totalLogs: 0,
    ...overrides,
  };
}

describe('buildReportHtml', () => {
  it('produces a self-contained HTML document with no external asset references', () => {
    const html = buildReportHtml(baseData());
    expect(html).toContain('<!DOCTYPE html>');
    expect(html).toContain('AfterNoon');
    // No network egress from the printed document.
    expect(html).not.toMatch(/https?:\/\//);
    expect(html).not.toContain('<script');
  });

  it('notes when there is no data in the range', () => {
    const html = buildReportHtml(baseData());
    expect(html).toContain('No symptoms were logged in this date range.');
  });

  it('renders a summary row per category and escapes user notes', () => {
    const html = buildReportHtml(
      baseData({
        totalLogs: 3,
        categorySummaries: [
          {
            category: 'hot_flashes',
            label: 'Hot flashes',
            emoji: '🔥',
            daysLogged: 2,
            totalLogs: 3,
            avgSeverity: 3.5,
            maxSeverity: 5,
          },
        ],
        cycleNotes: [
          {
            id: 1,
            noteDate: '2026-06-10',
            flow: 'light',
            note: '<script>alert(1)</script>',
            createdAt: '2026-06-10T00:00:00.000Z',
          },
        ],
      }),
    );
    expect(html).toContain('Hot flashes');
    expect(html).toContain('3.5');
    // The malicious note must be escaped, not injected.
    expect(html).not.toContain('<script>alert(1)</script>');
    expect(html).toContain('&lt;script&gt;');
  });

  it('includes the self-identified stage when present', () => {
    const html = buildReportHtml(
      baseData({
        stagingLabel: 'Early perimenopause',
        staging: {
          id: 1,
          stage: 'perimenopause_early',
          score: 4,
          answers: {},
          assessedAt: '2026-05-01T00:00:00.000Z',
        },
      }),
    );
    expect(html).toContain('Early perimenopause');
  });
});
