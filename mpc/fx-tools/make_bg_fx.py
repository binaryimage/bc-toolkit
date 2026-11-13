#!/usr/bin/env python3
"""make_bg_fx.py <style> <out.png> - page backgrounds (1280x628) for binaryimage's MPC plugins.
Each style is original procedural art with its own palette and texture, kept dark so the controls
on top stay readable. Styles: grendel ouroboros fenrir moloch mare banshee legion."""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 1280, 628
style, outp = sys.argv[1], sys.argv[2]
rng = np.random.default_rng(sum(ord(c) * 31 ** i for i, c in enumerate(style)) % 2**32)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

def fbm(octaves=5, base=6, aspect=None):
    acc = np.zeros((H, W), np.float32); amp, tot = 1.0, 0.0
    for o in range(octaves):
        gw = base * 2 ** o; gh = max(2, int(gw * (aspect if aspect else H / W)))
        g = rng.random((gh, gw)).astype(np.float32)
        acc += amp * np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32) / 255
        tot += amp; amp *= 0.5
    return acc / tot

def layer(mask, rgb):
    return mask[..., None] * np.array(rgb, np.float32)

def lines(n, fn, width=1, blur=0.0):
    im = Image.new('L', (W, H), 0); d = ImageDraw.Draw(im)
    for _ in range(n): fn(d)
    if blur: im = im.filter(ImageFilter.GaussianBlur(blur))
    return np.asarray(im, np.float32) / 255

def wordmark(text, size, y, font='/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf', erode=0.33, spacing=0):
    wm = Image.new('L', (W, H), 0); dw = ImageDraw.Draw(wm)
    f = ImageFont.truetype(font, size)
    if spacing:
        total = sum(dw.textlength(c, font=f) for c in text) + spacing * (len(text) - 1); x = (W - total) / 2
        for c in text: dw.text((x, y), c, font=f, fill=255); x += dw.textlength(c, font=f) + spacing
    else:
        dw.text(((W - dw.textlength(text, font=f)) / 2, y), text, font=f, fill=255)
    m = np.asarray(wm.filter(ImageFilter.GaussianBlur(1.4)), np.float32) / 255
    return m * (fbm(4, 10) > erode)

n1, n2 = fbm(5, 4), fbm(4, 8)

if style == 'grendel':      # chewed tape: oxide brown, horizontal streaks, crinkles, a spool
    img = np.stack([16 + 18 * n1, 11 + 10 * n1, 8 + 5 * n2], -1)
    streak = fbm(4, 3, aspect=40) ** 2
    img += layer(streak, [40, 22, 8])
    img += layer(lines(220, lambda d: d.line([(rng.random() * W, y := rng.random() * H), (rng.random() * W, y + rng.normal(0, 1.5))], fill=int(30 + rng.random() * 90))), [26, 16, 8])
    for _ in range(3):
        cx, cy, r = rng.random() * W, H * (0.2 + 0.6 * rng.random()), 90 + rng.random() * 120
        d = np.hypot(xx - cx, yy - cy)
        img += layer(np.clip(1 - np.abs(np.sin(d / 5.5)) * 3, 0, 1) * (d < r) * (d > r * 0.25) * 0.5, [34, 20, 8])
    img += layer(wordmark('GRENDEL', 200, H - 255), [30, 15, 6])
elif style == 'ouroboros':  # serpent rings: verdigris scales and a spiral
    img = np.stack([7 + 8 * n2, 14 + 16 * n1, 13 + 12 * n1], -1)
    cx, cy = W * 0.5, H * 0.46
    d = np.hypot(xx - cx, yy - cy); a = np.arctan2(yy - cy, xx - cx)
    spiral = np.clip(1 - np.abs(np.sin(d / 18.0 - a * 2.0)) * 2.4, 0, 1)
    ring = np.exp(-((d - 250) / 50) ** 2)
    scales = np.clip(1 - np.abs(np.sin(xx / 9 + np.sin(yy / 7) * 1.3)) * 2.5, 0, 1) * np.clip(1 - np.abs(np.sin(yy / 9)) * 2.5, 0, 1)
    img += layer(spiral * (0.35 + 0.65 * n1), [6, 28, 22])
    img += layer(ring * (0.4 + 0.6 * scales), [12, 46, 34])
    img += layer(wordmark('OUROBOROS', 150, H - 220), [10, 32, 26])
