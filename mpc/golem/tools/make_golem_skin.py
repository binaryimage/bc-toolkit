#!/usr/bin/env python3
"""make_golem_skin.py - Golem's skin: ui.json pages through the SDK's gen_ui.py, then
 * a grey ARP-style panel background on every page,
 * the PATCH page rebuilt as a jack panel: every jack is a film strip (dark, or lit in its cable's
   colour with the cable number, or striped when one output feeds several cables) with an
   invisible radio button on top that picks it,
 * a colour chip with the cable number beside every cable control on the AMT and CABLE pages.
Writes plugins/Golem/ui.json and plugins/Golem/skins/. Build once first (needs build/params.json)."""
import colorsys, json, math, os, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np

SDK = '/home/claude/ref/sdk'
proj = os.path.join(SDK, 'plugins/Golem')
tmp = '/tmp/golem_skin'
sys.path.insert(0, '/home/claude/fx-shared/tools')
import mkui

NCAB, NSRC, NDST, MULTI = 30, 25, 20, 31
OUT_LABELS = ["V1 SAW", "V1 SQ", "V2 SAW", "V2 PULSE", "V2 TRI", "V2 SINE", "V3 SAW", "V3 PULSE", "V3 TRI", "V3 SINE",
              "NOISE", "RING", "S&H", "LAG", "ADSR", "AR", "VCF", "SEQ A", "SEQ B", "SEQ C", "CLOCK", "KBD", "VELO", "WHEEL", "AFTER"]
IN_LABELS = ["V1 PITCH", "V2 PITCH", "V3 PITCH", "ALL PITCH", "V2 PW", "V3 PW", "CUTOFF", "RESO", "VCF IN", "VCA",
             "RING IN", "S&H IN", "S&H CLK", "LAG IN", "ADSR GATE", "AR GATE", "SEQ CLK", "SEQ RST", "REVERB", "PAN"]
FONT_B = '/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf'
FONT_R = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

def cable_colours():
    cols = []
    for i in range(NCAB):
        h = (i * 0.61803398875) % 1.0
        v = 1.0 if i % 2 == 0 else 0.82
        s = 0.85 if i % 3 else 0.65
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        cols.append((int(r * 255), int(g * 255), int(b * 255)))
    return cols
COLS = cable_colours()

# ---------------------------------------------------------------- pages
pages = [
    ("GOLEM", ["Volume:fader", "Voice Mode:menu", "Retrig:menu", "Glide",
               "Cutoff", "Resonance", "VCF Type:menu", "Reverb",
               "Seq Mode:menu", "Bend Range", "Velocity", "Pan:bi"]),
    ("PATCH", ["Unplug All:switch"]),
    ("VCO", ["VCO1 Tune:bi", "VCO1 Fine:bi", "VCO1 FM", "Sync:switch",
             "VCO2 Tune:bi", "VCO2 Fine:bi", "VCO2 PW", "VCO2 PWM",
             "VCO3 Tune:bi", "VCO3 Fine:bi", "VCO3 PW", "VCO3 FM",
             "VCO2 FM", "VCO3 LF:switch", "VCO3 KBD:switch", None]),
    ("MIXER", ["VCO1 Level", "VCO1 Wave:menu", "VCO2 Level", "VCO2 Wave:menu",
               "VCO3 Level", "VCO3 Wave:menu", "Noise Level", "Noise Color",
               "Ring Level", "Drive", None, None]),
    ("VCF/VCA", ["Cutoff", "Resonance", "VCF KBD", "VCF Env:bi",
                 "VCF FM", "VCF Type:menu", "VCA Env:menu", "VCA Init"]),
    ("ENV", ["Attack", "Decay", "Sustain", "Release", "AR Attack", "AR Release", None, None]),
    ("S&H/REV", ["S&H Rate", "S&H Sync:menu", "S&H Source:menu", "Lag",
                 "Reverb", "Rev Decay", "Rev Tone", "Pan:bi"]),
    ("SEQ", ["Seq Mode:menu", "Seq Rate:menu", "Seq Free Hz", "Seq Length",
             "Seq Dir:menu", "Quantize:menu", "Seq Range", "Seq Gate",
             "Seq Transpose:switch", None, None, None]),
    ("SEQ A", [f"A{i}" for i in range(1, 17)]),
    ("SEQ B", [f"B{i}" for i in range(1, 17)]),
    ("SEQ C", [f"C{i}" for i in range(1, 17)]),
    ("STEPS", [f"Step {i}:menu" for i in range(1, 17)]),
    ("AMT 1-15", [f"C{i} Amt:bi" for i in range(1, 16)] + [None]),
    ("AMT16-30", [f"C{i} Amt:bi" for i in range(16, 31)] + [None]),
]
for start in (1, 9, 17, 25):
    ctrls = []
    for c in range(start, min(start + 8, NCAB + 1)):
        ctrls += [f"C{c} From:menu", f"C{c} To:menu"]
    ctrls += [None] * (16 - len(ctrls))
    end = min(start + 7, NCAB)
    pages.append((f"CBL{start}-{end}" if start >= 10 else f"CBL {start}-{end}", ctrls))
