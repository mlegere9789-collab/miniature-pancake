import AsyncStorage from "@react-native-async-storage/async-storage";
import { enqueueCheckIn, flushCheckInQueue, pendingCheckInCount } from "./checkInQueue";
import { submitCheckIn, uploadCheckInPhoto } from "./api";

jest.mock("./api", () => ({
  uploadCheckInPhoto: jest.fn(),
  submitCheckIn: jest.fn(),
}));

const mockedUpload = uploadCheckInPhoto as jest.Mock;
const mockedSubmit = submitCheckIn as jest.Mock;

beforeEach(async () => {
  await AsyncStorage.clear();
  mockedUpload.mockReset();
  mockedSubmit.mockReset();
});

describe("checkInQueue", () => {
  it("starts empty", async () => {
    expect(await pendingCheckInCount()).toBe(0);
  });

  it("enqueues an offline check-in for later sync", async () => {
    await enqueueCheckIn("plant-1", "file://photo.jpg");
    expect(await pendingCheckInCount()).toBe(1);
  });

  it("flushes a queued check-in successfully and reports it as synced", async () => {
    mockedUpload.mockResolvedValue("/uploads/photo.jpg");
    mockedSubmit.mockResolvedValue({ checkIn: {}, priorScore: null });

    await enqueueCheckIn("plant-1", "file://photo.jpg");
    const result = await flushCheckInQueue();

    expect(result).toEqual({ succeeded: 1, remaining: 0, syncedPlantIds: ["plant-1"] });
    expect(await pendingCheckInCount()).toBe(0);
  });

  it("keeps a check-in queued when the sync attempt fails", async () => {
    mockedUpload.mockRejectedValue(new Error("offline"));

    await enqueueCheckIn("plant-1", "file://photo.jpg");
    const result = await flushCheckInQueue();

    expect(result).toEqual({ succeeded: 0, remaining: 1, syncedPlantIds: [] });
    expect(await pendingCheckInCount()).toBe(1);
  });

  it("only removes the items that actually synced, keeping the rest queued", async () => {
    mockedUpload.mockImplementation((uri: string) =>
      uri.includes("fail") ? Promise.reject(new Error("offline")) : Promise.resolve("/uploads/ok.jpg"),
    );
    mockedSubmit.mockResolvedValue({ checkIn: {}, priorScore: null });

    await enqueueCheckIn("plant-ok", "file://ok.jpg");
    await enqueueCheckIn("plant-fail", "file://fail.jpg");
    const result = await flushCheckInQueue();

    expect(result.succeeded).toBe(1);
    expect(result.remaining).toBe(1);
    expect(result.syncedPlantIds).toEqual(["plant-ok"]);
    expect(await pendingCheckInCount()).toBe(1);
  });
});
