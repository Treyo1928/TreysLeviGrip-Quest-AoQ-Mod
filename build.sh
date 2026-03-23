#!/usr/bin/env bash
# build.sh — Build liblevigrip.so and optionally push to Quest via ADB
#
# Must be run with bash, from the liblevigrip-quest/ directory:
#   bash build.sh [--push] [--logs]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NDK_BUILD="/home/treyb/QuestCompilation/android-ndk-r26d/ndk-build"
MODS_DIR="/sdcard/Android/data/com.AoQ.AttackOnQuest/files/mods"

DO_PUSH=0
DO_LOGS=0
for arg in "$@"; do
    case "$arg" in
        --push) DO_PUSH=1 ;;
        --logs) DO_LOGS=1 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

cd "$SCRIPT_DIR"

echo "Building..."
"$NDK_BUILD" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./jni/Android.mk APP_ABI=armeabi-v7a

SO="libs/armeabi-v7a/liblevigrip.so"
SIZE=$(stat -c%s "$SO")
echo "Built: $SO ($SIZE bytes)"

if [[ "$DO_PUSH" -eq 1 ]]; then
    echo ""
    echo "Pushing to Quest..."
    adb push "$SO" "$MODS_DIR/"
    echo "Pushed."
fi

if [[ "$DO_LOGS" -eq 1 ]]; then
    echo ""
    echo "Starting logcat (Ctrl-C to stop)..."
    adb logcat -c
    adb logcat -s LeviGrip,AoQModManager,QuestHook
fi
