#!/usr/bin/env bash
# Developer ID signing and notarization of the CPack-built Dino8 .dmg.
# macOS only; called from .github/workflows/dino8-app.yml, and only when the
# Apple signing secrets exist. See docs/CODE_SIGNING.md.
#
#   sign-dmg.sh sign     <dmg>   re-sign the app INSIDE the disk image, then the image
#   sign-dmg.sh notarize <dmg>   submit to Apple's notary service, staple, verify
#   sign-dmg.sh adhoc    <dmg>   CI SELF-TEST ONLY: the same code path as `sign`, but with the
#                                ad-hoc identity (`codesign --sign -`, needs no certificate and
#                                no secret) and no secure timestamp. The result is NOT
#                                Developer-ID signed and Gatekeeper does not trust it. Refuses
#                                to run unless the .dmg sits in a directory containing the
#                                marker file .dino8-test-signing-dir (a scratch COPY made by
#                                test-adhoc-signing.sh), so the published image stays as built.
#
# Why "inside the image": CPack's DragNDrop generator installs a fresh copy of
# the app into its own staging folder (copying the binary and adding Resources
# afterwards), so signing build/Dino8.app before `cpack` signs a bundle that is
# never shipped, and any seal made before the Resources are added would not
# match anyway. The .dmg is therefore unpacked, the app signed in place
# (inside-out: nested Mach-O files first, then the bundle, with hardened
# runtime, secure timestamp and entitlements), and the image rebuilt under the
# same file name and volume name.
#
# sign needs:       APPLE_SIGNING_IDENTITY  (e.g. "Developer ID Application: Name (TEAMID)")
#                   the identity's certificate in the default keychain search list
# notarize needs:   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_SPECIFIC_PASSWORD
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
entitlements="$here/entitlements.plist"
mode=${1:-}
dmg=${2:-}
[ -n "$mode" ] && [ -f "$dmg" ] || { echo "usage: $0 sign|notarize|adhoc <dmg>" >&2; exit 2; }
dmg=$(cd "$(dirname "$dmg")" && pwd)/$(basename "$dmg")

sign() {
  # Identity and timestamp are the ONLY differences between the production
  # `sign` mode and the `adhoc` self-test: everything else below is shared.
  local identity ts_flag
  if [ "$mode" = "adhoc" ]; then
    [ -f "$(dirname "$dmg")/.dino8-test-signing-dir" ] \
      || { echo "adhoc: refusing to sign $dmg - its directory has no .dino8-test-signing-dir marker (test mode only signs scratch copies)" >&2; exit 2; }
    echo "*** AD-HOC TEST SIGNING (codesign --sign -): NOT Developer-ID signed, NOT notarized, not trusted by Gatekeeper ***"
    identity="-"
    ts_flag=""   # an ad-hoc signature has no certificate to timestamp
  else
    : "${APPLE_SIGNING_IDENTITY:?APPLE_SIGNING_IDENTITY is empty}"
    identity="$APPLE_SIGNING_IDENTITY"
    ts_flag="--timestamp"
  fi
  local stage mnt app vol main details
  stage=$(mktemp -d "${RUNNER_TEMP:-/tmp}/dino8-dmg-stage.XXXXXX")
  mnt=$(mktemp -d "${RUNNER_TEMP:-/tmp}/dino8-dmg-mnt.XXXXXX")
  vol=$(basename "$dmg" .dmg)   # CPack's default volume name

  # the image carries a license agreement (the MIT text): hdiutil asks to agree on a pager and cancels without a terminal
  printf "Y\n" | PAGER=true hdiutil attach "$dmg" -readonly -nobrowse -noverify -mountpoint "$mnt"
  ditto "$mnt" "$stage"
  hdiutil detach "$mnt"

  app="$stage/Dino8.app"
  [ -d "$app" ] || { echo "Dino8.app not found inside $dmg" >&2; ls -la "$stage" >&2; exit 1; }
  main="$app/Contents/MacOS/Dino8"

  # The build copies the data files next to the executable (Contents/MacOS/data, for running from the build tree) and the
  # install step ALSO puts them in Contents/Resources/data. codesign treats everything under Contents/MacOS as code and refuses
  # the unsigned data files there ("code object is not signed at all ... hatchpatterns.pat"). The app looks in exe_dir/data first
  # and then in ../Resources/data (Application.cpp), so the bundle's own copy is the one that matters: drop the duplicate.
  if [ -d "$app/Contents/Resources/data" ] && [ -d "$app/Contents/MacOS/data" ]; then
    echo "removing the duplicate Contents/MacOS/data (the same files are in Contents/Resources/data)"
    rm -rf "$app/Contents/MacOS/data"
  fi

  # Nested code first (anything Mach-O that is not the main executable).
  while IFS= read -r f; do
    [ "$f" = "$main" ] && continue
    if file -b "$f" | grep -q 'Mach-O'; then
      echo "signing nested code: ${f#"$stage"/}"
      # shellcheck disable=SC2086  # ts_flag is deliberately unquoted: empty or one flag
      codesign --force --options runtime $ts_flag --sign "$identity" "$f"
    fi
  done < <(find "$app/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \))

  # The bundle (this also signs the main executable, with the entitlements).
  # shellcheck disable=SC2086
  codesign --force --options runtime $ts_flag \
    --entitlements "$entitlements" --sign "$identity" "$app"

  echo "--- verifying the signed app"
  codesign --verify --deep --strict --verbose=2 "$app"
  details=$(codesign -dvv "$app" 2>&1)
  printf '%s\n' "$details"
  printf '%s\n' "$details" | grep -q 'flags=.*runtime' \
    || { echo "hardened runtime flag missing from the signature" >&2; exit 1; }

  rm -f "$dmg"
  hdiutil create -volname "$vol" -srcfolder "$stage" -ov -format UDZO "$dmg"
  # shellcheck disable=SC2086
  codesign --force $ts_flag --sign "$identity" "$dmg"
  codesign --verify --verbose=2 "$dmg"
  rm -rf "$stage" "$mnt"
  echo "signed: $dmg"
}

notarize() {
  : "${APPLE_ID:?APPLE_ID is empty}" "${APPLE_TEAM_ID:?APPLE_TEAM_ID is empty}" "${APPLE_APP_SPECIFIC_PASSWORD:?APPLE_APP_SPECIFIC_PASSWORD is empty}"
  local out id
  # notarytool can exit 0 for an "Invalid" verdict, so the verdict is read
  # from its JSON output rather than trusted from the exit status.
  out=$(xcrun notarytool submit "$dmg" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
          --password "$APPLE_APP_SPECIFIC_PASSWORD" --wait --output-format json) || true
  echo "$out"
  id=$(printf '%s' "$out" | grep -o '"id"[[:space:]]*:[[:space:]]*"[^"]*"' | head -1 | sed 's/.*"\([^"]*\)"$/\1/')
  if ! printf '%s' "$out" | grep -q '"status"[[:space:]]*:[[:space:]]*"Accepted"'; then
    echo "notarization was NOT accepted" >&2
    if [ -n "$id" ]; then
      xcrun notarytool log "$id" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_SPECIFIC_PASSWORD" >&2 || true
    fi
    exit 1
  fi
  xcrun stapler staple "$dmg"
  xcrun stapler validate "$dmg"
  # Gatekeeper's own assessment of the disk image (what a user's Mac does).
  spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
  echo "notarized and stapled: $dmg"
}

case "$mode" in
  sign|adhoc) sign ;;
  notarize) notarize ;;
  *) echo "unknown mode: $mode" >&2; exit 2 ;;
esac
