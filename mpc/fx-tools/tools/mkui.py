"""mkui.py - write ui.json from a compact page list. Control spec: 'Param Name' (knob),
'Param Name:fader', ':switch', ':menu', ':bi' (bipolar knob), or None (empty slot)."""
import json
def write(path, accent, bg, text, muted, pages):
    out = {"kit": {"theme": "mono", "accent": accent}, "style": {"background": bg, "accent": accent, "text": text, "muted": muted}, "pages": []}
    for name, ctrls in pages:
        cs = []
        for c in ctrls:
            if c is None: cs.append("empty"); continue
            p, _, w = c.partition(':')
            if w == 'menu': cs.append({"param": p, "widget": "slider", "overlay": "menu"})
            elif w == 'bi': cs.append({"param": p, "widget": "knob", "polarity": "bipolar"})
            else: cs.append({"param": p, "widget": w or "knob"})
        out["pages"].append({"name": name, "controls": cs})
    json.dump(out, open(path, 'w'), indent=1)
