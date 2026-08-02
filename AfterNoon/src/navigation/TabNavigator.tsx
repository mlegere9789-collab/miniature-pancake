import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import React from 'react';
import { Text } from 'react-native';

import { HomeScreen } from '@/features/home/HomeScreen';
import { TrendsScreen } from '@/features/trends/TrendsScreen';
import { CommunityScreen } from '@/features/community/CommunityScreen';
import { ReportsNavigator } from './ReportsNavigator';
import { colors } from '@/theme/theme';
import type { MainTabParamList } from './types';

const Tab = createBottomTabNavigator<MainTabParamList>();

// Emoji tab icons keep the scaffold dependency-light; swap for vector icons to
// match the prototype.
const ICONS: Record<keyof MainTabParamList, string> = {
  Home: '🏠',
  Trends: '📈',
  Community: '💬',
  Reports: '📄',
};

export function TabNavigator() {
  return (
    <Tab.Navigator
      screenOptions={({ route }) => ({
        headerShown: false,
        tabBarActiveTintColor: colors.accent,
        tabBarInactiveTintColor: colors.textMuted,
        tabBarStyle: {
          backgroundColor: colors.surface,
          borderTopColor: colors.border,
        },
        tabBarIcon: ({ focused }) => (
          <Text style={{ fontSize: 20, opacity: focused ? 1 : 0.6 }}>{ICONS[route.name]}</Text>
        ),
      })}
    >
      <Tab.Screen name="Home" component={HomeScreen} />
      <Tab.Screen name="Trends" component={TrendsScreen} />
      <Tab.Screen name="Community" component={CommunityScreen} />
      <Tab.Screen
        name="Reports"
        component={ReportsNavigator}
        options={{ title: 'Reports' }}
      />
    </Tab.Navigator>
  );
}
