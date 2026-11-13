#!/usr/bin/env python3
"""newfx.py <Name> <uid> <kind> <description> - write plugin.json for one of binaryimage's MPC plugins
and copy the shared bi_fx.h into its src/."""
import json, os, shutil, sys
name, uid, kind, desc = sys.argv[1:5]
d = f'/home/claude/ref/sdk/plugins/{name}'
os.makedirs(f'{d}/src', exist_ok=True)
slug = name.lower().replace(' ', '-')
inst = kind == 'instrument'
json.dump({"name": name, "manufacturer": "binaryimage", "version": "1.0.0", "format": "vst2", "kind": kind, "uid": uid,
           "numInputs": 0 if inst else 2, "numOutputs": 2, "category": "Synth" if inst else "Effect",
           "file32": f"VST/{slug}-arm32.so", "file64": f"VST/{slug}-arm64.so", "description": desc, "author": "binaryimage",
           "url": f"https://github.com/binaryimage/bc-toolkit/tree/mpc-plugins/mpc/{slug}", "license": "MIT"},
          open(f'{d}/plugin.json', 'w'), indent=2)
shutil.copy('/home/claude/fx-shared/bi_fx.h', f'{d}/src/bi_fx.h')
print('ok', d)
