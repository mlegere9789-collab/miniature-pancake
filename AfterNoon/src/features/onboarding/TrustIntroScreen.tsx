import type { NativeStackScreenProps } from '@react-navigation/native-stack';
import React from 'react';
import { StyleSheet, Text, View } from 'react-native';

import { Body, Card, Heading, PrimaryButton, Screen, Title } from '@/components/ui';
import { colors, radius, spacing } from '@/theme/theme';
import type { OnboardingStackParamList } from '@/navigation/types';

type Props = NativeStackScreenProps<OnboardingStackParamList, 'TrustIntro'>;

const POINTS = [
  {
    icon: '🔒',
    title: 'On your device by default',
    body: 'Everything you log is stored locally. Nothing is sent anywhere unless you turn on sync yourself.',
  },
  {
    icon: '🚫',
    title: 'No trackers',
    body: 'We do not embed third-party analytics or ad SDKs that follow what you record.',
  },
  {
    icon: '📄',
    title: 'Yours to share',
    body: 'Generate a doctor-ready report on demand and share it however you choose — you decide who sees it.',
  },
];

export function TrustIntroScreen({ navigation }: Props) {
  return (
    <Screen contentStyle={styles.content}>
      <View style={styles.header}>
        <Text style={styles.emoji}>🤝</Text>
        <Title>Privacy you can actually verify</Title>
        <Body muted>Before anything else, here is how AfterNoon treats your health data.</Body>
      </View>

      <View style={styles.list}>
        {POINTS.map((p) => (
          <Card key={p.title} style={styles.point}>
            <Text style={styles.pointIcon}>{p.icon}</Text>
            <View style={styles.pointText}>
              <Heading>{p.title}</Heading>
              <Body muted>{p.body}</Body>
            </View>
          </Card>
        ))}
      </View>

      <PrimaryButton label="Continue" onPress={() => navigation.navigate('StagingAssessment')} />
    </Screen>
  );
}

const styles = StyleSheet.create({
  content: { gap: spacing.lg, paddingVertical: spacing.xl },
  header: { gap: spacing.sm },
  emoji: { fontSize: 44 },
  list: { gap: spacing.md },
  point: { flexDirection: 'row', gap: spacing.md, alignItems: 'flex-start' },
  pointIcon: {
    fontSize: 24,
    backgroundColor: colors.trustSoft,
    borderRadius: radius.md,
    padding: spacing.sm,
    overflow: 'hidden',
  },
  pointText: { flex: 1, gap: spacing.xs },
});
