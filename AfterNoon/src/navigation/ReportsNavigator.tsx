import { createNativeStackNavigator } from '@react-navigation/native-stack';
import React from 'react';

import { ReportsScreen } from '@/features/reports/ReportsScreen';
import { ReportPreviewScreen } from '@/features/reports/ReportPreviewScreen';
import { colors } from '@/theme/theme';
import type { ReportsStackParamList } from './types';

const Stack = createNativeStackNavigator<ReportsStackParamList>();

export function ReportsNavigator() {
  return (
    <Stack.Navigator
      screenOptions={{
        headerStyle: { backgroundColor: colors.background },
        headerTintColor: colors.text,
        headerShadowVisible: false,
        contentStyle: { backgroundColor: colors.background },
      }}
    >
      <Stack.Screen
        name="ReportsHome"
        component={ReportsScreen}
        options={{ title: 'Doctor Report' }}
      />
      <Stack.Screen
        name="ReportPreview"
        component={ReportPreviewScreen}
        options={{ title: 'Preview' }}
      />
    </Stack.Navigator>
  );
}