elif style == 'fenrir':     # claw marks: blood red on black iron
    img = np.stack([12 + 12 * n1, 7 + 5 * n2, 7 + 5 * n2], -1)
    def claw(d):
        x0, y0 = rng.random() * W, rng.random() * H * 0.5; ang = 0.45 + rng.random() * 0.5; ln = 260 + rng.random() * 260
        for k in range(4):
            pts = []
            for t in np.linspace(0, 1, 24):
                a = ang + 0.5 * t * t
                pts.append((x0 + k * 24 + np.cos(a) * ln * t, y0 + k * 6 + np.sin(a) * ln * t))
            d.line(pts, fill=255, width=max(2, 7 - k))
    cl = lines(5, claw, blur=2.0)
    img += layer(cl, [90, 10, 8])
    img += layer(lines(400, lambda d: d.point((rng.random() * W, rng.random() * H), fill=int(rng.random() * 255)), blur=0.6), [40, 6, 4])
    img += layer(wordmark('FENRIR', 220, H - 270, erode=0.36), [44, 6, 5])
elif style == 'moloch':     # furnace: ember glow rising from the bottom, ash
    glow = np.clip(1 - (H - yy) / H, 0, 1) ** 2.2 * (0.5 + 0.5 * fbm(5, 5))
    img = np.stack([9 + 8 * n1, 6 + 4 * n1, 6 + 4 * n2], -1)
    img += layer(glow, [78, 30, 6])
    ash = lines(500, lambda d: d.point((rng.random() * W, rng.random() * H), fill=255), blur=0.8)
    img += layer(ash * fbm(3, 4), [70, 40, 10])
    for k in (-1, 1):       # two horns
        hx = W / 2 + k * 330
        d = np.hypot((xx - hx) / 1.0, (yy - 120) * 1.4); arc = np.exp(-((d - 140) / 10) ** 2) * (yy < 260) * ((xx - hx) * k > -40)
        img += layer(arc, [60, 22, 4])
    img += layer(wordmark('MOLOCH', 210, H - 265), [50, 18, 4])
elif style == 'mare':       # night fog: bruised violet haze, drips
    fog = fbm(5, 3) ** 1.5
    img = np.stack([10 + 14 * fog, 7 + 6 * fog, 14 + 22 * fog], -1)
    def drip(d):
        x = rng.random() * W; ln = H * (0.1 + 0.6 * rng.random() ** 2); d.line([(x, 0), (x, ln)], fill=int(80 + rng.random() * 150), width=1 + int(rng.random() * 2))
    img += layer(lines(60, drip, blur=1.2), [22, 10, 30])
    eye = np.exp(-(((xx - W * 0.5) / 160) ** 2 + ((yy - H * 0.36) / 34) ** 2))
    img += layer(eye, [26, 12, 40])
    img += layer(wordmark('MARE', 240, H - 285, font='/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'), [24, 12, 34])
elif style == 'banshee':    # pale wail: cold blue-white wisps
    wisp = np.clip(fbm(5, 3, aspect=0.25) * 1.6 - 0.5, 0, 1)
    img = np.stack([8 + 10 * n2, 11 + 13 * n2, 16 + 20 * n1], -1)
    img += layer(wisp ** 2, [26, 36, 48])
    for _ in range(5):
        x0 = rng.random() * W
        d = np.abs(xx - x0 - 40 * np.sin(yy / 60 + rng.random() * 6)); img += layer(np.exp(-(d / 3) ** 2) * (0.3 + 0.7 * n1), [14, 22, 30])
    img += layer(wordmark('BANSHEE', 190, H - 250, font='/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'), [22, 30, 40])
elif style == 'legion':     # cathedral: gothic arches, gold leaf on crimson black, many small figures
    img = np.stack([16 + 14 * n1, 6 + 4 * n2, 8 + 5 * n2], -1)
    arch = np.zeros((H, W), np.float32)
    for k in range(7):
        cx = 90 + k * 183; w = 70
        side = np.exp(-((np.abs(xx - cx) - w) / 3.0) ** 2) * (yy > 200)
        top = np.exp(-((np.hypot((xx - cx) / 1.0, (yy - 200) * 0.8) - w) / 3.0) ** 2) * (yy <= 200)
        arch += side + top
    img += layer(np.clip(arch, 0, 1) * (0.5 + 0.5 * n1), [52, 36, 10])
    figs = np.zeros((H, W), np.float32)
    for _ in range(140):
        fx, fy = rng.random() * W, H * (0.55 + 0.4 * rng.random()); s = 4 + rng.random() * 5
        figs += np.exp(-(((xx - fx) / s) ** 2 + ((yy - fy) / (s * 2.6)) ** 2)) * (yy > fy - s * 3)
    img += layer(np.clip(figs, 0, 1) * 0.6, [30, 8, 8])
    img += layer(wordmark('LEGION', 210, H - 265, font='/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf', spacing=30), [48, 32, 8])
else:
    raise SystemExit('unknown style ' + style)

img += rng.normal(0, 3.0, (H, W, 1)).astype(np.float32)
vig = 1 - 0.45 * (((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) / 2
img *= np.clip(vig, 0.4, 1)[..., None]
Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB').save(outp, optimize=True)
print(style, 'mean luminance', round(float(np.mean(img)), 1))
