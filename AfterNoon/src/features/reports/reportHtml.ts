/**
 * Pure HTML generation for the Doctor Report.
 *
 * Takes the same `ReportData` the preview screen renders and produces a
 * self-contained, printable HTML string. No network, no external assets — it is
 * handed straight to expo-print on-device. Keeping this pure (data in, string
 * out) makes it trivial to snapshot-test and guarantees the PDF matches the
 * in-app preview.
 */

import { SEVERITY_LABELS } from '@/constants/symptoms';
import { formatDisplayDate } from '@/utils/date';
import type { ReportData } from '@/data/reportData';

function esc(input: string): string {
  return input
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

const SEVERITY_HEX = ['#DCE7DA', '#E7E0C4', '#F1D2A6', '#E9B089', '#D9795E'];

function severityCell(avg: number): string {
  const idx = Math.min(4, Math.max(0, Math.round(avg) - 1));
  return SEVERITY_HEX[idx] ?? '#eee';
}

export function buildReportHtml(data: ReportData): string {
  const rangeLabel = `${formatDisplayDate(data.startDateKey)} – ${formatDisplayDate(data.endDateKey)}`;

  const summaryRows = data.categorySummaries
    .map(
      (s) => `
        <tr>
          <td>${esc(s.emoji)} ${esc(s.label)}</td>
          <td class="num">${s.avgSeverity.toFixed(1)}</td>
          <td class="num">${s.maxSeverity} (${esc(SEVERITY_LABELS[s.maxSeverity] ?? '')})</td>
          <td class="num">${s.daysLogged}</td>
          <td class="num">${s.totalLogs}</td>
        </tr>`,
    )
    .join('');

  const cycleRows = data.cycleNotes
    .map(
      (c) => `
        <tr>
          <td>${esc(formatDisplayDate(c.noteDate))}</td>
          <td>${esc(c.flow ?? '—')}</td>
          <td>${esc(c.note ?? '')}</td>
        </tr>`,
    )
    .join('');

  const stagingBlock = data.staging
    ? `<p><strong>Self-identified stage:</strong> ${esc(data.stagingLabel ?? '')}
         <span class="muted">(self-assessment on ${esc(formatDisplayDate(data.staging.assessedAt))})</span></p>`
    : '<p class="muted">No staging self-assessment recorded.</p>';

  const emptyNotice =
    data.totalLogs === 0
      ? '<p class="muted">No symptoms were logged in this date range.</p>'
      : '';

  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>AfterNoon Doctor Report</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, Roboto, 'Helvetica Neue', Arial, sans-serif; color: #2E2A26; margin: 0; padding: 32px; }
  h1 { font-size: 24px; margin: 0 0 4px; color: #C2694B; }
  h2 { font-size: 16px; margin: 28px 0 10px; border-bottom: 2px solid #E7DCCF; padding-bottom: 6px; }
  .meta { color: #6B625A; font-size: 13px; margin: 0 0 4px; }
  .muted { color: #6B625A; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  th, td { text-align: left; padding: 8px 10px; border-bottom: 1px solid #EFE7DC; }
  th { background: #F3EAE0; font-size: 12px; text-transform: uppercase; letter-spacing: 0.03em; }
  td.num, th.num { text-align: right; }
  .legend { display: flex; align-items: center; gap: 6px; font-size: 12px; color: #6B625A; margin: 8px 0 0; }
  .swatch { width: 16px; height: 16px; border-radius: 3px; display: inline-block; }
  .footer { margin-top: 32px; padding-top: 12px; border-top: 1px solid #E7DCCF; font-size: 11px; color: #6B625A; }
  .disclaimer { background: #F3EAE0; border-radius: 8px; padding: 12px 14px; font-size: 12px; color: #6B625A; margin-top: 8px; }
</style>
</head>
<body>
  <h1>AfterNoon — Symptom Summary</h1>
  <p class="meta">Reporting period: <strong>${esc(rangeLabel)}</strong></p>
  <p class="meta">Generated: ${esc(formatDisplayDate(data.generatedAt))}</p>
  ${stagingBlock}
  ${emptyNotice}

  <h2>Symptom overview</h2>
  ${
    data.categorySummaries.length > 0
      ? `<table>
          <thead>
            <tr>
              <th>Symptom</th>
              <th class="num">Avg severity</th>
              <th class="num">Peak</th>
              <th class="num">Days logged</th>
              <th class="num">Entries</th>
            </tr>
          </thead>
          <tbody>${summaryRows}</tbody>
        </table>
        <div class="legend">
          <span>Severity 1–5:</span>
          ${SEVERITY_HEX.map((h, i) => `<span class="swatch" style="background:${h}"></span><span>${i + 1}</span>`).join('')}
        </div>`
      : '<p class="muted">—</p>'
  }

  <h2>Cycle notes</h2>
  ${
    data.cycleNotes.length > 0
      ? `<table>
          <thead><tr><th>Date</th><th>Flow</th><th>Note</th></tr></thead>
          <tbody>${cycleRows}</tbody>
        </table>`
      : '<p class="muted">No cycle notes recorded in this period.</p>'
  }

  <div class="disclaimer">
    This report was generated on the patient's device from data they self-recorded in the
    AfterNoon app. It is a symptom diary summary intended to support a clinical conversation and
    is not a diagnosis. Severity is a 1–5 self-rating.
  </div>

  <div class="footer">
    Prepared privately with AfterNoon. ${data.totalLogs} total symptom ${
      data.totalLogs === 1 ? 'entry' : 'entries'
    } in this period.
  </div>
</body>
</html>`;
}
