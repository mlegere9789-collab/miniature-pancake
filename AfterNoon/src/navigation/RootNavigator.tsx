import { createNativeStackNavigator } from '@react-navigation/native-stack';
import React from 'react';

import { useApp, needsOnboarding } from '@/context/AppContext';
import { SettingsScreen } from '@/features/settings/SettingsScreen';
import { colors } from '@/theme/theme';
import { OnboardingNavigator } from './OnboardingNavigator';
import { TabNavigator } from './TabNavigator';
import type { RootStackParamList } from './types';

const Stack = createNativeStackNavigator<RootStackParamList>();

export function RootNavigator() {
  const { consent } = useApp();
  const showOnboarding = needsOnboarding(consent);

  return (
    <Stack.Navigator
      screenOptions={{
        headerStyle: { backgroundColor: colors.background },
        headerTintColor: colors.text,
        headerShadowVisible: false,
        contentStyle: { backgroundColor: colors.background },
      }}
    >
      {showOnboarding ? (
        <Stack.Screen name="Onboarding" component={OnboardingNavigator} options={{ headerShown: false }} />
      ) : (
        <>
          <Stack.Screen name="Main" component={TabNavigator} options={{ headerShown: false }} />
          <Stack.Screen name="Settings" component={SettingsScreen} options={{ title: 'Settings' }} />
        </>
      )}
    </Stack.Navigator>
  );
}
