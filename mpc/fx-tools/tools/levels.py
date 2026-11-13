#!/usr/bin/env python3
"""levels.py <plugin.so> <levels.json> [target_db] [fx|synth] - measure every program and update the
level trims so each lands near target (fx: loudness change vs the dry loop; synth: peak dB)."""
import json, os, re, subprocess, sys
so, path = sys.argv[1], sys.argv[2]
target = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
mode = sys.argv[4] if len(sys.argv) > 4 else 'fx'
old = json.load(open(path)) if os.path.exists(path) else {}
if mode == 'fx':
    out = subprocess.run(['/home/claude/fx-shared/fxrender', so, '-', '8'], capture_output=True, text=True).stdout
    rx = re.compile(r'^\s*\d+ (.+?)\s+gain\s+([-+\d.]+) dB')
else:
    out = subprocess.run(['/home/claude/janus-tools/render', so, '/tmp/lv_render'], capture_output=True, text=True).stdout
    rx = re.compile(r'^p99 [\d.]+%\s+\d+ (.+?)\s+peak\s+([-\d.]+) dB')
new = {}
worst = 0
for line in out.splitlines():
    m = rx.match(line)
    if not m: continue
    name, g = m.group(1).strip(), float(m.group(2))
    new[name] = round(old.get(name, 0.0) + (target - g), 1)
    worst = max(worst, abs(target - g))
json.dump(new, open(path, 'w'), indent=0)
print(f'{len(new)} programs, worst off-target {worst:.1f} dB')
