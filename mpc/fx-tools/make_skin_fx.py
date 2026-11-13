#!/usr/bin/env python3
"""make_skin_fx.py <plugin dir> <style> - generate the skin with the SDK's gen_ui.py, then put the
style's background behind every page. Writes <plugin dir>/skins/ (build.py copies it as-is).
Needs <plugin dir>/build/params.json, so build once first; re-run after changing params or ui.json."""
import json, os, shutil, subprocess, sys
SDK = '/home/claude/ref/sdk'
proj, style = os.path.abspath(sys.argv[1]), sys.argv[2]
tmp = f'/tmp/skin_gen_{style}'
shutil.rmtree(tmp, ignore_errors=True)
subprocess.run(['python3', 'tools/gen_ui.py', '--params', f'{proj}/build/params.json', '--ui', f'{proj}/ui.json',
                '--out', tmp, '--kit-cache', f'{proj}/build', '--quiet'], cwd=SDK, check=True)
bg = f'{style}_bg.png'
subprocess.run(['python3', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_bg_fx.py'), style, os.path.join(tmp, bg)],
               check=True, capture_output=True)
path = os.path.join(tmp, 'TUI.json')
tui = json.load(open(path))
for tab in tui['pageData']['tabs']:
    b = tab['componentDefinition']['backgroundData']
    for state in ('focussed', 'unfocussed'):
        b[state]['image'] = bg
json.dump(tui, open(path, 'w'), indent=1)
dst = os.path.join(proj, 'skins')
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(tmp, dst)
print('skin ->', dst, len(os.listdir(dst)), 'files')
