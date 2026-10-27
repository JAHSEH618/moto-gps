#!/bin/bash
# Build, install and launch MOTO GPS on a physical iPhone.
#
# Signing values stay off the repo files: they are passed to xcodebuild on the
# command line, so project.yml keeps its placeholder bundle id.
#
#   DEVICE=... TEAM=... ./scripts/ios/run_on_device.sh
# Optional: BUNDLE_ID=... DD=...
set -euo pipefail

DEVICE="${DEVICE:?set DEVICE to the target iPhone UDID}"
TEAM="${TEAM:?set TEAM to the Apple developer team id}"
BUNDLE_ID="${BUNDLE_ID:-com.example.motogps}"
DD="${DD:-/tmp/moto-dev-build}"

cd "$(dirname "$0")/../.."

if xcrun devicectl device info details --device "$DEVICE" 2>&1 | grep -q "Developer Mode Status: Disabled"; then
  echo "error: Developer Mode is off on the device. Enable it, reboot, then rerun." >&2
  exit 2
fi

xcodegen generate --spec platforms/ios/project.yml

xcodebuild \
  -project platforms/ios/MotoGPS.xcodeproj \
  -scheme MotoGPS \
  -configuration Debug \
  -destination "platform=iOS,id=$DEVICE" \
  -derivedDataPath "$DD" \
  -allowProvisioningUpdates \
  DEVELOPMENT_TEAM="$TEAM" \
  PRODUCT_BUNDLE_IDENTIFIER="$BUNDLE_ID" \
  build

APP="$DD/Build/Products/Debug-iphoneos/MOTO GPS.app"
xcrun devicectl device install app --device "$DEVICE" "$APP"
xcrun devicectl device process launch --device "$DEVICE" "$BUNDLE_ID"
