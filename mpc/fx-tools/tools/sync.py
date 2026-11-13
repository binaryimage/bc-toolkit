#!/usr/bin/env python3
"""sync.py <Name> <dir> - copy a finished plugin project into the bc-toolkit repo (mpc/<dir>) and the
shared tools into mpc/fx-tools."""
import os, shutil, sys
name, d = sys.argv[1], sys.argv[2]
src = f'/home/claude/ref/sdk/plugins/{name}'
dst = f'/home/claude/bc-toolkit/mpc/{d}'
os.makedirs(dst, exist_ok=True)
for f in ('plugin.json', 'ui.json', 'README.md'):
    shutil.copy(f'{src}/{f}', f'{dst}/{f}')
for sub in ('src', 'skins'):
    shutil.rmtree(f'{dst}/{sub}', ignore_errors=True)
    if os.path.isdir(f'{src}/{sub}'): shutil.copytree(f'{src}/{sub}', f'{dst}/{sub}')
t = '/home/claude/bc-toolkit/mpc/fx-tools'
shutil.rmtree(t, ignore_errors=True)
shutil.copytree('/home/claude/fx-shared', t, ignore=shutil.ignore_patterns('bg', 'fxrender', '*.png', '__pycache__'))
print('synced', name, '->', dst)
