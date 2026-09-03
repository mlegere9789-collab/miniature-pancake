import { NavigationContainer } from "@react-navigation/native";
import { createNativeStackNavigator } from "@react-navigation/native-stack";
import React, { useEffect, useRef, useState } from "react";
import { AppState, AppStateStatus, Pressable, Text, View } from "react-native";
import { AddPlantScreen } from "./src/screens/AddPlantScreen";
import { ArchivedPlantsScreen } from "./src/screens/ArchivedPlantsScreen";
import { CheckInCameraScreen } from "./src/screens/CheckInCameraScreen";
import { GardenDashboardScreen } from "./src/screens/GardenDashboardScreen";
import { LeaderboardScreen } from "./src/screens/LeaderboardScreen";
import { PhotoTimelineScreen } from "./src/screens/PhotoTimelineScreen";
import { PlantDetailScreen } from "./src/screens/PlantDetailScreen";
import { ReportCardScreen } from "./src/screens/ReportCardScreen";
import { YardMapScreen } from "./src/screens/YardMapScreen";
import { fetchGarden, toAbsoluteUrl } from "./src/services/api";
import { flushCheckInQueue } from "./src/services/checkInQueue";
import { requestNotificationPermission, scheduleAllReminders, scheduleCheckInReminder } from "./src/services/notifications";
import { theme } from "./src/theme/theme";
import { CheckIn, Plant, PlantDetail } from "./src/types/domain";

// Phase 1: single hardcoded garden until account/auth + onboarding land (Phase 3).
const DEMO_GARDEN_ID = process.env.EXPO_PUBLIC_VITALS_DEMO_GARDEN_ID ?? "";

type RootStackParamList = {
  Dashboard: undefined;
  PlantDetail: { plantId: string };
  CheckIn: { plant: Plant | PlantDetail };
  AddPlant: { editingPlant?: PlantDetail } | undefined;
  ReportCard: undefined;
  Leaderboard: undefined;
  YardMap: undefined;
  ArchivedPlants: undefined;
  PhotoTimeline: { plantLabel: string; checkIns: CheckIn[] };
};

const Stack = createNativeStackNavigator<RootStackParamList>();

