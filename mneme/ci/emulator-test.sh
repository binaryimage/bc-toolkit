#!/usr/bin/env bash
# Screenshots every Mneme screen and dialog.   usage: emulator-test.sh <apk> <outdir>
set -u
APK="$1"; OUT="$2"; mkdir -p "$OUT"; rm -f "$OUT"/*.png
PKG=com.binaryimage.mneme
MAIN="$PKG/.MainActivity"
SONG="$PKG/.SongActivity"
log() { echo "[$(date +%T)] $*"; }
sh_() { adb shell "$@" | tr -d '\r'; }
shot() { sleep 1; adb exec-out screencap -p > "$OUT/$1.png"; log "screenshot $1"; }
dump() { sh_ uiautomator dump /sdcard/ui.xml > /dev/null 2>&1; adb pull /sdcard/ui.xml /tmp/ui.xml > /dev/null 2>&1; }
center_of() {   # match text or content-desc; "prefix:" for a prefix match
  dump
  python3 - "$1" <<'PY'
import re, sys, xml.etree.ElementTree as ET
want = sys.argv[1]
try:
    root = ET.parse('/tmp/ui.xml').getroot()
except Exception:
    sys.exit(0)
for n in root.iter('node'):
    for t in ((n.get('text') or ''), (n.get('content-desc') or '')):
        ok = t.lower().startswith(want[7:].lower()) if want.startswith('prefix:') else t == want
        if ok and t:
            x1, y1, x2, y2 = map(int, re.findall(r'\d+', n.get('bounds')))
            print((x1 + x2) // 2, (y1 + y2) // 2)
            sys.exit(0)
PY
}
tap_on() { local c; c=$(center_of "$1"); if [ -n "$c" ]; then sh_ input tap $c; log "tapped $1"; return 0; else log "NOT FOUND: $1"; return 1; fi; }
texts() { dump; grep -o -E '(text|content-desc)="[^"]+"' /tmp/ui.xml > "$OUT/$1.txt"; }
song() { sh_ am start -S -n "$SONG" --es song "$@" > /dev/null; sleep 4; }
back() { sh_ input keyevent KEYCODE_BACK; sleep 1; }

adb wait-for-device
until [ "$(sh_ getprop sys.boot_completed)" = "1" ]; do sleep 2; done
sleep 20
sh_ settings put global hide_error_dialogs 1
sh_ settings put system screen_off_timeout 1800000
sh_ input keyevent KEYCODE_WAKEUP; sh_ wm dismiss-keyguard 2>/dev/null
sleep 5
adb logcat -c
adb install -r "$APK" || exit 1

# ---- home
sh_ am start -S -n "$MAIN" --ez ci_seed true > /dev/null; sleep 6
shot 01-home; texts 01-home
sh_ am start -S -n "$MAIN" --ez ci_new true > /dev/null; sleep 4
shot 02-new-song-dialog; back; back
sh_ am start -S -n "$MAIN" --ez ci_menu true > /dev/null; sleep 4
shot 03-song-long-press-menu; back
# quick line on home
sh_ am start -S -n "$MAIN" > /dev/null; sleep 3
tap_on "Quick line…" && { sh_ input text "Rust%son%sthe%sswing%sset"; sleep 1; back; tap_on "ADD"; sleep 2; shot 04-home-after-quick-line; }

# ---- song tabs
song demo --es tab WRITE --ei ci_cursor 0
shot 10-write; texts 10-write
song demo --es tab WRITE --ei ci_cursor 40 --es ci_rhyme time
sleep 4; shot 11-write-rhyme-sheet
song demo --es tab WRITE --ei ci_cursor 99999 --es ci_say "new_verse/the_radio_is_talking_to_the_walls/and_i_let_it/chorus/nobody_calls"
sleep 4; shot 13-write-after-dictation
song demo --es tab CHECK
sleep 3; shot 14-check; texts 14-check
sh_ input swipe 540 1700 540 600 400; sleep 1; shot 15-check-scrolled
song demo --es tab CHECK --es ci_rhyme hurt
sleep 4; shot 16-check-rhyme-sheet
song demo --es tab SING
shot 17-sing; texts 17-sing
song demo --es tab SING --ez ci_scroll true
sleep 6; shot 18-sing-scrolling
song demo --es tab CLIPS
shot 19-clips; texts 19-clips
song demo --es tab CLIPS --ez ci_play true
sleep 1; shot 20-clips-playing
song demo --es tab CLIPS --ez ci_clipmenu true
sleep 2; shot 21-clip-menu
tap_on "Set section" && { sleep 2; shot 22-clip-set-section; back; }
song demo --es tab WRITE --ez ci_share true
sleep 2; shot 23-share-menu
tap_on "Save .txt to Downloads"; sleep 2; shot 24-saved-txt
sh_ ls -la /sdcard/Download/Mneme > "$OUT/downloads.txt" 2>&1
# ---- import that .txt back through the real file picker
sh_ am start -S -n "$MAIN" > /dev/null; sleep 3
tap_on "IMPORT .TXT"; sleep 5; shot 24b-file-picker; texts 24b-file-picker
tap_on "prefix:Demo Song - Lyrics" || { tap_on "Downloads" && sleep 3 && tap_on "Mneme" && sleep 3 && tap_on "prefix:Demo Song - Lyrics"; }
sleep 5; shot 24c-imported-song
sh_ am start -S -n "$MAIN" > /dev/null; sleep 3; shot 24d-home-after-import
song ideas --es tab WRITE
shot 25-ideas-write
song ideas --es tab CLIPS
shot 26-ideas-clips

# ---- MIC + HUM on a real tap (emulator mic is a fake input)
sh_ pm grant $PKG android.permission.RECORD_AUDIO
song demo --es tab WRITE --ei ci_cursor 99999
tap_on "MIC button"; sleep 3; shot 27-mic-on; texts 27-mic-on
tap_on "MIC button"; sleep 1
tap_on "HUM button"; sleep 4; shot 28-hum-recording; texts 28-hum-recording
tap_on "HUM button"; sleep 2; shot 29-hum-saved
song demo --es tab CLIPS
shot 30-clips-after-hum
sh_ am start -S -n "$MAIN" > /dev/null; sleep 3; shot 31-home-end

adb logcat -d | grep -i -E "mneme|AndroidRuntime|FATAL" | tail -300 > "$OUT/logcat.txt"
grep -c "FATAL EXCEPTION" "$OUT/logcat.txt" > "$OUT/crashes.txt" || true
ls -la "$OUT"
