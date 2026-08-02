import type { NativeStackScreenProps } from '@react-navigation/native-stack';
import React, { useMemo, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { Body, Card, PrimaryButton, Screen, Title } from '@/components/ui';
import { STAGE_INFO, STAGING_QUESTIONS, scoreToStage } from '@/constants/staging';
import { dataAccess } from '@/data';
import { useApp } from '@/context/AppContext';
import { colors, radius, spacing, typography } from '@/theme/theme';
import type { OnboardingStackParamList } from '@/navigation/types';

type Props = NativeStackScreenProps<OnboardingStackParamList, 'StagingAssessment'>;

export function StagingAssessmentScreen({ navigation }: Props) {
  const { notifyDataChanged } = useApp();
  const [step, setStep] = useState(0);
  const [answers, setAnswers] = useState<Record<string, number>>({});
  const [saving, setSaving] = useState(false);

  const question = STAGING_QUESTIONS[step];
  const isLast = step === STAGING_QUESTIONS.length - 1;
  const selected = question ? answers[question.id] : undefined;

  const result = useMemo(() => {
    const score = Object.values(answers).reduce((a, b) => a + b, 0);
    return { score, stage: scoreToStage(score, answers) };
  }, [answers]);

  if (!question) return null;

  function choose(value: number) {
    setAnswers((prev) => ({ ...prev, [question!.id]: value }));
  }

  async function next() {
    if (selected === undefined) return;
    if (!isLast) {
      setStep((s) => s + 1);
      return;
    }
    setSaving(true);
    try {
      await dataAccess.staging.create({
        stage: result.stage,
        score: result.score,
        answers,
      });
      notifyDataChanged();
      navigation.navigate('PrivacyConsent');
    } finally {
      setSaving(false);
    }
  }

  return (
    <Screen contentStyle={styles.content}>
      <View style={styles.header}>
        <Text style={styles.progress}>
          Question {step + 1} of {STAGING_QUESTIONS.length}
        </Text>
        <View style={styles.progressBar}>
          <View
            style={[styles.progressFill, { width: `${((step + 1) / STAGING_QUESTIONS.length) * 100}%` }]}
          />
        </View>
        <Title>{question.prompt}</Title>
      </View>

      <View style={styles.options}>
        {question.options.map((opt) => {
          const active = selected === opt.value;
          return (
            <Pressable
              key={opt.label}
              accessibilityRole="radio"
              accessibilityState={{ selected: active }}
              onPress={() => choose(opt.value)}
              style={[styles.option, active && styles.optionActive]}
            >
              <View style={[styles.radio, active && styles.radioActive]} />
              <Text style={[styles.optionLabel, active && styles.optionLabelActive]}>
                {opt.label}
              </Text>
            </Pressable>
          );
        })}
      </View>

      {isLast && selected !== undefined && (
        <Card style={styles.preview}>
          <Body muted>Based on your answers, you may be in:</Body>
          <Title>{STAGE_INFO[result.stage].title}</Title>
          <Body muted>{STAGE_INFO[result.stage].description}</Body>
          <Body muted style={styles.disclaimer}>
            This is a self-guided estimate, not a medical diagnosis.
          </Body>
        </Card>
      )}

      <View style={styles.actions}>
        {step > 0 && (
          <PrimaryButton
            variant="secondary"
            label="Back"
            onPress={() => setStep((s) => Math.max(0, s - 1))}
          />
        )}
        <View style={{ flex: 1 }}>
          <PrimaryButton
            label={isLast ? 'See my result' : 'Next'}
            onPress={next}
            disabled={selected === undefined}
            loading={saving}
          />
        </View>
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  content: { gap: spacing.lg, paddingVertical: spacing.xl, flexGrow: 1 },
  header: { gap: spacing.sm },
  progress: { ...typography.caption, color: colors.textMuted },
  progressBar: {
    height: 6,
    borderRadius: radius.pill,
    backgroundColor: colors.surfaceMuted,
    overflow: 'hidden',
  },
  progressFill: { height: 6, backgroundColor: colors.accent, borderRadius: radius.pill },
  options: { gap: spacing.sm },
  option: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: spacing.md,
    backgroundColor: colors.surface,
    borderWidth: 1.5,
    borderColor: colors.border,
    borderRadius: radius.md,
    padding: spacing.md,
  },
  optionActive: { borderColor: colors.accent, backgroundColor: colors.accentSoft },
  radio: {
    width: 20,
    height: 20,
    borderRadius: 10,
    borderWidth: 2,
    borderColor: colors.textMuted,
  },
  radioActive: { borderColor: colors.accent, backgroundColor: colors.accent },
  optionLabel: { ...typography.body, color: colors.text, flex: 1 },
  optionLabelActive: { color: colors.text, fontWeight: '600' },
  preview: { backgroundColor: colors.surface },
  disclaimer: { fontStyle: 'italic' },
  actions: { flexDirection: 'row', gap: spacing.md, marginTop: 'auto' },
});
