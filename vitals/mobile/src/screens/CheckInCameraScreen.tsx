import { CameraType, CameraView, useCameraPermissions } from "expo-camera";
import React, { useRef, useState } from "react";
import { ActivityIndicator, Alert, Linking, Pressable, ScrollView, StyleSheet, Text, View } from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";
import { GhostOverlay } from "../components/GhostOverlay";
import { ScoreDeltaBadge } from "../components/ScoreDeltaBadge";
import { setTreatmentPlanCompleted, submitCheckIn, uploadCheckInPhoto } from "../services/api";
import { enqueueCheckIn } from "../services/checkInQueue";
import { theme } from "../theme/theme";
import { CreateCheckInResponse } from "../types/domain";

type Stage = "aligning" | "capturing" | "scoring" | "result" | "error";

interface Props {
  plantId: string;
  plantLabel: string;
  /** Photo URL/URI from the plant's most recent check-in, used for ghost-overlay alignment. */
  previousPhotoUri?: string;
  onDone: (result: CreateCheckInResponse | null) => void;
}

/**
 * The core 20-second check-in ritual (spec §4.2): camera opens with a ghost
 * overlay of the previous photo for alignment, one tap captures, the score
 * updates with an animated result. Falls back to an offline queue if the
 * upload/scoring call fails.
 */
