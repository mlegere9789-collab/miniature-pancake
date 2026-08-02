import type { NavigatorScreenParams } from '@react-navigation/native';

export type OnboardingStackParamList = {
  Welcome: undefined;
  TrustIntro: undefined;
  StagingAssessment: undefined;
  PrivacyConsent: undefined;
};

export type ReportsStackParamList = {
  ReportsHome: undefined;
  ReportPreview: { startIso: string; endIso: string };
};

export type MainTabParamList = {
  Home: undefined;
  Trends: undefined;
  Community: undefined;
  Reports: NavigatorScreenParams<ReportsStackParamList> | undefined;
};

export type RootStackParamList = {
  Onboarding: NavigatorScreenParams<OnboardingStackParamList> | undefined;
  Main: NavigatorScreenParams<MainTabParamList> | undefined;
  // Settings is reachable from the Home header rather than being a 5th tab, so
  // the tab bar stays Home / Trends / Community / Reports as in the prototype.
  Settings: undefined;
};
