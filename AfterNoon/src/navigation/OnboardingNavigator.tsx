import { createNativeStackNavigator } from '@react-navigation/native-stack';
import React from 'react';

import { WelcomeScreen } from '@/features/onboarding/WelcomeScreen';
import { TrustIntroScreen } from '@/features/onboarding/TrustIntroScreen';
import { StagingAssessmentScreen } from '@/features/onboarding/StagingAssessmentScreen';
import { PrivacyConsentScreen } from '@/features/onboarding/PrivacyConsentScreen';
import { colors } from '@/theme/theme';
import type { OnboardingStackParamList } from './types';

const Stack = createNativeStackNavigator<OnboardingStackParamList>();

export function OnboardingNavigator() {
  return (
    <Stack.Navigator
      screenOptions={{
        headerShown: false,
        contentStyle: { backgroundColor: colors.background },
        animation: 'slide_from_right',
      }}
    >
      <Stack.Screen name="Welcome" component={WelcomeScreen} />
      <Stack.Screen name="TrustIntro" component={TrustIntroScreen} />
      <Stack.Screen name="StagingAssessment" component={StagingAssessmentScreen} />
      <Stack.Screen name="PrivacyConsent" component={PrivacyConsentScreen} />
    </Stack.Navigator>
  );
}
