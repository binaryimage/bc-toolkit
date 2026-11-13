"""behemoth_presets.py - Behemoth's kits, written as C++ for addPrograms()."""
import json, os
DR = ['kick', 'kick2', 'snare', 'clap', 'rim', 'tom_l', 'tom_m', 'tom_h', 'chat', 'ohat', 'cym', 'fm']
OFF = {'tune': 0, 'decay': 1, 'level': 2, 'pan': 3, 'drive': 4, 'a': 5, 'b': 6, 'out': 7}
G = {'volume': 'kVolume', 'routing': 'kRouting', 'grit': 'kGrit', 'glue': 'kGlue', 'accent': 'kAccent', 'swing': 'kSwing',
     'mo_tune': 'kMoTune', 'mo_decay': 'kMoDecay', 'mo_level': 'kMoLevel', 'mo_pan': 'kMoPan', 'mo_drive': 'kMoDrive', 'mo_out': 'kMoOut',
     'v1_wave': 'kV1Wave', 'v2_wave': 'kV2Wave', 'v1_lvl': 'kV1Lvl', 'v2_lvl': 'kV2Lvl', 'noise': 'kNoiseLvl', 'v2_tune': 'kV2Tune',
     'fm': 'kFM', 'sync': 'kSync', 'cutoff': 'kCutoff', 'reso': 'kReso', 'flt_mode': 'kFltMode', 'peg': 'kPitchEgAmt', 'peg_dec': 'kPitchEgDecay',
     'feg': 'kFltEgAmt', 'feg_dec': 'kFltEgDecay', 'noise_vcf': 'kNoiseVcf', 'seq_mode': 'kSeqMode', 'seq_rate': 'kSeqRate', 'seq_len': 'kSeqLen',
     'seq_pdst': 'kSeqPitchDst', 'seq_vel_vcf': 'kSeqVelVcf'}
TRI, SQR = 0, 1
SOFF, STRANS, SPADS = 0, 1, 2
R32, R16, R8, R4 = range(4)

def kit(name, drums=None, pitch=None, vel=None, **g):
    v = {}
    for k, x in g.items(): v[G[k]] = x
    for dn, params in (drums or {}).items():
        d = DR.index(dn)
        for pk, x in params.items(): v[f'kDrums + {8 * d + OFF[pk]}'] = x
    if pitch:
        for i, x in enumerate(pitch): v[f'kSeqPitch + {i}'] = x
    if vel:
        for i, x in enumerate(vel): v[f'kSeqVel + {i}'] = x
    return (name, v)

