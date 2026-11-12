#!/usr/bin/env python3
"""make_skin.py: generate Maggot's skin with the SDK's gen_ui.py, then put the textured
background behind every page. Writes plugins/Maggot/skins/ (the build copies it as-is).
Re-run after changing parameters or ui.json."""
import json, os, shutil, subprocess
SDK = '/home/claude/ref/sdk'
proj = os.path.join(SDK, 'plugins/Maggot')
tmp = '/tmp/mg_skin_gen'
shutil.rmtree(tmp, ignore_errors=True)
subprocess.run(['python3', 'tools/gen_ui.py', '--params', f'{proj}/build/params.json', '--ui', f'{proj}/ui.json',
                '--out', tmp, '--kit-cache', f'{proj}/build', '--quiet'], cwd=SDK, check=True)
subprocess.run(['python3', '/home/claude/mg-tools/make_bg.py', os.path.join(tmp, 'maggot_bg.png')], check=True, capture_output=True)
path = os.path.join(tmp, 'TUI.json')
tui = json.load(open(path))
for tab in tui['pageData']['tabs']:
    bg = tab['componentDefinition']['backgroundData']
    for state in ('focussed', 'unfocussed'):
        bg[state]['image'] = 'maggot_bg.png'
json.dump(tui, open(path, 'w'), indent=1)
dst = os.path.join(proj, 'skins')
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(tmp, dst)
print('skin ->', dst, sorted(os.listdir(dst)))
