import * as Notifications from "expo-notifications";
import { Platform } from "react-native";
import { Plant } from "../types/domain";

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
