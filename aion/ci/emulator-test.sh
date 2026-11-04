#!/usr/bin/env bash
# Renders the style gallery, screenshots every editor tab, pins the widget to the home screen
# and screenshots it with a few different styles.   usage: emulator-test.sh <apk> <outdir>
set -u
APK="$1"; OUT="$2"; mkdir -p "$OUT"
PKG=com.binaryimage.aion
ACT="$PKG/.EditorActivity"
EXT=/sdcard/Android/data/$PKG/files
log() { echo "[$(date +%T)] $*"; }
sh_() { adb shell "$@" | tr -d '\r'; }
shot() { clear_dialogs; adb exec-out screencap -p > "$OUT/$1.png"; log "screenshot $1"; }
center_of() {
  sh_ uiautomator dump /sdcard/ui.xml > /dev/null 2>&1
  adb pull /sdcard/ui.xml /tmp/ui.xml > /dev/null 2>&1
  python3 - "$1" <<'PY'
import re, sys, xml.etree.ElementTree as ET
want = sys.argv[1]
try:
    root = ET.parse('/tmp/ui.xml').getroot()
except Exception:
    sys.exit(0)
for n in root.iter('node'):
    t = (n.get('text') or '')
    ok = t.lower().startswith(want[7:].lower()) if want.startswith('prefix:') else t == want
    if ok:
        x1, y1, x2, y2 = map(int, re.findall(r'\d+', n.get('bounds')))
        print((x1 + x2) // 2, (y1 + y2) // 2)
        break
PY
}
clear_dialogs() { local w; w=$(center_of "Wait"); [ -n "$w" ] && { sh_ input tap $w; sleep 2; }; }
tap_on() { clear_dialogs; local c; c=$(center_of "$1"); if [ -n "$c" ]; then sh_ input tap $c; log "tapped $1"; return 0; else log "not found: $1"; return 1; fi; }
ui_texts() { sh_ uiautomator dump /sdcard/ui.xml > /dev/null 2>&1; adb pull /sdcard/ui.xml /tmp/ui3.xml > /dev/null 2>&1; grep -o 'text="[^"]*"' /tmp/ui3.xml | head -40; }

adb wait-for-device
until [ "$(sh_ getprop sys.boot_completed)" = "1" ]; do sleep 2; done
sleep 20
sh_ settings put global hide_error_dialogs 1
sh_ settings put system screen_off_timeout 1800000
sh_ input keyevent KEYCODE_WAKEUP; sh_ wm dismiss-keyguard 2>/dev/null
sleep 10
adb logcat -c
adb install -r "$APK" || exit 1

# ---------- gallery of every option, rendered by the app itself
sh_ am start -n "$ACT" --ez ci_gallery true
for i in $(seq 1 120); do
  if sh_ ls "$EXT/gallery/done.txt" 2>/dev/null | grep -q done; then break; fi
  sleep 3
done
mkdir -p "$OUT/gallery"
adb pull "$EXT/gallery/." "$OUT/gallery/" > /dev/null 2>&1 || log "gallery pull failed"
cat "$OUT/gallery/done.txt" 2>/dev/null; echo

# ---------- editor tabs
for t in Presets Face Case Marks Numerals Hands Lettering Extras; do
  sh_ am start -S -n "$ACT" --es tab "$t" --ei preset 1 > /dev/null
  sleep 6
  shot "editor-$(echo $t | tr 'A-Z' 'a-z')"
done

# ---------- put the widget on the home screen (restart the launcher first: it tends to ANR on the emulator)
sh_ am force-stop com.google.android.apps.nexuslauncher
sh_ input keyevent KEYCODE_HOME; sleep 15; clear_dialogs; sleep 2; clear_dialogs
sh_ am start -S -n "$ACT" --ez ci_pin true --ei preset 1 > /dev/null
sleep 5
ui_texts > "$OUT/pin-dialog.txt"
shot pin-dialog
tap_on "prefix:Add to home screen" || tap_on "prefix:ADD TO HOME SCREEN" || tap_on "Add" || tap_on "ADD"
sleep 3
sh_ input keyevent KEYCODE_HOME; sleep 12; clear_dialogs; sleep 4
shot home-1-diver
echo "widgets: $(sh_ dumpsys appwidget | grep -c 'com.binaryimage.aion')" > "$OUT/appwidget.txt"
sh_ dumpsys appwidget | grep -A6 -i aion | head -60 >> "$OUT/appwidget.txt"

n=2
for p in 4 6 0 30 5 14; do
  sh_ am start -S -n "$ACT" --ez ci_apply true --ei preset $p > /dev/null
  sleep 9
  sh_ input keyevent KEYCODE_HOME; sleep 5
  shot "home-$n-preset$p"
  n=$((n+1))
done
sleep 3; shot home-later

adb logcat -d | grep -i -E "aion|AndroidRuntime|AnalogClock|RemoteViews|FATAL|TransactionTooLarge|FAILED BINDER|AppWidget" | tail -300 > "$OUT/logcat.txt"
ls -la "$OUT" "$OUT/gallery"