P = [
 kit("Init Kit"),
 kit("Analog Classic", grit=10, glue=20),
 kit("EBM Machine", grit=25, glue=35, drums={'kick': dict(decay=35, a=60, b=55, drive=30), 'snare': dict(a=70, b=60, decay=30, drive=20),
     'chat': dict(decay=12, a=70), 'ohat': dict(decay=35), 'clap': dict(a=40, decay=35)}),
 kit("Industrial Forge", grit=45, glue=40, drums={'kick': dict(drive=70, a=70, b=70, decay=50), 'snare': dict(drive=60, a=80, tune=-3, decay=45),
     'clap': dict(drive=50, a=80), 'fm': dict(a=90, b=70, decay=55, drive=40), 'cym': dict(b=90, decay=80, tune=-5), 'tom_l': dict(drive=50, b=50),
     'tom_m': dict(drive=50, b=50), 'tom_h': dict(drive=50, b=50)}),
 kit("Factory Floor", grit=55, glue=45, drums={'kick': dict(drive=80, tune=-2, decay=40), 'kick2': dict(b=90, decay=50, drive=50), 'snare': dict(a=90, b=40, drive=70),
     'rim': dict(a=90, b=90, tune=-5), 'chat': dict(b=95, a=40), 'ohat': dict(b=95, a=40), 'fm': dict(a=100, b=95, decay=30)}),
 kit("Rusted Metal", grit=35, glue=30, drums={'fm': dict(a=80, b=85, decay=60, tune=-7), 'cym': dict(b=100, a=30, decay=90, tune=-10), 'chat': dict(b=100), 'ohat': dict(b=100, decay=60),
     'rim': dict(tune=-12, b=100), 'tom_l': dict(b=80, tune=5), 'tom_m': dict(b=80, tune=5), 'tom_h': dict(b=80, tune=5)}),
 kit("Clank Factory", grit=40, glue=30, drums={'fm': dict(a=100, b=60, decay=40), 'rim': dict(a=100, b=100, decay=40), 'clap': dict(a=100, b=80), 'snare': dict(a=40, b=90, tune=7)}),
 kit("Ministry Kit", grit=60, glue=50, drums={'kick': dict(drive=90, a=40, b=80, decay=35), 'snare': dict(drive=90, a=85, b=55, decay=50), 'chat': dict(drive=40),
     'tom_l': dict(drive=70, decay=70), 'tom_m': dict(drive=70, decay=70), 'tom_h': dict(drive=70, decay=70)}),
 kit("Front Line Kit", grit=20, glue=35, drums={'kick': dict(decay=30, a=45, b=30), 'snare': dict(a=60, b=45, decay=25), 'clap': dict(decay=25, a=35),
     'chat': dict(decay=8, a=65), 'ohat': dict(decay=25)}),
 kit("Skinny Bones", grit=30, glue=25, drums={'kick': dict(tune=3, decay=25, b=70), 'snare': dict(tune=5, a=95, b=80, decay=20), 'clap': dict(a=90, b=90, decay=60),
     'rim': dict(a=80), 'fm': dict(a=70, b=40, decay=25, tune=12), 'chat': dict(b=20, a=90)}),
 kit("Boom Bap Dust", grit=15, glue=30, swing=55, drums={'kick': dict(tune=-2, decay=40, a=35, b=20), 'snare': dict(tune=-3, a=60, b=30, decay=45),
     'chat': dict(a=35, decay=15), 'ohat': dict(a=35), 'rim': dict(tune=-2)}),
 kit("Sub Quake", grit=15, glue=25, drums={'kick2': dict(decay=85, a=70, b=40, tune=-3), 'kick': dict(decay=30, b=60), 'snare': dict(a=60)}),
 kit("808-ish Long", grit=10, glue=20, drums={'kick2': dict(decay=95, a=40, b=20, tune=-5), 'clap': dict(a=50, decay=55), 'chat': dict(b=80, a=50, decay=15),
     'ohat': dict(b=80, a=50, decay=45), 'cym': dict(b=80, decay=70), 'rim': dict(tune=0)}),
 kit("Distorted Techno", grit=50, glue=45, drums={'kick': dict(drive=85, decay=55, a=55, tune=-1), 'chat': dict(decay=10, a=80, drive=30), 'ohat': dict(decay=30, a=80),
     'clap': dict(drive=60, a=60), 'rim': dict(drive=50)}),
 kit("Warehouse Rumble", grit=40, glue=40, drums={'kick': dict(drive=70, decay=70, a=40), 'kick2': dict(decay=90, b=80, drive=40, tune=-7), 'ohat': dict(decay=40, a=70)}),
 kit("Dark Ritual", grit=30, glue=30, drums={'kick': dict(tune=-5, decay=60, b=20), 'tom_l': dict(tune=-7, decay=90, a=70), 'tom_m': dict(tune=-7, decay=90, a=70),
     'tom_h': dict(tune=-7, decay=90, a=70), 'cym': dict(tune=-12, decay=100, a=20), 'snare': dict(tune=-7, a=40, b=20, decay=70)}),
 kit("Tribal Drums", grit=20, glue=30, drums={'tom_l': dict(decay=60, a=60, b=40), 'tom_m': dict(decay=55, a=60, b=40), 'tom_h': dict(decay=50, a=60, b=40),
     'rim': dict(decay=40, b=80), 'kick': dict(decay=50)}),
 kit("Lo-Fi Machine", grit=70, glue=50, drums={'kick': dict(drive=60), 'snare': dict(drive=60, b=20), 'chat': dict(a=20, drive=50), 'ohat': dict(a=20, drive=50), 'cym': dict(a=20)}),
 kit("Clean Analog", grit=0, glue=10, drums={'kick': dict(b=20), 'snare': dict(a=45)}),
 kit("Snappy Electro", grit=20, glue=30, drums={'kick': dict(decay=25, a=80, b=80), 'snare': dict(a=90, b=70, decay=20), 'clap': dict(a=30, decay=20), 'fm': dict(a=60, b=20, tune=12, decay=20)}),
 kit("Noise Kit", grit=40, glue=30, drums={'snare': dict(a=100, b=100, decay=60), 'clap': dict(b=100, decay=80), 'chat': dict(b=0, a=100), 'ohat': dict(b=0, a=100, decay=70),
     'cym': dict(b=0, a=100, decay=100)}),
 kit("Cave Toms", grit=25, glue=30, drums={'tom_l': dict(tune=-5, decay=100, a=90, drive=30), 'tom_m': dict(tune=-5, decay=100, a=90, drive=30), 'tom_h': dict(tune=-5, decay=100, a=90, drive=30)}),
 # ---- Mother (DFAM-style) loops: pad 14 starts the loop, 15 stops, 16 resets (Transport kits run with the MPC)
 kit("Mother Pulse", seq_mode=STRANS, cutoff=900, reso=55, peg=50, peg_dec=20, feg=60, feg_dec=25, mo_decay=30, v2_tune=7,
     pitch=[0, 0, 12, 0, 7, 0, 12, 5], vel=[100, 40, 80, 40, 100, 30, 70, 50]),
 kit("Mother Techno Loop", seq_mode=STRANS, grit=30, cutoff=400, reso=70, peg=70, peg_dec=15, feg=70, feg_dec=20, mo_decay=25, mo_drive=60, v1_wave=SQR,
     pitch=[0, 0, 0, 7, 0, 0, 12, 3], vel=[100, 0, 70, 90, 0, 60, 100, 40], drums={'kick': dict(drive=60)}),
 kit("Mother Clank", seq_mode=STRANS, cutoff=2500, reso=40, fm=70, v2_tune=11, sync=1, peg=30, peg_dec=10, feg=40, feg_dec=15, mo_decay=20, noise=30,
     pitch=[0, 6, 0, 13, 0, 6, 1, 0], vel=[100, 60, 0, 90, 100, 0, 70, 50]),
 kit("Mother Bongos", seq_mode=STRANS, cutoff=1800, reso=20, peg=60, peg_dec=8, feg=20, feg_dec=10, mo_decay=18, v1_wave=TRI, v2_lvl=0,
     pitch=[0, 7, 12, 7, 0, 5, 10, 3], vel=[100, 50, 80, 60, 90, 40, 70, 60], swing=50),
 kit("Mother Laser", seq_mode=STRANS, cutoff=3000, reso=60, peg=-90, peg_dec=30, feg=50, feg_dec=30, mo_decay=35, v1_wave=SQR, v2_wave=SQR, v2_tune=12,
     pitch=[12, 0, 7, 0, 12, 3, 10, 0], vel=[100, 70, 90, 60, 100, 50, 80, 40]),
 kit("Mother Noise Snares", seq_mode=STRANS, flt_mode=1, cutoff=1500, reso=40, noise=100, v1_lvl=30, v2_lvl=0, peg=20, feg=40, feg_dec=20, mo_decay=22, noise_vcf=40,
     pitch=[0] * 8, vel=[0, 0, 100, 0, 0, 60, 100, 30]),
 kit("Mother Phrygian Bass", seq_mode=STRANS, cutoff=500, reso=50, peg=15, peg_dec=10, feg=60, feg_dec=30, mo_decay=45, v2_tune=-12, mo_tune=-12, mo_drive=50,
     pitch=[0, 0, 1, 0, 0, 7, 6, 1], vel=[100, 60, 90, 0, 100, 70, 90, 50], seq_rate=R16),
 kit("Mother Drip", seq_mode=STRANS, cutoff=2200, reso=85, peg=80, peg_dec=5, feg=30, feg_dec=8, mo_decay=12, v1_lvl=60, v2_lvl=0,
     pitch=[24, 17, 19, 12, 24, 13, 18, 12], vel=[100, 40, 70, 30, 90, 20, 60, 30], seq_rate=R16),
 kit("Mother Half Time", seq_mode=STRANS, cutoff=700, reso=45, peg=45, peg_dec=25, feg=55, feg_dec=40, mo_decay=55, mo_drive=40, seq_rate=R8,
     pitch=[0, 0, 6, 0, 0, 12, 1, 0], vel=[100, 0, 80, 40, 100, 0, 90, 0], drums={'kick': dict(decay=60), 'snare': dict(decay=60)}),
 kit("Behemoth Rises", seq_mode=STRANS, grit=55, glue=45, cutoff=350, reso=65, peg=60, peg_dec=20, feg=65, feg_dec=30, mo_decay=40, mo_drive=80, v1_wave=SQR, fm=30,
     pitch=[0, 0, 0, 6, 0, 0, 1, 0], vel=[100, 50, 80, 100, 60, 40, 100, 70],
     drums={'kick': dict(drive=80, decay=50, a=65), 'snare': dict(drive=70, a=80), 'cym': dict(b=90, decay=85, tune=-8), 'fm': dict(a=90, b=80, drive=50)}),
]
names = [p[0] for p in P]; assert len(set(names)) == len(names)
lev_path = '/home/claude/golem-tools/behemoth_levels.json'
lev = json.load(open(lev_path)) if os.path.exists(lev_path) else {}
out = [f'// Generated by behemoth_presets.py - {len(P)} kits.']
for name, v in P:
    v = dict(v); v['kVolume'] = round(max(-30.0, min(6.0, v.get('kVolume', -6.0) + lev.get(name, 0.0))), 1)
    def f(x):
        t = f'{float(x):.4g}'; return t + ('f' if ('.' in t or 'e' in t) else '.0f')
    out.append(f'program("{name}", {{' + ', '.join(f'{{{k}, {f(x)}}}' for k, x in v.items()) + '});')
open('/home/claude/ref/sdk/plugins/Behemoth/src/behemoth_presets.inc', 'w').write('\n'.join(out) + '\n')
print(len(P), 'kits')
