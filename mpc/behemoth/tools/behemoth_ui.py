import sys; sys.path.insert(0, '/home/claude/fx-shared/tools')
import mkui
def drum(n, a, b): return [f"{n} Tune:bi", f"{n} Decay", f"{n} {a}", f"{n} {b}", f"{n} Level", f"{n} Pan:bi", f"{n} Drive", f"{n} Out:menu"]
mkui.write('/home/claude/ref/sdk/plugins/Behemoth/ui.json', 'ffc84b31', 'ff141110', 'fff3ece6', 'ffaa9e94', [
 ("MAIN", ["Volume:fader", "Routing:menu", "Grit", "Glue", "Accent", None, None, None]),
 ("KICKS", drum("Kick", "Punch", "Click") + drum("Kick 2", "Sweep", "Sub")),
 ("SNR/CLAP", drum("Snare", "Snappy", "Tone") + drum("Clap", "Spread", "Tone")),
 ("RIM/FM", drum("Rim", "Click", "Ring") + drum("FM Drum", "FM", "Ratio")),
 ("TOMS", drum("Low Tom", "Bend", "Noise") + drum("Mid Tom", "Bend", "Noise")),
 ("TOM/CYM", drum("High Tom", "Bend", "Noise") + drum("Cymbal", "Tone", "Metal")),
 ("HATS", drum("Closed Hat", "Tone", "Metal") + drum("Open Hat", "Tone", "Metal")),
 ("MOTHER", ["VCO1 Wave:menu", "VCO2 Wave:menu", "VCO1 Level", "VCO2 Level",
             "Mother Tune:bi", "VCO2 Tune:bi", "1>2 FM", "Hard Sync:switch",
             "Noise Level", "Cutoff", "Resonance", "Filter Mode:menu",
             "Pitch EG:bi", "Pitch EG Decay", "Filter EG:bi", "Filter EG Decay"]),
 ("MO AMP", ["Mother Decay", "Mother Level", "Mother Pan:bi", "Mother Drive",
             "Mother Out:menu", "Noise>VCF", "Seq Vel>VCF", "Seq Pitch To:menu",
             "Seq Mode:menu", "Seq Rate:menu", "Seq Length", "Swing"]),
 ("MO SEQ", [f"Pitch {i}:bi" for i in range(1, 9)] + [f"Velocity {i}" for i in range(1, 9)]),
])