export default function App() {
  const [refreshKey, setRefreshKey] = useState(0);
  const appState = useRef<AppStateStatus>(AppState.currentState);

  useEffect(() => {
    // Baseline every plant's reminder once per app launch — not on every
    // refreshKey bump. Each reminder's countdown restarts from "now" it's
    // (re)scheduled, so re-running this on every check-in/refresh would
    // reset every OTHER plant's countdown too, pushing their reminders back
    // indefinitely with regular app use. A single check-in only resets its
    // own plant's reminder — see the CheckIn screen's onDone below.
    requestNotificationPermission().then((granted) => {
      if (!granted || !DEMO_GARDEN_ID) return;
      fetchGarden(DEMO_GARDEN_ID)
        .then((garden) => scheduleAllReminders(garden.plants))
        .catch(() => undefined);
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    // Flush any check-ins queued while offline (spec §4.2: "syncs when
    // connectivity returns") on launch and whenever the app comes back to
    // the foreground, since that's the most reliable proxy for "we might
    // have connectivity again" without adding a network-status dependency.
    function trySync() {
      flushCheckInQueue()
        .then(({ succeeded }) => {
          if (succeeded > 0) setRefreshKey((k) => k + 1);
        })
        .catch(() => undefined);
    }

    trySync();
    const subscription = AppState.addEventListener("change", (nextState) => {
      if (appState.current.match(/inactive|background/) && nextState === "active") {
        trySync();
      }
      appState.current = nextState;
    });
    return () => subscription.remove();
  }, []);

  return (
    <NavigationContainer>
      <Stack.Navigator screenOptions={{ headerTintColor: theme.color.forestGreen }}>
        <Stack.Screen name="Dashboard" options={({ navigation }) => ({
          title: "Vitals",
          headerRight: () => (
            <View style={{ flexDirection: "row", alignItems: "center", gap: 16 }}>
              <Pressable onPress={() => navigation.navigate("YardMap")} accessibilityLabel="Yard map" accessibilityRole="button">
                <Text style={{ color: theme.color.forestGreen, fontSize: 20 }}>🗺️</Text>
              </Pressable>
              <Pressable onPress={() => navigation.navigate("Leaderboard")} accessibilityLabel="Leaderboard" accessibilityRole="button">
                <Text style={{ color: theme.color.forestGreen, fontSize: 20 }}>🏆</Text>
              </Pressable>
              <Pressable onPress={() => navigation.navigate("ReportCard")} accessibilityLabel="Weekly report" accessibilityRole="button">
                <Text style={{ color: theme.color.forestGreen, fontSize: 20 }}>📋</Text>
              </Pressable>
              <Pressable
                onPress={() => navigation.navigate("ArchivedPlants")}
                accessibilityLabel="Archived plants"
                accessibilityRole="button"
              >
                <Text style={{ color: theme.color.forestGreen, fontSize: 20 }}>🗄️</Text>
              </Pressable>
              <Pressable onPress={() => navigation.navigate("AddPlant")} accessibilityLabel="Add plant" accessibilityRole="button">
                <Text style={{ color: theme.color.forestGreen, fontSize: 28, marginRight: 4 }}>+</Text>
              </Pressable>
            </View>
          ),
        })}>
          {({ navigation }) => (
            <GardenDashboardScreen
              key={refreshKey}
              gardenId={DEMO_GARDEN_ID}
              onSelectPlant={(plant) => navigation.navigate("PlantDetail", { plantId: plant.id })}
              onAddPlant={() => navigation.navigate("AddPlant")}
            />
          )}
        </Stack.Screen>

        <Stack.Screen name="PlantDetail" options={{ title: "Plant" }}>
          {({ route, navigation }) => (
            <PlantDetailScreen
              plantId={route.params.plantId}
              onCheckIn={(plant) => navigation.navigate("CheckIn", { plant })}
              onViewPhotoTimeline={(plant) =>
                navigation.navigate("PhotoTimeline", {
                  plantLabel: plant.nickname || plant.speciesName,
                  checkIns: plant.checkIns,
                })
              }
              onArchived={() => {
                setRefreshKey((k) => k + 1);
                navigation.goBack();
              }}
              onEdit={(plant) => navigation.navigate("AddPlant", { editingPlant: plant })}
            />
          )}
        </Stack.Screen>

        <Stack.Screen name="CheckIn" options={{ title: "Check In", headerShown: false }}>
          {({ route, navigation }) => (
            <CheckInCameraScreen
              plantId={route.params.plant.id}
              plantLabel={route.params.plant.nickname || route.params.plant.speciesName}
              previousPhotoUri={toAbsoluteUrl(
                "checkIns" in route.params.plant ? route.params.plant.checkIns[0]?.photoUrl : undefined,
              )}
              onDone={(result) => {
                // Only reset this plant's own reminder countdown — other
                // plants' reminders should be untouched by this check-in.
                if (result) scheduleCheckInReminder(route.params.plant).catch(() => undefined);
                setRefreshKey((k) => k + 1);
                navigation.goBack();
              }}
            />
          )}
        </Stack.Screen>

        <Stack.Screen
          name="AddPlant"
          options={({ route }) => ({
            title: route.params?.editingPlant ? "Edit Plant" : "Add Plant",
            presentation: "modal",
          })}
        >
          {({ route, navigation }) => (
            <AddPlantScreen
              gardenId={DEMO_GARDEN_ID}
              editingPlant={route.params?.editingPlant}
              onCreated={(plant) => {
                scheduleCheckInReminder(plant).catch(() => undefined);
                setRefreshKey((k) => k + 1);
                navigation.goBack();
              }}
              onCancel={() => navigation.goBack()}
            />
          )}
        </Stack.Screen>

        <Stack.Screen name="ReportCard" options={{ title: "Weekly Report" }}>
          {() => <ReportCardScreen gardenId={DEMO_GARDEN_ID} />}
        </Stack.Screen>

        <Stack.Screen name="Leaderboard" options={{ title: "Leaderboard" }}>
          {() => <LeaderboardScreen gardenId={DEMO_GARDEN_ID} />}
        </Stack.Screen>

        <Stack.Screen name="YardMap" options={{ title: "Yard Map" }}>
          {({ navigation }) => (
            <YardMapScreen
              gardenId={DEMO_GARDEN_ID}
              onSelectPlant={(plant) => navigation.navigate("PlantDetail", { plantId: plant.id })}
            />
          )}
        </Stack.Screen>

        <Stack.Screen name="ArchivedPlants" options={{ title: "Archived Plants" }}>
          {() => (
            <ArchivedPlantsScreen gardenId={DEMO_GARDEN_ID} onRestored={() => setRefreshKey((k) => k + 1)} />
          )}
        </Stack.Screen>

        <Stack.Screen name="PhotoTimeline" options={{ title: "Photo Timeline" }}>
          {({ route }) => (
            <PhotoTimelineScreen plantLabel={route.params.plantLabel} checkIns={route.params.checkIns} />
          )}
        </Stack.Screen>
      </Stack.Navigator>
    </NavigationContainer>
  );
}
