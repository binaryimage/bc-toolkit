#!/usr/bin/env bash
# Runs inside the Android emulator job: installs the APK, starts the app in self-test mode, waits for it to
# finish (screenshots of every tab, the button check, real MediaCodec exports), then pulls the results.
#   emulator-test.sh <apk> <results folder>
set -u
APK="$1"
OUT="$2"
PKG=com.binaryimage.bcvisualizer
DEV_DIR="/sdcard/Download/BC Visualizer selftest"
mkdir -p "$OUT"

adb wait-for-device
adb shell getprop ro.build.version.release > "$OUT/android-version.txt"
adb shell rm -rf "'$DEV_DIR'"
adb install -r -g "$APK" | tee "$OUT/install.txt"
adb shell setprop debug.bcv.selftest 1
adb logcat -c
adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 > /dev/null
sleep 8
adb exec-out screencap -p > "$OUT/emulator-start.png"

# up to 25 minutes for the layout pass and the exports (the emulator is slow, and its encoders are software)
status=timeout
for i in $(seq 1 300); do
  if adb shell "test -f '$DEV_DIR/done.txt'" 2>/dev/null; then status=done; break; fi
  if ! adb shell pidof "$PKG" > /dev/null 2>&1; then
    sleep 3
    adb shell "test -f '$DEV_DIR/done.txt'" 2>/dev/null && { status=done; break; }
    status=crashed; break
  fi
  if [ $((i % 12)) -eq 0 ]; then adb exec-out screencap -p > "$OUT/emulator-running.png"; fi
  sleep 5
done
echo "$status" > "$OUT/emulator-status.txt"
adb exec-out screencap -p > "$OUT/emulator-end.png"
adb pull "$DEV_DIR" "$OUT/device" > /dev/null 2>&1 || true
adb logcat -d -v time > "$OUT/logcat-full.txt" 2>/dev/null || true
grep -iE "juce|bcv|AndroidRuntime|DEBUG|libc|MediaCodec|CCodec" "$OUT/logcat-full.txt" | tail -n 400 > "$OUT/logcat.txt" || true
rm -f "$OUT/logcat-full.txt"
adb shell am force-stop "$PKG" || true

echo "emulator self test: $status"
cat "$OUT/device/done.txt" 2>/dev/null || true
cat "$OUT/device/selftest.txt" 2>/dev/null || true
[ "$status" = done ] && grep -q "^exit 0" "$OUT/device/done.txt"
