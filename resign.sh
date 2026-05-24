#!/bin/bash
# resign.sh — re-sign ASFWDriver.dext with Apple Development cert after ad-hoc Xcode build.
# Required because Personal Team cannot provision DriverKit entitlements, so Xcode signs
# ad-hoc, but DriverKit exec path on Apple Silicon requires a real Apple Development cert.
# amfi_get_out_of_my_way=1 boot-arg bypasses entitlement validation at runtime.
#
# Usage: ./resign.sh [Debug|Release]
#   ./resign.sh          — re-signs Debug build (default)
#   ./resign.sh Release  — re-signs Release build

set -euo pipefail

SRCROOT="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${1:-Debug}"
IDENTITY="Apple Development: j.slipiec@gmail.com (239NB3LFDQ)"
ENTITLEMENTS="${SRCROOT}/ASFWDriver/ASFWDriver.entitlements"
DEXT_NAME="net.mrmidi.ASFW.ASFWDriver.dext"

# Locate the dext embedded inside ASFW.app (this is what systemextensionsctl installs).
DEXT=$(find ~/Library/Developer/Xcode/DerivedData/ASFW-*/Build/Products/"${CONFIG}" \
    -name "${DEXT_NAME}" -path "*/ASFW.app/*" 2>/dev/null | head -1)

if [ -z "${DEXT}" ]; then
    echo "ERROR: ${DEXT_NAME} not found in DerivedData/${CONFIG}. Build first."
    exit 1
fi

echo "Dext:         ${DEXT}"
echo "Identity:     ${IDENTITY}"
echo "Entitlements: ${ENTITLEMENTS}"
echo ""

codesign --force \
    --sign "${IDENTITY}" \
    --entitlements "${ENTITLEMENTS}" \
    --timestamp=none \
    "${DEXT}"

echo ""
echo "=== Verification ==="
codesign -dvvv "${DEXT}" 2>&1 | grep -E "^(Identifier|Authority|TeamIdentifier)"
echo ""
echo "Done. Now uninstall the old dext and re-activate from ASFW.app:"
echo "  systemextensionsctl uninstall 4MJNRC8SW5 net.mrmidi.ASFW.ASFWDriver"
