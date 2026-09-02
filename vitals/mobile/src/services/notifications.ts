import AsyncStorage from "@react-native-async-storage/async-storage";
import * as Notifications from "expo-notifications";
import { Platform } from "react-native";
import { OutbreakAlert, Plant, WeatherAlert } from "../types/domain";

Notifications.setNotificationHandler({
  handleNotification: async () => ({
    shouldShowAlert: true,
    shouldPlaySound: false,
    shouldSetBadge: false,
    shouldShowBanner: true,
    shouldShowList: true,
  }),
});

export async function requestNotificationPermission(): Promise<boolean> {
  const { status } = await Notifications.requestPermissionsAsync();
  if (Platform.OS === "android") {
    await Notifications.setNotificationChannelAsync("checkin-reminders", {
      name: "Check-in reminders",
      importance: Notifications.AndroidImportance.DEFAULT,
    });
  }
  return status === "granted";
}

/**
 * Local check-in reminder (spec §4.5): "Time to check in on your tomatoes —
 * 30 seconds." Scheduled per-plant on its own cadence. Re-scheduling on
 * every check-in (identifier keyed by plant id) keeps exactly one reminder
 * per plant instead of stacking duplicates.
 */
export async function scheduleCheckInReminder(plant: Plant): Promise<void> {
  const identifier = `checkin-reminder-${plant.id}`;
  await Notifications.cancelScheduledNotificationAsync(identifier).catch(() => undefined);

  await Notifications.scheduleNotificationAsync({
    identifier,
    content: {
      title: "Time for a check-in 🌱",
      body: `${plant.nickname || plant.speciesName} is due for its ${plant.checkinCadenceDays}-day check-in — 30 seconds.`,
      data: { plantId: plant.id },
    },
    trigger: {
      seconds: plant.checkinCadenceDays * 24 * 60 * 60,
      repeats: true,
    },
  });
}

export async function scheduleAllReminders(plants: Plant[]): Promise<void> {
  await Promise.all(plants.map(scheduleCheckInReminder));
}

export async function cancelCheckInReminder(plantId: string): Promise<void> {
  await Notifications.cancelScheduledNotificationAsync(`checkin-reminder-${plantId}`).catch(() => undefined);
}

const NOTIFIED_KEY_PREFIX = "vitals.notifiedAlert.";

/**
 * Predictive alerts (spec §4.3/§4.5) are computed server-side but only
 * shown as an in-app banner when the dashboard happens to be open — which
 * defeats the point of a proactive warning. This fires a local push the
 * first time a given alert is seen, deduped by day so it doesn't repeat
 * every time the dashboard reloads while the same alert is still active.
 */
async function notifyOnce(dedupeKey: string, title: string, body: string): Promise<void> {
  const storageKey = `${NOTIFIED_KEY_PREFIX}${dedupeKey}`;
  const today = new Date().toISOString().slice(0, 10);
  const lastNotified = await AsyncStorage.getItem(storageKey);
  if (lastNotified === today) return;

  await Notifications.scheduleNotificationAsync({ content: { title, body }, trigger: null });
  await AsyncStorage.setItem(storageKey, today);
}

export async function notifyWeatherAlertIfNew(alert: WeatherAlert): Promise<void> {
  await notifyOnce(
    "frost",
    "❄️ Frost tonight",
    `${Math.round(alert.minTempTonightC)}°C expected — ${alert.affectedPlantIds.length} frost-sensitive plant${
      alert.affectedPlantIds.length === 1 ? "" : "s"
    } may need covering.`,
  );
}

export async function notifyOutbreakAlertsIfNew(alerts: OutbreakAlert[]): Promise<void> {
  await Promise.all(
    alerts.map((alert) =>
      notifyOnce(
        `outbreak-${alert.condition}`,
        "🦠 Outbreak nearby",
        `${alert.condition.replace(/-/g, " ")} reported in ${alert.gardenCount} nearby gardens this week.`,
      ),
    ),
  );
}
