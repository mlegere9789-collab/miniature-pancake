import React from "react";
import { Image, StyleSheet, View } from "react-native";

interface Props {
  /** URL/local URI of the plant's most recent check-in photo, or undefined for a first-ever check-in. */
  previousPhotoUri?: string;
  opacity?: number;
}

/**
 * Renders the previous check-in photo as a translucent overlay on top of the
 * live camera preview so the user can line up the same angle, distance, and
 * framing every time. This is the single highest-leverage UX detail for
 * making the health score's visual-vitality trend scientifically meaningful
 * (spec §4.2) — without it, a score change could just mean "different photo
 * angle" instead of "the plant changed."
 */
export function GhostOverlay({ previousPhotoUri, opacity = 0.35 }: Props) {
  if (!previousPhotoUri) return null;

  return (
    <View style={StyleSheet.absoluteFill} pointerEvents="none">
      <Image
        source={{ uri: previousPhotoUri }}
        style={[StyleSheet.absoluteFill, { opacity }]}
        resizeMode="cover"
      />
    </View>
  );
}
