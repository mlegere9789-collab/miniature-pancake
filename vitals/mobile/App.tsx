import { NavigationContainer } from "@react-navigation/native";
import { createNativeStackNavigator } from "@react-navigation/native-stack";
import React, { useState } from "react";
import { CheckInCameraScreen } from "./src/screens/CheckInCameraScreen";
import { GardenDashboardScreen } from "./src/screens/GardenDashboardScreen";
import { Plant } from "./src/types/domain";

// Phase 1: single hardcoded garden until account/auth + onboarding land (Phase 3).
const DEMO_GARDEN_ID = process.env.EXPO_PUBLIC_VITALS_DEMO_GARDEN_ID ?? "";

type RootStackParamList = {
  Dashboard: undefined;
  CheckIn: { plant: Plant };
};

const Stack = createNativeStackNavigator<RootStackParamList>();

export default function App() {
  const [refreshKey, setRefreshKey] = useState(0);

  return (
    <NavigationContainer>
      <Stack.Navigator>
        <Stack.Screen name="Dashboard" options={{ title: "Vitals" }}>
          {({ navigation }) => (
            <GardenDashboardScreen
              key={refreshKey}
              gardenId={DEMO_GARDEN_ID}
              onSelectPlant={(plant) => navigation.navigate("CheckIn", { plant })}
            />
          )}
        </Stack.Screen>
        <Stack.Screen name="CheckIn" options={{ title: "Check In", headerShown: false }}>
          {({ route, navigation }) => (
            <CheckInCameraScreen
              plantId={route.params.plant.id}
              plantLabel={route.params.plant.nickname || route.params.plant.speciesName}
              onDone={() => {
                setRefreshKey((k) => k + 1);
                navigation.goBack();
              }}
            />
          )}
        </Stack.Screen>
      </Stack.Navigator>
    </NavigationContainer>
  );
}
