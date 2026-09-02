import React, { useRef, useState } from "react";
import { Animated, Image, PanResponder, StyleSheet, View } from "react-native";
import { theme } from "../theme/theme";

interface Props {
  beforeUri: string;
  afterUri: string;
  height?: number;
}

/**
 * Side-by-side check-in photo comparison (spec §4.4: "Photo timeline —
 * side-by-side slider comparing any two check-ins — visually striking,
 * highly shareable"). The "after" photo is the full-bleed base layer; the
 * "before" photo sits on top, clipped by a draggable handle so dragging
 * right reveals more of the before shot.
 */
export function BeforeAfterSlider({ beforeUri, afterUri, height = 320 }: Props) {
  const [containerWidth, setContainerWidth] = useState(0);
  const sliderX = useRef(new Animated.Value(0)).current;
  const widthRef = useRef(0);
  const dragStartRef = useRef(0);

  const panResponder = useRef(
    PanResponder.create({
      onStartShouldSetPanResponder: () => true,
      onMoveShouldSetPanResponder: () => true,
      onPanResponderGrant: (evt) => {
        dragStartRef.current = evt.nativeEvent.locationX;
      },
      onPanResponderMove: (_evt, gesture) => {
        const width = widthRef.current;
        if (width === 0) return;
        const x = Math.max(0, Math.min(width, gesture.moveX - gesture.x0 + dragStartRef.current));
        sliderX.setValue(x);
      },
    }),
  ).current;

  function handleLayout(width: number) {
    if (widthRef.current === 0) {
      widthRef.current = width;
      setContainerWidth(width);
      sliderX.setValue(width / 2);
    }
  }

  return (
    <View
      style={[styles.container, { height }]}
      onLayout={(e) => handleLayout(e.nativeEvent.layout.width)}
    >
      {containerWidth > 0 && (
        <>
          <Image source={{ uri: afterUri }} style={[styles.image, { width: containerWidth, height }]} resizeMode="cover" />
          <Animated.View style={[styles.beforeClip, { height, width: sliderX }]}>
            <Image source={{ uri: beforeUri }} style={{ width: containerWidth, height }} resizeMode="cover" />
          </Animated.View>
          <Animated.View
            {...panResponder.panHandlers}
            style={[styles.handle, { height, transform: [{ translateX: Animated.subtract(sliderX, 2) }] }]}
          >
            <View style={styles.handleKnob} />
          </Animated.View>
          <View style={styles.labelLeft}>
            <LabelText text="Before" />
          </View>
          <View style={styles.labelRight}>
            <LabelText text="After" />
          </View>
        </>
      )}
    </View>
  );
}

function LabelText({ text }: { text: string }) {
  return (
    <View style={styles.labelPill}>
      <Animated.Text style={styles.labelText}>{text}</Animated.Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { width: "100%", borderRadius: theme.radius.md, overflow: "hidden", backgroundColor: "#000" },
  image: { position: "absolute", top: 0, left: 0 },
  beforeClip: { position: "absolute", top: 0, left: 0, overflow: "hidden" },
  handle: {
    position: "absolute",
    top: 0,
    width: 4,
    backgroundColor: theme.color.cream,
    alignItems: "center",
    justifyContent: "center",
  },
  handleKnob: {
    width: 28,
    height: 28,
    borderRadius: 14,
    backgroundColor: theme.color.cream,
    borderWidth: 2,
    borderColor: theme.color.forestGreen,
  },
  labelLeft: { position: "absolute", top: theme.spacing(1), left: theme.spacing(1) },
  labelRight: { position: "absolute", top: theme.spacing(1), right: theme.spacing(1) },
  labelPill: {
    backgroundColor: "rgba(0,0,0,0.55)",
    borderRadius: theme.radius.sm,
    paddingHorizontal: theme.spacing(1),
    paddingVertical: theme.spacing(0.5),
  },
  labelText: { color: "white", fontSize: theme.font.captionSize, fontWeight: "600" },
});
