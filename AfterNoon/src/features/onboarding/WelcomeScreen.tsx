import type { NativeStackScreenProps } from '@react-navigation/native-stack';
import React from 'react';
import { StyleSheet, Text, View } from 'react-native';

import { Body, PrimaryButton, Screen, Title } from '@/components/ui';
import { colors, spacing, typography } from '@/theme/theme';
import type { OnboardingStackParamList } from '@/navigation/types';

type Props = NativeStackScreenProps<OnboardingStackParamList, 'Welcome'>;

export function WelcomeScreen({ navigation }: Props) {
  return (
    <Screen contentStyle={styles.content}>
      <View style={styles.hero}>
        <Text style={styles.logo}>🌇</Text>
        <Text style={styles.wordmark}>AfterNoon</Text>
        <Title>Understand your change, on your terms.</Title>
        <Body muted>
          A calm, private companion for perimenopause and menopause. Track how you feel, spot
          patterns, and walk into your next appointment prepared.
        </Body>
      </View>
      <View style={styles.actions}>
        <PrimaryButton label="Get started" onPress={() => navigation.navigate('TrustIntro')} />
        <Body muted style={styles.footnote}>
          No account needed. Your data stays on your phone.
        </Body>
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  content: { flexGrow: 1, justifyContent: 'space-between', paddingVertical: spacing.xxl },
  hero: { gap: spacing.md, marginTop: spacing.xl },
  logo: { fontSize: 56 },
  wordmark: { ...typography.display, color: colors.accent },
  actions: { gap: spacing.md },
  footnote: { textAlign: 'center' },
});