export function CheckInCameraScreen({ plantId, plantLabel, previousPhotoUri, onDone }: Props) {
  const insets = useSafeAreaInsets();
  const [permission, requestPermission] = useCameraPermissions();
  const [stage, setStage] = useState<Stage>("aligning");
  const [result, setResult] = useState<CreateCheckInResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [treatedIds, setTreatedIds] = useState<Set<string>>(new Set());
  const cameraRef = useRef<CameraView>(null);

  async function handleMarkTreated(treatmentPlanId: string) {
    setTreatedIds((prev) => new Set(prev).add(treatmentPlanId));
    await setTreatmentPlanCompleted(treatmentPlanId, true).catch(() => undefined);
  }

  if (!permission) {
    return <View style={styles.center} />;
  }

  if (!permission.granted) {
    // Once the OS has permanently denied the prompt, requestPermission()
    // just silently returns the same denied state again on iOS — the only
    // way back in is the system Settings app.
    if (!permission.canAskAgain) {
      return (
        <View style={styles.center}>
          <Text style={styles.bodyText}>
            Camera access is off for Vitals. Enable it in Settings to check in on {plantLabel}.
          </Text>
          <Pressable style={styles.primaryButton} onPress={() => Linking.openSettings()}>
            <Text style={styles.primaryButtonText}>Open Settings</Text>
          </Pressable>
        </View>
      );
    }
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>Vitals needs camera access to check in on {plantLabel}.</Text>
        <Pressable style={styles.primaryButton} onPress={requestPermission}>
          <Text style={styles.primaryButtonText}>Grant camera access</Text>
        </Pressable>
      </View>
    );
  }

  async function handleCapture() {
    if (!cameraRef.current || stage !== "aligning") return;
    setStage("capturing");

    try {
      const photo = await cameraRef.current.takePictureAsync({ quality: 0.85 });
      if (!photo) throw new Error("capture failed");

      setStage("scoring");
      try {
        const photoUrl = await uploadCheckInPhoto(photo.uri);
        const submitted = await submitCheckIn(plantId, photoUrl);
        setResult(submitted);
        setStage("result");
      } catch (networkError) {
        // Offline or backend unreachable: queue for later sync rather than losing the check-in.
        await enqueueCheckIn(plantId, photo.uri);
        setErrorMessage("You're offline — this check-in will sync automatically once you're back online.");
        setStage("error");
      }
    } catch (captureError) {
      setErrorMessage("Couldn't capture the photo. Try again.");
      setStage("error");
      Alert.alert("Capture failed", String(captureError));
    }
  }

  if (stage === "result" && result) {
    const openFlags = result.checkIn.diagnosticFlags.filter((f) => f.status !== "RESOLVED");
    return (
      <ScrollView contentContainerStyle={styles.resultScroll}>
        <View style={styles.center}>
          <Text style={styles.heroScore}>{result.checkIn.computedScore}</Text>
          <ScoreDeltaBadge priorScore={result.priorScore} newScore={result.checkIn.computedScore} />
          <Text style={[styles.bodyText, { marginTop: theme.spacing(3) }]}>{plantLabel}</Text>

          {openFlags.length > 0 && (
            <View style={styles.flagsSection}>
              {openFlags.map((f) => (
                <View key={f.id} style={styles.flagCard}>
                  <Text style={styles.flagCardTitle}>
                    ⚠ {f.condition.replace(/-/g, " ")} — {f.urgency.replace(/_/g, " ").toLowerCase()}
                  </Text>
                  {f.treatmentPlan && !treatedIds.has(f.treatmentPlan.id) && (
                    <>
                      {f.treatmentPlan.steps.map((step, i) => (
                        <Text key={i} style={styles.flagCardStep}>
                          • {step}
                        </Text>
                      ))}
                      {f.treatmentPlan.productsRecommended.length > 0 && (
                        <Text style={styles.flagCardProducts}>
                          Suggested: {f.treatmentPlan.productsRecommended.join(", ")}
                        </Text>
                      )}
                      <Pressable style={styles.flagCardDoneButton} onPress={() => handleMarkTreated(f.treatmentPlan!.id)}>
                        <Text style={styles.flagCardDoneButtonText}>Mark treated</Text>
                      </Pressable>
                    </>
                  )}
                  {f.treatmentPlan && treatedIds.has(f.treatmentPlan.id) && (
                    <Text style={styles.flagCardDoneText}>✓ Marked treated</Text>
                  )}
                </View>
              ))}
            </View>
          )}

          <Pressable style={styles.primaryButton} onPress={() => onDone(result)}>
            <Text style={styles.primaryButtonText}>Done</Text>
          </Pressable>
        </View>
      </ScrollView>
    );
  }

  if (stage === "error") {
    return (
      <View style={styles.center}>
        <Text style={styles.bodyText}>{errorMessage}</Text>
        <Pressable style={styles.primaryButton} onPress={() => onDone(null)}>
          <Text style={styles.primaryButtonText}>Back to garden</Text>
        </Pressable>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <CameraView ref={cameraRef} style={StyleSheet.absoluteFill} facing={"back" as CameraType} />
      <GhostOverlay previousPhotoUri={previousPhotoUri} />

      <View style={[styles.topBar, { top: insets.top + theme.spacing(3) }]}>
        <Text style={styles.topBarText}>
          {previousPhotoUri ? "Line up with the faded outline of your last photo" : `First check-in: ${plantLabel}`}
        </Text>
      </View>

      <View style={[styles.bottomBar, { bottom: insets.bottom + theme.spacing(4) }]}>
        {stage === "aligning" ? (
          <Pressable style={styles.shutterButton} onPress={handleCapture} accessibilityLabel="Capture check-in photo" />
        ) : (
          <ActivityIndicator size="large" color={theme.color.cream} />
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: "black" },
  resultScroll: { flexGrow: 1, backgroundColor: theme.color.cream },
  center: {
    flex: 1,
    alignItems: "center",
    justifyContent: "center",
    padding: theme.spacing(4),
    backgroundColor: theme.color.cream,
  },
  flagsSection: { width: "100%", marginTop: theme.spacing(3) },
  flagCard: {
    backgroundColor: "white",
    borderRadius: theme.radius.md,
    padding: theme.spacing(2),
    marginBottom: theme.spacing(1.5),
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: theme.color.border,
  },
  flagCardTitle: { fontSize: theme.font.bodySize, fontWeight: "600", color: theme.color.danger, marginBottom: theme.spacing(1) },
  flagCardStep: { fontSize: theme.font.captionSize, color: theme.color.textPrimary, marginBottom: theme.spacing(0.5) },
  flagCardProducts: {
    fontSize: theme.font.captionSize,
    color: theme.color.textSecondary,
    fontStyle: "italic",
    marginTop: theme.spacing(0.5),
  },
  flagCardDoneButton: { marginTop: theme.spacing(1), alignSelf: "flex-start" },
  flagCardDoneButtonText: { fontSize: theme.font.captionSize, color: theme.color.forestGreen, fontWeight: "600" },
  flagCardDoneText: { fontSize: theme.font.captionSize, color: theme.color.forestGreenLight, fontWeight: "600" },
  topBar: {
    position: "absolute",
    left: theme.spacing(3),
    right: theme.spacing(3),
    alignItems: "center",
  },
  topBarText: {
    color: theme.color.cream,
    fontSize: theme.font.bodySize,
    textAlign: "center",
    backgroundColor: "rgba(27,58,43,0.7)",
    paddingHorizontal: theme.spacing(2),
    paddingVertical: theme.spacing(1),
    borderRadius: theme.radius.sm,
    overflow: "hidden",
  },
  bottomBar: {
    position: "absolute",
    left: 0,
    right: 0,
    alignItems: "center",
  },
  shutterButton: {
    width: 76,
    height: 76,
    borderRadius: 38,
    borderWidth: 5,
    borderColor: theme.color.cream,
    backgroundColor: "transparent",
  },
  heroScore: {
    fontSize: theme.font.heroSize,
    fontWeight: "700",
    color: theme.color.forestGreen,
  },
  bodyText: {
    fontSize: theme.font.bodySize,
    color: theme.color.textPrimary,
    textAlign: "center",
  },
  primaryButton: {
    marginTop: theme.spacing(4),
    backgroundColor: theme.color.forestGreen,
    paddingHorizontal: theme.spacing(4),
    paddingVertical: theme.spacing(2),
    borderRadius: theme.radius.md,
  },
  primaryButtonText: {
    color: theme.color.cream,
    fontWeight: "600",
    fontSize: theme.font.bodySize,
  },
});
