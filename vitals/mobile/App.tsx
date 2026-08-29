import { NavigationContainer } from "@react-navigation/native";
import { createNativeStackNavigator } from "@react-navigation/native-stack";
import React, { useEffect, useState } from "react";
import { Pressable, Text } from "react-native";
import { AddPlantScreen } from "./src/screens/AddPlantScreen";
import { CheckInCameraScreen } from "./src/screens/CheckInCameraScreen";
import { GardenDashboardScreen } from "./src/screens/GardenDashboardScreen";
import { PlantDetailScreen } from "./src/screens/PlantDetailScreen";
import { API_BASE_URL, fetchGarden } from "./src/services/api";
import { requestNotificationPermission, scheduleAllReminders, scheduleCheckInReminder } from "./src/services/notifications";
import { theme } from "./src/theme/theme";
import { Plant, PlantDetail } from "./src/types/domain";

// Phase 1: single hardcoded garden until account/auth + onboarding land (Phase 3).
const DEMO_GARDEN_ID = process.env.EXPO_PUBLIC_VITALS_DEMO_GARDEN_ID ?? "";

type RootStackParamList = {
  Dashboard: undefined;
  PlantDetail: { plantId: string };
  CheckIn: { plant: Plant | PlantDetail };
  AddPlant: undefined;
};

const Stack = createNativeStackNavigator<RootStackParamList>();

function toAbsoluteUrl(photoUrl: string | undefined): string | undefined {
  if (!photoUrl) return undefined;
  return photoUrl.startsWith("http") ? photoUrl : `${API_BASE_URL}${photoUrl}`;
}

export default function App() {
  const [refreshKey, setRefreshKey] = useState(0);

  useEffect(() => {
    requestNotificationPermission().then((granted) => {
      if (!granted || !DEMO_GARDEN_ID) return;
      fetchGarden(DEMO_GARDEN_ID)
        .then((garden) => scheduleAllReminders(garden.plants))
        .catch(() => undefined);
    });
  }, [refreshKey]);

  return (
    <NavigationContainer>
      <Stack.Navigator screenOptions={{ headerTintColor: theme.color.forestGreen }}>
        <Stack.Screen name="Dashboard" options={({ navigation }) => ({
          title: "Vitals",
          headerRight: () => (
            <Pressable onPress={() => navigation.navigate("AddPlant")}>
              <Text style={{ color: theme.color.forestGreen, fontSize: 28, marginRight: 4 }}>+</Text>
            </Pressable>
          ),
        })}>
          {({ navigation }) => (
            <GardenDashboardScreen
              key={refreshKey}
              gardenId={DEMO_GARDEN_ID}
              onSelectPlant={(plant) => navigation.navigate("PlantDetail", { plantId: plant.id })}
            />
          )}
        </Stack.Screen>

        <Stack.Screen name="PlantDetail" options={{ title: "Plant" }}>
          {({ route, navigation }) => (
            <PlantDetailScreen
              plantId={route.params.plantId}
              onCheckIn={(plant) => navigation.navigate("CheckIn", { plant })}
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
              onDone={() => {
                setRefreshKey((k) => k + 1);
                navigation.goBack();
              }}
            />
          )}
        </Stack.Screen>

        <Stack.Screen name="AddPlant" options={{ title: "Add Plant", presentation: "modal" }}>
          {({ navigation }) => (
            <AddPlantScreen
              gardenId={DEMO_GARDEN_ID}
              onCreated={(plant) => {
                scheduleCheckInReminder(plant).catch(() => undefined);
                setRefreshKey((k) => k + 1);
                navigation.goBack();
              }}
              onCancel={() => navigation.goBack()}
            />
          )}
        </Stack.Screen>
      </Stack.Navigator>
    </NavigationContainer>
  );
}
