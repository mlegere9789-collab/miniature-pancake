import AsyncStorage from "@react-native-async-storage/async-storage";
import { submitCheckIn, uploadCheckInPhoto } from "./api";

const QUEUE_KEY = "vitals.checkInQueue.v1";

interface QueuedCheckIn {
  plantId: string;
  localPhotoUri: string;
  queuedAt: string;
}

async function readQueue(): Promise<QueuedCheckIn[]> {
  const raw = await AsyncStorage.getItem(QUEUE_KEY);
  return raw ? (JSON.parse(raw) as QueuedCheckIn[]) : [];
}

async function writeQueue(queue: QueuedCheckIn[]): Promise<void> {
  await AsyncStorage.setItem(QUEUE_KEY, JSON.stringify(queue));
}

/** Enqueue a check-in captured while offline (or as a durable retry buffer even when online). */
export async function enqueueCheckIn(plantId: string, localPhotoUri: string): Promise<void> {
  const queue = await readQueue();
  queue.push({ plantId, localPhotoUri, queuedAt: new Date().toISOString() });
  await writeQueue(queue);
}

/**
 * Attempt to flush every queued check-in to the backend. Call this on app
 * foreground and on network-reconnect. Failed items stay queued for the
 * next attempt; successful ones are removed. Returns the plant ids that
 * actually synced so the caller can reset just those plants' reminder
 * countdowns from this real check-in moment (see App.tsx) — the plant
 * whose check-in just landed shouldn't still be nagged on its old,
 * pre-offline-gap schedule.
 */
export async function flushCheckInQueue(): Promise<{ succeeded: number; remaining: number; syncedPlantIds: string[] }> {
  const queue = await readQueue();
  const stillQueued: QueuedCheckIn[] = [];
  const syncedPlantIds: string[] = [];

  for (const item of queue) {
    try {
      const photoUrl = await uploadCheckInPhoto(item.localPhotoUri);
      await submitCheckIn(item.plantId, photoUrl);
      syncedPlantIds.push(item.plantId);
    } catch {
      stillQueued.push(item);
    }
  }

  await writeQueue(stillQueued);
  return { succeeded: syncedPlantIds.length, remaining: stillQueued.length, syncedPlantIds };
}

export async function pendingCheckInCount(): Promise<number> {
  return (await readQueue()).length;
}
