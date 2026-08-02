import React from 'react';
import { StyleSheet, Text, View } from 'react-native';

import { Body, Card, Heading, Screen, Title } from '@/components/ui';
import { colors, radius, spacing, typography } from '@/theme/theme';

/**
 * Community is a preview surface in this MVP. It intentionally shows no real
 * user content and makes no network calls — consistent with the privacy-first
 * posture, a real community feature would be a separate, opt-in module. This
 * screen exists so the tab structure matches the prototype.
 */

const TOPICS = [
  { emoji: '🌙', title: 'Sleep & night sweats', count: '2.1k members' },
  { emoji: '🧠', title: 'Brain fog & focus', count: '1.4k members' },
  { emoji: '💪', title: 'Movement & strength', count: '980 members' },
  { emoji: '🍲', title: 'Nutrition', count: '1.7k members' },
];

export function CommunityScreen() {
  return (
    <Screen>
      <Title>Community</Title>
      <Body muted>Connect with others going through the change. Coming soon.</Body>

      <Card style={styles.banner}>
        <Text style={styles.bannerIcon}>🌸</Text>
        <View style={{ flex: 1 }}>
          <Heading>A private space, when you&apos;re ready</Heading>
          <Body muted>
            When Community launches it will be opt-in and separate from your health log. Nothing
            here reads or posts your symptom data.
          </Body>
        </View>
      </Card>

      <Heading>Popular circles</Heading>
      {TOPICS.map((t) => (
        <Card key={t.title} style={styles.topic}>
          <Text style={styles.topicIcon}>{t.emoji}</Text>
          <View style={{ flex: 1 }}>
            <Text style={styles.topicTitle}>{t.title}</Text>
            <Text style={styles.topicMeta}>{t.count}</Text>
          </View>
          <Text style={styles.preview}>Preview</Text>
        </Card>
      ))}
    </Screen>
  );
}

const styles = StyleSheet.create({
  banner: {
    flexDirection: 'row',
    gap: spacing.md,
    backgroundColor: colors.supportSoft,
    borderColor: colors.supportSoft,
  },
  bannerIcon: { fontSize: 28 },
  topic: { flexDirection: 'row', alignItems: 'center', gap: spacing.md },
  topicIcon: { fontSize: 24 },
  topicTitle: { ...typography.body, color: colors.text, fontWeight: '600' },
  topicMeta: { ...typography.caption, color: colors.textMuted },
  preview: {
    ...typography.caption,
    color: colors.textMuted,
    borderWidth: 1,
    borderColor: colors.border,
    borderRadius: radius.pill,
    paddingHorizontal: spacing.sm,
    paddingVertical: 2,
    overflow: 'hidden',
  },
});