mkui.write(os.path.join(proj, 'ui.json'), 'ffd9822b', 'ff2a2b2d', 'fff4f4f2', 'ffa9abae', pages)

shutil.rmtree(tmp, ignore_errors=True)
subprocess.run(['python3', 'tools/gen_ui.py', '--params', f'{proj}/build/params.json', '--ui', f'{proj}/ui.json',
                '--out', tmp, '--kit-cache', f'{proj}/build', '--quiet'], cwd=SDK, check=True)
params = json.load(open(f'{proj}/build/params.json'))
plist = params['params'] if isinstance(params, dict) and 'params' in params else params
def pindex(name):
    for i, p in enumerate(plist):
        if p.get('name') == name: return p.get('index', i)
    raise KeyError(name)

# ---------------------------------------------------------------- images
W, H = 1280, 628
def panel(seed):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    base = 44 + 6 * (1 - yy / H)
    img = np.stack([base, base + 1, base + 3], -1)
    brushed = np.asarray(Image.fromarray((rng.random((H // 2, W // 16)) * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32) / 255
    img += (brushed[..., None] - 0.5) * 6
    img += rng.normal(0, 1.6, (H, W, 1))
    vig = 1 - 0.3 * (((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) / 2
    img *= vig[..., None]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB')

def silkscreen(im, wordmark=True):
    if wordmark:
        layer = Image.new('RGBA', im.size, (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
        f = ImageFont.truetype(FONT_B, 170)
        t = 'GOLEM'
        tw = d.textlength(t, font=f)
        d.text(((W - tw) / 2, H - 230), t, font=f, fill=(235, 235, 230, 16))
        im = Image.alpha_composite(im.convert('RGBA'), layer).convert('RGB')
    return im

os.makedirs(tmp, exist_ok=True)
silkscreen(panel(1)).save(os.path.join(tmp, 'golem_bg.png'), optimize=True)

# jack film strip: frame 0 empty socket, 1..30 lit cable colour + number, 31 split (striped)
J = 64
strip = Image.new('RGBA', (J, J * 32), (0, 0, 0, 0))
fnum = ImageFont.truetype(FONT_B, 22)
for fr in range(32):
    im = Image.new('RGBA', (J * 4, J * 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = J * 2
    if fr == 0:
        d.ellipse([c - 104, c - 104, c + 104, c + 104], fill=(150, 152, 156, 255))
        d.ellipse([c - 84, c - 84, c + 84, c + 84], fill=(70, 72, 76, 255))
        d.ellipse([c - 52, c - 52, c + 52, c + 52], fill=(12, 12, 13, 255))
    elif fr <= NCAB:
        col = COLS[fr - 1]
        glow = Image.new('RGBA', im.size, (0, 0, 0, 0)); dg = ImageDraw.Draw(glow)
        dg.ellipse([c - 124, c - 124, c + 124, c + 124], fill=col + (150,))
        glow = glow.filter(ImageFilter.GaussianBlur(14)); im.alpha_composite(glow); d = ImageDraw.Draw(im)
        d.ellipse([c - 104, c - 104, c + 104, c + 104], fill=col + (255,))
        d.ellipse([c - 74, c - 74, c + 74, c + 74], fill=(18, 18, 20, 255))
    else:   # split output: striped ring of several cable colours
        for k in range(12):
            a0 = k * 30
            d.pieslice([c - 104, c - 104, c + 104, c + 104], a0, a0 + 30, fill=COLS[(k * 7) % NCAB] + (255,))
        d.ellipse([c - 74, c - 74, c + 74, c + 74], fill=(18, 18, 20, 255))
    im = im.resize((J, J), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    if 1 <= fr <= NCAB:
        t = str(fr); tw = d.textlength(t, font=fnum)
        d.text(((J - tw) / 2, J / 2 - 15), t, font=fnum, fill=(255, 255, 255, 255))
    elif fr == MULTI:
        t = '+'; tw = d.textlength(t, font=fnum)
        d.text(((J - tw) / 2, J / 2 - 16), t, font=fnum, fill=(255, 255, 255, 255))
    strip.paste(im, (0, fr * J))
strip.save(os.path.join(tmp, 'jacks.png'), optimize=True)
# picked highlight (button on) and transparent (button off)
pk = Image.new('RGBA', (J * 4, J * 4), (0, 0, 0, 0)); d = ImageDraw.Draw(pk)
d.ellipse([12, 12, J * 4 - 12, J * 4 - 12], outline=(255, 255, 255, 255), width=18)
pk.resize((J, J), Image.LANCZOS).save(os.path.join(tmp, 'jack_pick.png'))
Image.new('RGBA', (J, J), (0, 0, 0, 0)).save(os.path.join(tmp, 'jack_off.png'))
# cable chips
fch = ImageFont.truetype(FONT_B, 17)
for i in range(NCAB):
    ch = Image.new('RGBA', (34 * 4, 34 * 4), (0, 0, 0, 0)); d = ImageDraw.Draw(ch)
    d.ellipse([4, 4, 34 * 4 - 4, 34 * 4 - 4], fill=COLS[i] + (255,))
    ch = ch.resize((34, 34), Image.LANCZOS); d = ImageDraw.Draw(ch)
    t = str(i + 1); tw = d.textlength(t, font=fch)
    d.text(((34 - tw) / 2, 5), t, font=fch, fill=(15, 15, 15, 255))
    ch.save(os.path.join(tmp, f'chip{i + 1}.png'))

# ---------------------------------------------------------------- PATCH page layout
OX, OY, CW, CH = 22, 46, 118, 112          # outputs: 5 x 5
IX, IY = 660, 46                            # inputs: 5 x 4
def out_pos(k): return OX + (k % 5) * CW, OY + (k // 5) * CH
def in_pos(k): return IX + (k % 5) * CW, IY + (k // 5) * CH
pbg = silkscreen(panel(2), wordmark=False).convert('RGBA')
d = ImageDraw.Draw(pbg)
fh = ImageFont.truetype(FONT_B, 22); fl = ImageFont.truetype(FONT_R, 13)
d.text((OX + 4, 10), 'OUTPUTS', font=fh, fill=(240, 240, 236, 230))
d.text((IX + 4, 10), 'INPUTS', font=fh, fill=(240, 240, 236, 230))
d.text((IX + 160, 16), 'tap an output, then an input  -  tap a lit input to pull it', font=ImageFont.truetype(FONT_R, 12), fill=(200, 200, 196, 170))
d.line([(640, 20), (640, H - 20)], fill=(235, 235, 230, 60), width=2)
for k, lab in enumerate(OUT_LABELS):
    x, y = out_pos(k); tw = d.textlength(lab, font=fl)
    d.text((x + (CW - tw) / 2, y + 76), lab, font=fl, fill=(238, 238, 234, 235))
for k, lab in enumerate(IN_LABELS):
    x, y = in_pos(k); tw = d.textlength(lab, font=fl)
    d.text((x + (CW - tw) / 2, y + 76), lab, font=fl, fill=(238, 238, 234, 235))
pbg.convert('RGB').save(os.path.join(tmp, 'golem_patch_bg.png'), optimize=True)

def entry(name, ctype, data, param, bounds, focus='No'):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": ctype, "data": data},
            "handle remapping": {"version": 1, "map": ([{"key": "Data", "value": f"Parameter {param}"}] if param is not None else [])},
            "bounds": {"version": 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
                       "boundsType": "Absolute", "bounds": bounds}}

pick_out, pick_in = pindex('Patch Out'), pindex('Patch In')
jack_out0, jack_in0 = pindex('Jack Out 1'), pindex('Jack In 1')

tuiPath = os.path.join(tmp, 'TUI.json')
tui = json.load(open(tuiPath))
for tab in tui['pageData']['tabs']:
    bg = tab['componentDefinition']['backgroundData']
    img = 'golem_patch_bg.png' if tab['tabName'] == 'PATCH' else 'golem_bg.png'
    for s in ('focussed', 'unfocussed'): bg[s]['image'] = img
    comps = tab['componentDefinition']['componentsData']
    if tab['tabName'] == 'PATCH':
        for c in comps:     # move the Unplug All switch to the bottom right
            c['bounds']['bounds'] = '976 500 278 107'
        for k in range(NSRC):
            x, y = out_pos(k); b = f'{x + (CW - J) // 2} {y + 6} {J} {J}'
            comps.append(entry(f'jo{k + 1}', 'Image', {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": "jacks.png", "numFrames": 32, "handleName": "Data"}, jack_out0 + k, b))
            comps.append(entry(f'po{k + 1}', 'Button', {"version": 2, "onImage": "jack_pick.png", "offImage": "jack_off.png", "buttonId": k + 1, "numButtonsInGroup": NSRC + 1, "handleName": "Data", "gestureBehaviour": "Instant"}, pick_out, b, 'Yes'))
        for k in range(NDST):
            x, y = in_pos(k); b = f'{x + (CW - J) // 2} {y + 6} {J} {J}'
            comps.append(entry(f'ji{k + 1}', 'Image', {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": "jacks.png", "numFrames": 32, "handleName": "Data"}, jack_in0 + k, b))
            comps.append(entry(f'pi{k + 1}', 'Button', {"version": 2, "onImage": "jack_pick.png", "offImage": "jack_off.png", "buttonId": k + 1, "numButtonsInGroup": NDST + 1, "handleName": "Data", "gestureBehaviour": "Instant"}, pick_in, b, 'Yes'))
    elif tab['tabName'].startswith('AMT') or tab['tabName'].startswith('CBL'):
        extra = []
        for c in comps:
            m = c.get('handle remapping', {}).get('map', [])
            if not m: continue
            pi = int(m[0]['value'].split()[-1]); name = plist[pi]['name']
            if name.endswith(' Amt') or name.endswith(' From'):
                cab = int(name[1:].split()[0])
                x, y, w, h = (int(float(v)) for v in c['bounds']['bounds'].split())
                extra.append(entry(f'chip{cab}', 'Image', {"version": 2, "imageType": "Regular", "colour": "0", "image": f"chip{cab}.png"}, None, f'{x + w - 40} {y + 2} 34 34'))
        comps.extend(extra)
json.dump(tui, open(tuiPath, 'w'), indent=1)
dst = os.path.join(proj, 'skins')
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(tmp, dst)
print('skin ->', dst, len(os.listdir(dst)), 'files,', len(tui['pageData']['tabs']), 'tabs')
