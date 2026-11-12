#!/usr/bin/env python3
"""Draw an SDK-generated MPC skin (TUI.json + PNGs) as full 1280x800 MPC screens, one PNG per tab.

    render_skin.py <Plugin Skins dir> <params dump.json> <out dir> [plugin title]

The params dump comes from dump.cpp (real values and display text from the plugin for a preset).
Fonts are stand-ins (the MPC uses Titillium Web); positions, images, film-strip frames, button
states and visibility rules follow the skin exactly.
"""
import json, os, re, sys
from PIL import Image, ImageDraw, ImageFont

FONT_REG = "/usr/share/fonts/opentype/inter/Inter-Regular.otf"
FONT_BOLD = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
for cand in ["/usr/share/fonts/opentype/inter/Inter-Regular.otf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
    if os.path.exists(cand):
        FONT_REG = cand; break
if not os.path.exists(FONT_BOLD):
    FONT_BOLD = FONT_REG

_fonts = {}
def font(size, bold=False):
    key = (int(size), bold)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(FONT_BOLD if bold else FONT_REG, int(size * 0.82))
    return _fonts[key]

def col(argb):
    argb = argb.lower()
    a, r, g, b = (int(argb[i:i + 2], 16) for i in (0, 2, 4, 6))
    return (r, g, b, a)

def rect(s):
    x, y, w, h = (float(v) for v in s.split())
    return int(x), int(y), int(w), int(h)

def main():
    skin, dump_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    title = sys.argv[4] if len(sys.argv) > 4 else "Plugin"
    os.makedirs(out, exist_ok=True)
    pd = json.load(open(os.path.join(skin, "TUI.json")))["pageData"]
    dump = json.load(open(dump_path))
    params = {p["i"]: p for p in dump["params"]}
    defs = {d["key"]: d["value"] for d in pd["componentDefinitions"]["localComponentDefinitions"]}
    choices = {int(r["key"].split()[1]): r["value"] for r in pd.get("replacementStrings", [])}
    images = {}

    def img(name):
        if name not in images:
            images[name] = Image.open(os.path.join(skin, name)).convert("RGBA")
        return images[name]

    def param_of(handle_map, inherited):
        for m in handle_map:
            if m["key"] == "Data" and m["value"].startswith("Parameter "):
                return int(m["value"].split()[1])
        return inherited

    def step_index(p, n):
        return int(round(params[p]["norm"] * (n - 1))) if n > 1 else (1 if params[p]["norm"] >= 0.5 else 0)

    def visible(bounds):
        for h in bounds.get("additionalInvalidatingHandles", []):
            m = re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)", h)
            if m:
                k, n, p = int(m.group(1)), int(m.group(2)), int(m.group(3))
                if step_index(p, n) != k:
                    return False
        return True

    def draw_text(d, box, text, size, colour, just, bold=False):
        x, y, w, h = box
        f = font(size, bold)
        tw = d.textlength(text, font=f)
        while tw > w and len(text) > 1:      # the MPC clips; we shrink to show the full text honestly
            f = font(f.size / 0.82 - 1, bold); tw = d.textlength(text, font=f)
            if f.size < 10: break
        asc, desc = f.getmetrics()
        th = asc + desc
        if "horizontallyCentred" in just or just == "centred":
            tx = x + (w - tw) / 2
        elif "right" in just:
            tx = x + w - tw
        else:
            tx = x
        ty = y + (h - th) / 2 if ("verticallyCentred" in just or True) else y
        d.text((tx, ty), text, font=f, fill=colour)

    def value_text(p):
        q = params[p]
        t = q["display"]
        if q["unit"] and q["unit"] not in ("",):
            t = t + " " + q["unit"]
        return t

    def draw_component(canvas, d, comp, ox, oy, inherited_param, tabstate):
        cd = comp["componentData"]
        b = comp["bounds"]
        if not visible(b):
            return
        x, y, w, h = rect(b["bounds"])
        x += ox; y += oy
        p = param_of(comp.get("handle remapping", {}).get("map", []), inherited_param)
        ty = cd["type"]
        data = cd.get("data", {})
        if ty in defs:      # an instance of a reusable component
            dv = defs[ty]
            for sub in dv["componentsData"]:
                draw_component(canvas, d, sub, x, y, p, tabstate)
            if tabstate.get("focus_first") and p is not None and not tabstate.get("focused"):
                pass
            return
        if ty == "Focus":
            return
        if ty == "Knob" and data.get("knobType") == "FilmStrip":
            strip = img(data["filmStrip"]); n = data["numFrames"]
            vertical = strip.height >= strip.width
            fw, fh = (strip.width, strip.height // n) if vertical else (strip.width // n, strip.height)
            k = int(round(params[p]["norm"] * (n - 1))) if p is not None else 0
            if data.get("invert"): k = n - 1 - k
            frame = strip.crop((0, k * fh, fw, (k + 1) * fh)) if vertical else strip.crop((k * fw, 0, (k + 1) * fw, fh))
            canvas.alpha_composite(frame, (x + (w - fw) // 2, y + (h - fh) // 2))
            return
        if ty == "Button":
            n = data.get("numButtonsInGroup", 1); bid = data.get("buttonId", 0)
            on = (step_index(p, n) == bid) if n > 1 else ((1 if params[p]["norm"] >= 0.5 else 0) == bid)
            im = img(data["onImage"] if on else data["offImage"])
            canvas.alpha_composite(im, (x + (w - im.width) // 2, y + (h - im.height) // 2))
            return
        if ty == "Label":
            st = data["textStyle"]; size = st["font"]["height"]; colour = col(st["colour"])
            just = st.get("justification", "left")
            bold = st["font"].get("style", "") in ("Bold", "SemiBold")
            kind = data.get("type")
            if kind == "Name": text = params[p]["name"]
            elif kind == "Value": text = value_text(p)
            else: text = data.get("text", "")
            draw_text(d, (x, y, w, h), text, size, colour, just, bold)
            return
        if ty == "Slider":
            # The MPC's generic slider: name, value, and a bar.
            d.rounded_rectangle((x, y, x + w, y + h), 8, fill=(36, 38, 42, 255))
            draw_text(d, (x + 14, y + 6, w - 28, 26), params[p]["name"], 22, (150, 150, 155, 255), "left")
            draw_text(d, (x + 14, y + 30, w - 28, 30), value_text(p), 28, (255, 255, 255, 255), "left", True)
            n = len(choices.get(p, [])) or 0
            frac = params[p]["norm"]
            d.rounded_rectangle((x + 14, y + h - 16, x + w - 14, y + h - 10), 3, fill=(60, 62, 66, 255))
            d.rounded_rectangle((x + 14, y + h - 16, x + 14 + int((w - 28) * frac), y + h - 10), 3, fill=accent)
            if comp.get("componentData", {}).get("name", "").endswith("Menu") or True:
                pass
            return
        # Anything else: outline so a missing renderer is obvious.
        d.rectangle((x, y, x + w, y + h), outline=(255, 0, 255, 255), width=2)
        draw_text(d, (x, y, w, 20), ty, 16, (255, 0, 255, 255), "left")

    tabs = pd["tabs"]
    accent = (79, 143, 224, 255)
    m = re.search(r'"outlineColour": "(ff[0-9a-f]{6})"', json.dumps(pd))
    if m: accent = col(m.group(1))
    outs = []
    for ti, tab in enumerate(tabs):
        screen = Image.new("RGBA", (1280, 800), (10, 10, 12, 255))
        d = ImageDraw.Draw(screen)
        # Header (stand-in for the MPC's own track/plugin header).
        d.rectangle((0, 0, 1280, 109), fill=(26, 27, 30, 255))
        d.text((24, 22), "TRACK EDIT  >  PLUGIN", font=font(22), fill=(140, 140, 145, 255))
        d.text((24, 54), "%s   ·   %s" % (title, dump["program"]), font=font(40, True), fill=(235, 235, 240, 255))
        # Content area.
        cd = tab["componentDefinition"]
        bg = cd["backgroundData"]["unfocussed"]["colour"]
        content = Image.new("RGBA", (1280, 628), col(bg))
        cdraw = ImageDraw.Draw(content)
        state = {}
        for comp in cd["componentsData"]:
            draw_component(content, cdraw, comp, 0, 0, None, state)
        screen.alpha_composite(content, (0, 110))
        # Function-key bar: five keys, with a pager when there are more than six tabs.
        d.rectangle((0, 738, 1280, 800), fill=(22, 22, 25, 255))
        many = len(tabs) > 6
        if many:
            page = 0 if ti < 5 else 1
            keys = (["<"] if page else []) + [t["tabName"] for t in (tabs[:5] if page == 0 else tabs[-5:])] + ([">"] if page == 0 else [])
        else:
            keys = [t["tabName"] for t in tabs]
        kw = 1280 / max(6, len(keys))
        for k, name in enumerate(keys):
            x0 = int(k * kw) + 4
            active = name == tab["tabName"]
            d.rounded_rectangle((x0, 744, int(x0 + kw) - 8, 794), 6, fill=accent if active else (44, 45, 50, 255))
            draw_text(d, (x0, 744, int(kw) - 12, 50), name, 24, (255, 255, 255, 255) if active else (190, 190, 195, 255), "horizontallyCentred verticallyCentred", active)
        path = os.path.join(out, "%02d_%s.png" % (ti + 1, re.sub(r"[^A-Za-z0-9]+", "_", tab["tabName"])))
        screen.convert("RGB").save(path)
        outs.append(path)
    print("\n".join(outs))

if __name__ == "__main__":
    main()
