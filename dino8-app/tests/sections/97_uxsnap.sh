# ---------------------------------------------------------------------------
# UXSNAP_: grid snap, ortho and the object-snap switches, proved headlessly through the app's command layer and settings model.
# Rows D8-RHINO-VIEW-0042 Ortho, 0043 OrthoAngle, 0044 OrthoSnapToCPlaneZ, 0083 SetOrtho, 0085 SetSnap, 0086 Snap, 0087 SnapSize,
# 0088 SnapToLocked, 0089 SnapToMeshes, 0091 SnapToOccluded, 0093 SmartTrack, 0009 DisableOsnap, 0063 ProjectOsnap.
# Oracle: a truth table written here by hand: from a known state each of Off / bare / Toggle / On / Status gives off, flipped, flipped again, on,
# unchanged. That Rhino offers the On / Off / Toggle words on these commands, and the 90 degree default ortho angle, is recalled (needs owner check);
# no vendor wording is used.
# Persistence ("settings persist across sessions"): a hand-chosen pattern of nine switches is exported with OptionsExport, every switch is
# flipped, OptionsImport restores them, and the exported JSON file is read back with python for the exact booleans.
# Not tested (needs a real window or a pick): that a switched-off snap really stops a pointer snap, and the hash marks of OrthoAngle.
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXSNAP (not run on Windows: needs python3 and the Linux headless launch)"
else
UXSNAP_S="$TMPW/uxsnap_script.txt"
{
  echo "@wait 2"
  echo "UXBEGIN_ortho"; for w in Off "" Toggle On Status Maybe Status; do echo "Ortho $w"; done
  echo "UXBEGIN_setortho"; echo "OrthoAngle 90"; for w in Off On Toggle Toggle Off "" Status; do echo "SetOrtho $w"; done
  echo "UXBEGIN_orthoangle"; echo "OrthoAngle 45"; echo "SetOrtho Status"; echo "OrthoAngle 90"
  echo "UXBEGIN_snap"; for w in Off ""; do echo "Snap $w"; done; for w in Off On Toggle Toggle 5; do echo "SetSnap $w"; done; echo "Snap Status"; echo "SetSnap Maybe"
  echo "UXBEGIN_snapsize"; for w in 2.5 0 -3 1; do echo "SnapSize $w"; done
  for c in OrthoSnapToCPlaneZ SnapToLocked SnapToOccluded SnapToMeshes SmartTrack DisableOsnap ProjectOsnap; do
    echo "UXBEGIN_$c"; for w in Off "" Toggle On Status; do echo "$c $w"; done
  done
  echo "UXBEGIN_patternA"
  for c in "SnapToLocked Off" "SnapToOccluded Off" "SnapToMeshes Off" "ProjectOsnap On" "OrthoSnapToCPlaneZ On" "SmartTrack On" "DisableOsnap Off" "Ortho On" "Snap On"; do echo "$c"; done
  echo "OptionsExport $TMPW/uxsnap_a.json"
  echo "UXBEGIN_flip"
  for c in "SnapToLocked On" "SnapToOccluded On" "SnapToMeshes On" "ProjectOsnap Off" "OrthoSnapToCPlaneZ Off" "SmartTrack Off" "DisableOsnap On" "Ortho Off" "Snap Off"; do echo "$c"; done
  echo "OptionsImport $TMPW/uxsnap_a.json"
  echo "UXBEGIN_restored"
  for c in SnapToLocked SnapToOccluded SnapToMeshes ProjectOsnap OrthoSnapToCPlaneZ SmartTrack DisableOsnap Ortho Snap; do echo "$c Status"; done
  echo "UXBEGIN_end"
} > "$UXSNAP_S"
rm -f "$TMPW/uxsnap_a.json"
# The switches this section leaves on are saved to settings.json when the app exits; every later launch of the suite that shares the config directory
# (the pointer-driven ui_script of BASE, for one) would start with ortho and grid snap on. So the app runs with a config directory of its own.
mkdir -p "$TMPW/uxsnap_cfg"
if ux_launch "$UXSNAP_S" XDG_CONFIG_HOME="$TMPW/uxsnap_cfg" HOME="$TMPW/uxsnap_cfg"; then
  P=UXSNAP_Ortho
  ux_eq $P ortho "Off, bare (toggle), Toggle, On, Status give off/on/off/on/on; an unknown word warns and changes nothing" \
    "Ortho off" "Ortho on" "Ortho off" "Ortho on" "Ortho on" "! Ortho: expected On, Off, Toggle or Status, got 'maybe'" "Ortho on"
  P=UXSNAP_SetOrtho
  ux_eq $P setortho "turns ortho on, off or toggles it; with no word it turns ortho on" \
    "Ortho angle = 90 deg" "Ortho off (angle 90 deg)" "Ortho on (angle 90 deg)" "Ortho off (angle 90 deg)" "Ortho on (angle 90 deg)" "Ortho off (angle 90 deg)" "Ortho on (angle 90 deg)" "Ortho on (angle 90 deg)"
  P=UXSNAP_OrthoAngle
  ux_eq $P orthoangle "the angle typed is the angle ortho reports, and 90 degrees is restorable" \
    "Ortho angle = 45 deg" "Ortho on (angle 45 deg)" "Ortho angle = 90 deg"
  P=UXSNAP_SetSnap
  ux_eq $P snap "Snap and SetSnap On/Off/Toggle give off/on/off/on/off/on, SetSnap 5 turns grid snap on at size 5, Status reports, an unknown word warns" \
    "Grid snap off" "Grid snap on" "Grid snap off" "Grid snap on" "Grid snap off" "Grid snap on" "Grid snap on, size = 5" "Grid snap on" "! Grid snap: expected On, Off, Toggle or Status, got 'maybe'"
  P=UXSNAP_SnapSize
  ux_eq $P snapsize "a positive spacing is accepted, zero and a negative one are refused and the spacing is kept" \
    "Grid snap size = 2.5" "! SnapSize: the grid snap size must be greater than zero (kept 2.5)" "! SnapSize: the grid snap size must be greater than zero (kept 2.5)" "Grid snap size = 1"
  for pair in "OrthoSnapToCPlaneZ|Ortho snap to CPlane Z" "SnapToLocked|Snap to locked objects" "SnapToOccluded|Snap to occluded objects" "SnapToMeshes|Snap to meshes" "SmartTrack|SmartTrack" "DisableOsnap|Osnaps disabled" "ProjectOsnap|Project osnap"; do
    c="${pair%%|*}"; l="${pair#*|}"
    ux_eq UXSNAP_$c $c "Off, bare (toggle), Toggle, On, Status give off/on/off/on/on" "$l off" "$l on" "$l off" "$l on" "$l on"
  done
  ux_eq UXSNAP_persist restored "OptionsImport restores all nine switches exactly as exported (pattern A)" \
    "Snap to locked objects off" "Snap to occluded objects off" "Snap to meshes off" "Project osnap on" "Ortho snap to CPlane Z on" "SmartTrack on" "Osnaps disabled off" "Ortho on" "Grid snap on"
  if [ -f "$TMPW/uxsnap_a.json" ]; then
    UXSNAP_J="$(python3 - "$TMPW/uxsnap_a.json" <<'PYEOF'
import json, sys
s = json.load(open(sys.argv[1]))["snaps"]
want = {"snap_to_locked": False, "snap_to_occluded": False, "snap_to_meshes": False, "project": True, "ortho_snap_to_cplane_z": True,
        "smart_track": True, "disable_all": False, "ortho": True, "grid_snap": True}
bad = [k for k, v in want.items() if s.get(k) is not v]
print("match" if not bad else "differs: " + ",".join(bad))
PYEOF
)"
    if [ "$UXSNAP_J" = match ]; then ux_ok "UXSNAP_persist: the exported settings file holds the nine switches as booleans exactly as set"; else ux_bad "UXSNAP_persist: exported file $UXSNAP_J"; fi
  else ux_bad "UXSNAP_persist: OptionsExport did not write $TMPW/uxsnap_a.json"; fi
  if grep -q '^FAIL' <<<"$UX_OUT"; then ux_bad "UXSNAP_script: an @expect line failed"; fi
fi
fi
