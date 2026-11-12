#!/usr/bin/env python3
"""make_bg.py <out.png> - Maggot's page background (1280x628): rotten cells, drips, grain and a
sunk-in wordmark. Original procedural art; dark enough that the controls on top stay readable."""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 1280, 628
rng = np.random.default_rng(13)

def fbm(octaves=5, base=6):
    """Smooth fractal noise in 0..1 from upsampled random grids."""
    acc = np.zeros((H, W), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        gw, gh = base * 2 ** o, max(2, int(base * 2 ** o * H / W))
        g = rng.random((gh, gw)).astype(np.float32)
        img = Image.fromarray((g * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
        acc += amp * np.asarray(img, np.float32) / 255.0
        tot += amp
        amp *= 0.5
    return acc / tot

# 1) base: near-black olive with a sick green / bruise purple drift
n1, n2 = fbm(5, 4), fbm(4, 7)
r = 10 + 14 * n2 + 6 * n1
g = 13 + 22 * n1
b = 9 + 16 * n2 * (1 - n1)
img = np.stack([r, g, b], -1)

# 2) cell membranes: Voronoi edges (distance to nearest minus second nearest seed)
pts = np.stack([rng.random(140) * W, rng.random(140) * H], -1).astype(np.float32)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
d1 = np.full((H, W), 1e9, np.float32); d2 = np.full((H, W), 1e9, np.float32)
for px, py in pts:
    d = np.hypot(xx - px, yy - py)
    closer = d < d1
    d2 = np.where(closer, d1, np.minimum(d2, d))
    d1 = np.where(closer, d, d1)
edge = np.clip(1.0 - (d2 - d1) / 6.0, 0, 1) ** 2          # thin walls
inner = np.clip(d1 / 60.0, 0, 1)                         # cell interiors darken toward centres
wall_glow = edge * (0.35 + 0.65 * fbm(3, 5))
img[..., 0] += 26 * wall_glow
img[..., 1] += 44 * wall_glow                            # bile green walls
img[..., 2] += 6 * wall_glow
img *= (1.0 - 0.25 * inner[..., None])

# 3) a few swollen glowing cells (larvae) scattered low in the frame
for _ in range(9):
    cx, cy = rng.random() * W, H * (0.45 + 0.55 * rng.random())
    rad = 18 + rng.random() * 46
    d = np.hypot((xx - cx) / 1.6, yy - cy)
    blob = np.clip(1 - d / rad, 0, 1) ** 2.2
    img[..., 0] += 30 * blob; img[..., 1] += 52 * blob; img[..., 2] += 10 * blob

# 4) drips from the top edge
drip = np.zeros((H, W), np.float32)
for _ in range(70):
    x = int(rng.random() * W); length = int(H * (0.08 + 0.55 * rng.random() ** 2)); wdt = 1 + int(rng.random() * 3)
    fade = np.linspace(1, 0, length) ** 1.5
    drip[:length, max(0, x - wdt):x + wdt] += fade[:, None] * (0.4 + 0.6 * rng.random())
    if rng.random() < 0.5:                               # a bead at the end
        cy = min(H - 4, length); d = np.hypot(xx - x, yy - cy); drip += np.clip(1 - d / (wdt * 2.5), 0, 1) * 0.6
drip = np.asarray(Image.fromarray(np.clip(drip * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
img[..., 0] += 18 * drip; img[..., 1] += 26 * drip; img[..., 2] += 4 * drip

# 5) scratches
sc = Image.new('L', (W, H), 0); ds = ImageDraw.Draw(sc)
for _ in range(60):
    x0, y0 = rng.random() * W, rng.random() * H
    ang = rng.random() * np.pi; ln = 20 + rng.random() * 160
    ds.line([(x0, y0), (x0 + np.cos(ang) * ln, y0 + np.sin(ang) * ln)], fill=int(40 + rng.random() * 70), width=1)
img += (np.asarray(sc, np.float32) / 255)[..., None] * np.array([14, 18, 12], np.float32)

# 6) the wordmark, huge and sunk into the muck
wm = Image.new('L', (W, H), 0); dw = ImageDraw.Draw(wm)
font = ImageFont.truetype('/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf', 230)
text = 'MAGGOT'
tw = dw.textlength(text, font=font)
dw.text(((W - tw) / 2, H - 280), text, font=font, fill=255)
wm = np.asarray(wm.filter(ImageFilter.GaussianBlur(1.5)), np.float32) / 255
erode = fbm(4, 10) > 0.42                                 # eaten away in patches
wm *= erode
img[..., 0] += 10 * wm; img[..., 1] += 16 * wm; img[..., 2] += 7 * wm

# 7) grain + vignette
img += rng.normal(0, 3.2, (H, W, 1)).astype(np.float32)
vig = 1 - 0.45 * (((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) / 2
img *= np.clip(vig, 0.4, 1)[..., None]

out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB')
out.save(sys.argv[1], optimize=True)
print('mean luminance', float(np.mean(img)))
