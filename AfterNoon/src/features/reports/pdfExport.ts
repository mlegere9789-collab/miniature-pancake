/**
 * On-device PDF export + native share.
 *
 * Uses expo-print (`Print.printToFileAsync`) to render the report HTML to a PDF
 * entirely on the device, then expo-sharing to hand it to the OS share sheet so
 * the user sends/prints/saves it themselves. NOTE: no upload happens here — the
 * file never leaves the device except through the user's own explicit share
 * action. This is deliberately independent of the sync boundary: the user
 * exporting their own report is a local action, not health-data egress by the
 * app.
 *
 * (We use expo-print rather than react-native-html-to-pdf because it is the
 * Expo-managed-workflow equivalent — same "HTML in, on-device PDF out" contract,
 * no native linking, works in EAS builds.)
 */

import * as Print from 'expo-print';
import * as Sharing from 'expo-sharing';

import type { ReportData } from '@/data/reportData';
import { buildReportHtml } from './reportHtml';

export interface ExportResult {
  uri: string;
  shared: boolean;
}

/**
 * Generates a PDF from the report data and opens the share sheet. Returns the
 * file URI so callers can surface it if sharing is unavailable.
 */
export async function exportReportPdf(data: ReportData): Promise<ExportResult> {
  const html = buildReportHtml(data);

  const { uri } = await Print.printToFileAsync({ html, base64: false });

  const canShare = await Sharing.isAvailableAsync();
  if (canShare) {
    await Sharing.shareAsync(uri, {
      mimeType: 'application/pdf',
      dialogTitle: 'Share your AfterNoon report',
      UTI: 'com.adobe.pdf',
    });
    return { uri, shared: true };
  }

  return { uri, shared: false };
}
