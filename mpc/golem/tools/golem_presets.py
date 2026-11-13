"""golem_presets.py - Golem's built-in programs (with patch cables and sequences), written as C++."""
import json, os
ids = ['volume','voice','glide','bend','retrig','velo','v1_tune','v1_fine','v1_fm','v2_tune','v2_fine','v2_pw','v2_pwm','v2_fm','sync',
       'v3_tune','v3_fine','v3_pw','v3_lf','v3_kbd','v3_fm','m1_lvl','m1_wave','m2_lvl','m2_wave','m3_lvl','m3_wave','noise_lvl','ring_lvl',
       'noise_color','vcf_type','cutoff','reso','vcf_kbd','vcf_env','vcf_fm','drive','adsr_a','adsr_d','adsr_s','adsr_r','ar_a','ar_r',
       'vca_env','vca_init','sh_rate','sh_sync','sh_src','lag','reverb','rev_decay','rev_tone','pan','seq_mode','seq_div','seq_hz',
       'seq_len','seq_dir','seq_quant','seq_range','seq_gate','seq_trans']
enum = ['kVolume','kVoice','kGlide','kBend','kRetrig','kVelo','kV1Tune','kV1Fine','kV1FM','kV2Tune','kV2Fine','kV2PW','kV2PWM','kV2FM','kSync',
        'kV3Tune','kV3Fine','kV3PW','kV3LF','kV3Kbd','kV3FM','kM1Lvl','kM1Wave','kM2Lvl','kM2Wave','kM3Lvl','kM3Wave','kNoiseLvl','kRingLvl',
        'kNoiseColor','kFltType','kCutoff','kReso','kFltKbd','kFltEnv','kFltFM','kDrive','kA','kD','kS','kR','kArA','kArR',
        'kVcaEnv','kVcaInit','kShRate','kShSync','kShSrc','kLag','kReverb','kRevDecay','kRevTone','kPan','kSeqMode','kSeqDiv','kSeqRate',
        'kSeqLen','kSeqDir','kSeqQuant','kSeqRange','kSeqGate','kSeqTrans']
K = dict(zip(ids, enum))
S = {n: i for i, n in enumerate(["None","V1Saw","V1Sq","V2Saw","V2Pulse","V2Tri","V2Sine","V3Saw","V3Pulse","V3Tri","V3Sine","Noise","Ring",
     "SH","Lag","ADSR","AR","VCF","SeqA","SeqB","SeqC","Clock","Kbd","Velo","Wheel","AT"])}
D = {n: i for i, n in enumerate(["None","V1P","V2P","V3P","AllP","V2PW","V3PW","Cut","Reso","VcfIn","VCA","RingIn","SHIn","SHClk","LagIn",
     "ADSRGate","ARGate","SeqClk","SeqRst","Rev","Pan"])}
SAW, PULSE, TRI, SINE = 0, 1, 2, 3
F4012, F4072 = 0, 1
OFF, SYNC, FREE, KEY = 0, 1, 2, 3
D32, D16T, D16, D8T, D8, D4, D2 = range(7)
FWD, BACK, PEND, RAND = range(4)
QOFF, CHROM, MINOR, PHRYG = range(4)
PLAY, REST, SKIP, TIE = range(4)

def preset(name, cables=(), A=None, B=None, C=None, steps=None, rng=2, **kv):
    v = {}
    for k, x in kv.items(): v[K[k]] = x
    v['kSeqRange'] = kv.get('seq_range', rng)
    for n, (s, d, a) in enumerate(cables):
        v[f'kCable + {3 * n}'] = S[s]; v[f'kCable + {3 * n + 1}'] = D[d]; v[f'kCable + {3 * n + 2}'] = a
    if A is not None:   # semitone offsets -> slider %
        for i, st in enumerate(A): v[f'kSeqA + {i}'] = round(min(100.0, max(0.0, st / (12.0 * v['kSeqRange']) * 100.0)), 2)
    for row, vals in (('kSeqB', B), ('kSeqC', C)):
        if vals is not None:
            for i, x in enumerate(vals): v[f'{row} + {i}'] = x
    if steps is not None:
        for i, x in enumerate(steps): v[f'kStep + {i}'] = x
    return (name, v)

P = [
 preset("Init Golem"),
 # ---- leads
 preset("Sync Scream", v2_tune=19, sync=1, m1_lvl=0, m2_lvl=90, m2_wave=SAW, cutoff=5000, reso=25, vcf_env=30,
        cables=[("ADSR", "V2P", 55)], adsr_a=2, adsr_d=900, adsr_s=40, reverb=15),
 preset("Sync Sweep Wheel", v2_tune=7, sync=1, m1_lvl=0, m2_lvl=90, cutoff=6000, reso=20, cables=[("Wheel", "V2P", 80)], reverb=20),
 preset("4072 Screamer", vcf_type=F4072, cutoff=900, reso=78, vcf_env=55, drive=60, m1_lvl=80, m2_lvl=60, m2_wave=SAW, v2_fine=9, adsr_d=600, adsr_s=50),
 preset("Hard Lead", m1_lvl=80, m1_wave=1, m2_lvl=70, m2_wave=SAW, v2_tune=12, v2_fine=4, cutoff=3500, reso=30, drive=50, vcf_env=35, adsr_d=500, adsr_s=70),
 preset("Fifths Lead", m1_lvl=80, m2_lvl=70, m2_wave=SAW, v2_tune=7, cutoff=4000, reso=25, vcf_env=30, adsr_s=70, reverb=25),
 preset("Ring Lead", m1_lvl=20, ring_lvl=90, v2_tune=5, v2_fine=0, cutoff=7000, reso=15, adsr_d=700, adsr_s=50, reverb=25),
 preset("PWM Lead", m1_lvl=0, m2_lvl=90, m2_wave=PULSE, v2_pwm=60, v3_lf=1, v3_kbd=0, v3_tune=0, cutoff=4500, reso=20, vcf_env=30, reverb=20),
 preset("Whistling 4012", m1_lvl=0, cutoff=800, reso=100, vcf_kbd=100, vcf_env=10, adsr_a=40, adsr_s=90, reverb=30),
 preset("Vibrato Lead", m1_lvl=80, m2_lvl=40, m2_wave=SAW, v3_lf=1, v3_kbd=0, v3_tune=12, cutoff=3000, reso=25, cables=[("V3Sine", "AllP", 3)], reverb=25),
 preset("Wheel Wah Lead", m1_lvl=80, m2_lvl=40, m2_wave=PULSE, cutoff=500, reso=60, cables=[("Wheel", "Cut", 60)], reverb=20),
 preset("Duo Fifths", voice=1, m1_lvl=80, m2_lvl=80, m2_wave=SAW, cutoff=3500, reso=25, vcf_env=30, adsr_s=70, reverb=20),
 preset("Duo Ring", voice=1, m1_lvl=30, m2_lvl=40, ring_lvl=80, cutoff=6000, reso=20, adsr_s=70, reverb=25),
 # ---- basses
 preset("ARP Bass", m1_lvl=85, m2_lvl=60, m2_wave=PULSE, v2_tune=-12, cutoff=600, reso=35, vcf_env=60, adsr_a=1, adsr_d=350, adsr_s=20, adsr_r=120, reverb=0, drive=30),
 preset("4072 Acid Bass", vcf_type=F4072, m1_lvl=85, cutoff=350, reso=72, vcf_env=70, drive=50, adsr_a=1, adsr_d=250, adsr_s=10, adsr_r=80, reverb=0),
 preset("Sub Thump", m1_lvl=0, m2_lvl=0, m3_lvl=90, m3_wave=TRI, v3_tune=-12, cutoff=300, reso=10, vcf_env=40, adsr_a=1, adsr_d=400, adsr_s=30, adsr_r=150, reverb=0),
 preset("Grinding Bass", m1_lvl=80, m1_wave=1, m2_lvl=80, m2_wave=SAW, v2_fine=-12, v3_tune=-12, m3_lvl=60, m3_wave=PULSE, cutoff=500, reso=40, vcf_env=50,
        drive=80, adsr_a=1, adsr_d=600, adsr_s=40, adsr_r=150, reverb=0),
 preset("FM Bass Growl", m1_lvl=85, v1_fm=40, v2_tune=-12, cutoff=900, reso=30, vcf_env=45, drive=50, adsr_a=1, adsr_d=400, adsr_s=40, adsr_r=120, reverb=0),
 preset("Sync Bass", v2_tune=12, sync=1, m1_lvl=40, m2_lvl=80, cutoff=1200, reso=30, vcf_env=40, cables=[("ADSR", "V2P", 40)], adsr_a=1, adsr_d=300, adsr_s=30, adsr_r=100, reverb=0),
 preset("EBM Pulse Bass", m1_lvl=0, m2_lvl=90, m2_wave=PULSE, v2_pw=30, cutoff=700, reso=40, vcf_env=55, adsr_a=1, adsr_d=180, adsr_s=0, adsr_r=90, drive=40, reverb=0),
 # ---- S&H and modular classics
 preset("S&H Bleeps", m1_lvl=0, m2_lvl=0, m3_lvl=0, cutoff=500, reso=100, vcf_kbd=80, sh_sync=3, cables=[("SH", "Cut", 60)], vca_init=60, reverb=35),
 preset("Random Computer", m1_lvl=80, m1_wave=1, cutoff=2500, reso=30, sh_sync=3, cables=[("SH", "V1P", 50), ("Clock", "ARGate", 100)], vca_env=0, ar_a=1, ar_r=90, reverb=30,
        seq_mode=SYNC, seq_div=D16, steps=[REST] * 16),
 preset("Sample And Hell", m1_lvl=70, m2_lvl=60, m2_wave=SAW, v2_tune=7, cutoff=1200, reso=60, sh_sync=2, cables=[("SH", "Cut", 50), ("SH", "Pan", 60)], drive=40, reverb=30),
 preset("Lagged Drift", m1_lvl=80, m2_lvl=60, m2_wave=SAW, v2_fine=8, cutoff=1500, reso=40, sh_rate=2, lag=600, cables=[("Lag", "Cut", 40), ("Lag", "V2P", 4)], reverb=40),
 preset("Noise Wind", m1_lvl=0, noise_lvl=90, noise_color=40, cutoff=900, reso=80, vcf_kbd=100, sh_rate=0.5, lag=1500, cables=[("Lag", "Cut", 50)], adsr_a=900, adsr_s=100, adsr_r=2000, reverb=60),
 preset("Low Noise Rumble", m1_lvl=0, noise_lvl=100, noise_color=100, cutoff=400, reso=50, vcf_kbd=0, adsr_a=300, adsr_s=100, adsr_r=1500, reverb=30, drive=60),
 preset("Ring Bell", m1_lvl=0, ring_lvl=100, v2_tune=17, v2_fine=13, cutoff=9000, reso=5, vcf_env=0, vca_env=0, ar_a=1, ar_r=1800, reverb=45),
 preset("Clang Hit", m1_lvl=20, ring_lvl=90, v2_tune=11, v2_fine=21, noise_lvl=20, cutoff=6000, reso=30, vcf_env=-30, vca_env=0, ar_a=1, ar_r=500, drive=60, reverb=40),
 preset("Laser Zap", m1_lvl=80, m1_wave=1, cutoff=8000, reso=40, cables=[("AR", "AllP", 90)], vca_env=0, ar_a=1, ar_r=180, reverb=20),
 preset("Spring Boing", m1_lvl=0, m3_lvl=90, m3_wave=SINE, v3_tune=0, cutoff=5000, vca_env=0, ar_a=1, ar_r=60, reverb=100, rev_decay=85, rev_tone=70, cables=[("AR", "V3P", 60)]),
 preset("Kick Drum 2600", m1_lvl=0, m3_lvl=100, m3_wave=SINE, v3_tune=-24, v3_kbd=0, cutoff=4000, cables=[("AR", "V3P", 70)], vca_env=0, ar_a=1, ar_r=260, reverb=0),
 preset("Snare Noise", m1_lvl=0, noise_lvl=100, m3_lvl=40, m3_wave=TRI, v3_tune=12, v3_kbd=0, cutoff=5000, reso=20, vca_env=0, ar_a=1, ar_r=180, reverb=30),
 preset("Hat Tick", m1_lvl=0, noise_lvl=100, cutoff=9000, reso=50, vcf_kbd=0, vca_env=0, ar_a=1, ar_r=40, reverb=10),
 preset("Wobble VCF", m1_lvl=80, m2_lvl=60, m2_wave=SAW, v2_fine=10, v3_lf=1, v3_kbd=0, v3_tune=0, cutoff=500, reso=55, cables=[("V3Sine", "Cut", 40)], drive=40, reverb=10),
 preset("Audio Rate VCF FM", m1_lvl=85, cutoff=800, reso=60, vcf_fm=50, v2_tune=7, drive=40, reverb=20),
 preset("Cross Mod Chaos", m1_lvl=70, m2_lvl=60, m2_wave=SAW, v1_fm=60, v2_fm=50, v2_tune=-5, cutoff=2000, reso=40, reverb=30),
 preset("Feedback Monster", m1_lvl=70, m1_wave=1, cutoff=600, reso=70, drive=80, cables=[("VCF", "V1P", 15), ("VCF", "Cut", 30)], reverb=20),
 preset("Gated Drone", m1_lvl=70, m2_lvl=60, m2_wave=SAW, v2_fine=7, m3_lvl=50, v3_tune=-12, cutoff=900, reso=40, vca_env=0, ar_a=1, ar_r=60, sh_sync=2,
        cables=[("Clock", "ARGate", 100)], seq_mode=SYNC, seq_div=D16, steps=[PLAY, REST, PLAY, PLAY] * 4, reverb=30),
 # ---- pads and drones
 preset("Analog Strings", m1_lvl=70, m2_lvl=70, m2_wave=SAW, v2_fine=10, m3_lvl=50, v3_tune=0, v3_fine=-9, cutoff=2500, reso=10, vcf_env=10, adsr_a=700, adsr_s=90, adsr_r=1500, reverb=50),
 preset("Dark Drone", m1_lvl=70, m2_lvl=60, m2_wave=SAW, v2_tune=-12, v2_fine=5, m3_lvl=60, v3_tune=7, v3_fine=-6, cutoff=600, reso=35, vcf_env=0,
        adsr_a=2000, adsr_s=100, adsr_r=4000, sh_rate=0.3, lag=1800, cables=[("Lag", "Cut", 35)], reverb=70, rev_decay=80),
 preset("Tritone Dread", m1_lvl=70, m2_lvl=60, m2_wave=SAW, v2_tune=6, m3_lvl=50, v3_tune=-12, cutoff=1000, reso=40, adsr_a=1500, adsr_s=100, adsr_r=3000, reverb=60),
 preset("Breathing Machine", m1_lvl=60, noise_lvl=50, noise_color=50, v3_lf=1, v3_kbd=0, v3_tune=-30, cutoff=700, reso=55, cables=[("V3Tri", "Cut", 45), ("V3Tri", "VCA", 30)],
        adsr_a=500, adsr_s=100, adsr_r=2000, reverb=50),
 preset("Radio Static", m1_lvl=30, noise_lvl=70, ring_lvl=40, cutoff=3000, reso=60, sh_rate=12, cables=[("SH", "Cut", 50), ("SH", "VCA", 40)], vca_init=30, reverb=40),
 # ---- sequencer patterns (Sync = locked to the MPC; hold a key to transpose)
 preset("Seq Phrygian Bass", m1_lvl=85, m2_lvl=60, m2_wave=PULSE, v2_tune=-12, cutoff=500, reso=40, vcf_env=55, adsr_a=1, adsr_d=180, adsr_s=10, adsr_r=80, drive=40, reverb=0,
        seq_mode=SYNC, seq_div=D16, seq_quant=PHRYG, A=[0, 0, 12, 1, 0, 7, 6, 0, 0, 3, 0, 1, 12, 0, 6, 1], steps=[PLAY] * 16),
 preset("Seq EBM Gallop", m1_lvl=0, m2_lvl=90, m2_wave=PULSE, v2_pw=30, cutoff=600, reso=45, vcf_env=60, adsr_a=1, adsr_d=150, adsr_s=0, adsr_r=60, drive=50, reverb=0,
        seq_mode=SYNC, seq_div=D16, seq_quant=MINOR, A=[0, 0, 0, 12, 0, 0, 0, 10, 0, 0, 0, 12, 0, 0, 3, 5], steps=[PLAY, PLAY, REST, PLAY] * 4),
 preset("Seq Tritone Chug", m1_lvl=80, m1_wave=1, m2_lvl=70, m2_wave=SAW, v2_fine=8, cutoff=700, reso=35, vcf_env=50, drive=70, adsr_a=1, adsr_d=200, adsr_s=20, adsr_r=60,
        seq_mode=SYNC, seq_div=D16, A=[0, 0, 0, 0, 6, 6, 0, 0, 0, 0, 1, 1, 0, 0, 6, 0], steps=[PLAY] * 16, reverb=0),
 preset("Seq Acid 4072", vcf_type=F4072, m1_lvl=85, cutoff=400, reso=75, vcf_env=65, drive=55, adsr_a=1, adsr_d=200, adsr_s=10, adsr_r=60, reverb=10,
        seq_mode=SYNC, seq_div=D16, seq_quant=PHRYG, A=[0, 12, 0, 1, 0, 13, 7, 0, 3, 0, 12, 1, 0, 8, 0, 6],
        steps=[PLAY, PLAY, TIE, PLAY, PLAY, REST, PLAY, PLAY, PLAY, TIE, PLAY, PLAY, REST, PLAY, PLAY, PLAY],
        B=[80, 20, 40, 90, 10, 60, 30, 100, 20, 50, 80, 10, 70, 40, 90, 30], cables=[("SeqB", "Cut", 35)]),
 preset("Seq Berlin School", m1_lvl=75, m2_lvl=60, m2_wave=SAW, v2_fine=7, cutoff=900, reso=50, vcf_env=40, adsr_a=1, adsr_d=250, adsr_s=20, adsr_r=200, reverb=50, rev_decay=70,
        seq_mode=SYNC, seq_div=D16, seq_quant=MINOR, A=[0, 7, 12, 15, 12, 7, 3, 7, 0, 7, 12, 17, 15, 12, 7, 3], steps=[PLAY] * 16, seq_dir=PEND,
        C=[i * 6.5 for i in range(16)], cables=[("SeqC", "Cut", 25)]),
 preset("Seq Random Cave", m1_lvl=70, m2_lvl=50, m2_wave=TRI, v2_tune=12, cutoff=1500, reso=45, vcf_env=30, adsr_a=1, adsr_d=300, adsr_s=0, adsr_r=400, reverb=70, rev_decay=80,
        seq_mode=SYNC, seq_div=D8, seq_dir=RAND, seq_quant=PHRYG, A=[0, 1, 3, 5, 7, 8, 10, 12, 13, 15, 0, 7, 12, 1, 6, 10]),
 preset("Seq Skip Machine", m1_lvl=80, m1_wave=1, cutoff=1800, reso=35, vcf_env=40, adsr_a=1, adsr_d=150, adsr_s=0, adsr_r=60, drive=40, reverb=15,
        seq_mode=SYNC, seq_div=D16, seq_quant=MINOR, A=[0, 3, 7, 10, 12, 10, 7, 3, 0, 15, 12, 7, 0, 5, 7, 10],
        steps=[PLAY, SKIP, PLAY, PLAY, SKIP, PLAY, REST, PLAY, PLAY, SKIP, PLAY, SKIP, PLAY, PLAY, REST, PLAY]),
 preset("Seq Ring Arp", m1_lvl=20, ring_lvl=80, v2_tune=7, cutoff=5000, reso=20, adsr_a=1, adsr_d=300, adsr_s=0, adsr_r=300, reverb=45,
        seq_mode=SYNC, seq_div=D16T, seq_quant=PHRYG, A=[0, 7, 13, 0, 8, 15, 0, 7, 12, 1, 8, 13], seq_len=12),
 preset("Seq Industrial Clank", m1_lvl=20, ring_lvl=90, v2_tune=11, v2_fine=17, noise_lvl=20, cutoff=4000, reso=40, vcf_env=-20, vca_env=0, ar_a=1, ar_r=120, drive=60, reverb=30,
        seq_mode=SYNC, seq_div=D16, A=[0, 0, 6, 0, 1, 0, 6, 12, 0, 0, 6, 0, 1, 0, 13, 6], steps=[PLAY, REST, PLAY, REST, PLAY, PLAY, REST, PLAY] * 2,
        B=[100, 0, 60, 0, 80, 40, 0, 100, 100, 0, 60, 0, 80, 40, 0, 100], cables=[("SeqB", "Rev", 50)]),
 preset("Seq Slow Dread", m1_lvl=70, m2_lvl=60, m2_wave=SAW, v2_fine=9, m3_lvl=50, v3_tune=-12, cutoff=800, reso=40, vcf_env=30, adsr_a=200, adsr_d=900, adsr_s=60, adsr_r=1200,
        reverb=60, rev_decay=85, seq_mode=SYNC, seq_div=D4, seq_quant=PHRYG, A=[0, 1, 0, 6, 0, 1, 3, 0], seq_len=8, seq_gate=90),
 preset("Seq Key Arp", m1_lvl=80, m2_lvl=50, m2_wave=PULSE, v2_tune=12, cutoff=2500, reso=30, vcf_env=35, adsr_a=1, adsr_d=200, adsr_s=10, adsr_r=150, reverb=35,
        seq_mode=KEY, seq_div=D16, seq_hz=8, seq_quant=MINOR, A=[0, 3, 7, 12, 15, 12, 7, 3], seq_len=8),
 preset("Seq Free Runner", m1_lvl=75, m2_lvl=50, m2_wave=SAW, v2_tune=7, cutoff=1200, reso=55, vcf_env=40, adsr_a=1, adsr_d=200, adsr_s=0, adsr_r=200, reverb=50,
        seq_mode=FREE, seq_hz=5, seq_quant=PHRYG, A=[0, 12, 1, 13, 0, 7, 6, 3], seq_len=8, seq_dir=PEND),
 preset("Seq Ratchet Ghost", m1_lvl=0, noise_lvl=80, noise_color=30, cutoff=2000, reso=80, vcf_kbd=100, vca_env=0, ar_a=1, ar_r=70, reverb=55,
        seq_mode=SYNC, seq_div=D32, seq_quant=PHRYG, A=[24, 24, 13, 13, 19, 19, 12, 12, 24, 13, 19, 12, 24, 24, 18, 18], steps=[PLAY, PLAY, REST, PLAY] * 4, rng=3),
 preset("Seq Sub Pulse", m1_lvl=0, m3_lvl=90, m3_wave=TRI, v3_tune=-12, cutoff=600, vcf_env=30, adsr_a=1, adsr_d=200, adsr_s=30, adsr_r=80, reverb=0,
        seq_mode=SYNC, seq_div=D8, A=[0, 0, 0, 0, 0, 0, 1, 0], seq_len=8, steps=[PLAY, TIE, PLAY, PLAY, PLAY, TIE, PLAY, REST]),
 preset("Seq CV Morph", m1_lvl=70, m2_lvl=70, m2_wave=PULSE, cutoff=1000, reso=50, vcf_env=30, adsr_a=1, adsr_d=250, adsr_s=20, adsr_r=150, reverb=30,
        seq_mode=SYNC, seq_div=D16, seq_quant=MINOR, A=[0, 0, 3, 0, 7, 0, 10, 0, 12, 0, 10, 0, 7, 0, 3, 0],
        B=[10, 30, 50, 70, 90, 70, 50, 30] * 2, C=[90, 10] * 8, cables=[("SeqB", "V2PW", 80), ("SeqC", "Cut", 30)]),
 preset("Seq Duo Pedal", m1_lvl=70, m2_lvl=70, m2_wave=SAW, v2_tune=-12, cutoff=900, reso=40, vcf_env=45, drive=40, adsr_a=1, adsr_d=200, adsr_s=20, adsr_r=100, reverb=10,
        seq_mode=SYNC, seq_div=D16, A=[0, 0, 12, 0, 0, 13, 0, 0, 0, 0, 12, 0, 0, 18, 0, 6]),
 # ---- weird
 preset("Possessed Radio", m1_lvl=40, m2_lvl=40, m2_wave=SINE, ring_lvl=60, v1_fm=50, v2_fm=40, cutoff=2500, reso=60, sh_rate=7, cables=[("SH", "V2P", 30), ("SH", "Cut", 30), ("Lag", "Pan", 80)],
        lag=200, reverb=50),
 preset("Alien Voice", m1_lvl=70, m2_lvl=50, m2_wave=PULSE, v3_lf=1, v3_kbd=0, v3_tune=20, cutoff=1500, reso=85, vcf_kbd=60, cables=[("V3Tri", "Cut", 20), ("Wheel", "Reso", 30)], reverb=40),
 preset("Machine Heart", m1_lvl=0, m3_lvl=100, m3_wave=SINE, v3_tune=-24, v3_kbd=0, noise_lvl=30, cutoff=900, reso=40, vca_env=0, ar_a=1, ar_r=200, cables=[("AR", "V3P", 50), ("Clock", "ARGate", 100)],
        seq_mode=SYNC, seq_div=D4, steps=[PLAY, REST, PLAY, REST, REST, REST, PLAY, REST], seq_len=8, reverb=30),
 preset("Dying Robot", m1_lvl=80, m1_wave=1, v2_fm=0, cutoff=2000, reso=50, glide=600, cables=[("SH", "V1P", 25)], sh_rate=3, lag=900, drive=50, reverb=40),
 preset("Golem Awakens", m1_lvl=70, m2_lvl=70, m2_wave=SAW, v2_tune=-12, v2_fine=7, m3_lvl=60, v3_tune=-24, noise_lvl=20, noise_color=80, vcf_type=F4072, cutoff=400, reso=60, drive=70,
        vcf_env=50, adsr_a=1500, adsr_d=2000, adsr_s=70, adsr_r=3000, sh_rate=0.4, lag=1500, cables=[("Lag", "Cut", 30), ("Wheel", "AllP", -50)], reverb=60, rev_decay=80),
]
assert len(P) >= 60, len(P)
names = [p[0] for p in P]; assert len(set(names)) == len(names)
levels_path = '/home/claude/golem-tools/golem_levels.json'
levels = json.load(open(levels_path)) if os.path.exists(levels_path) else {}
lines = [f'// Generated by golem_presets.py - {len(P)} programs.']
for name, v in P:
    v = dict(v)
    v['kVolume'] = round(max(-30.0, min(6.0, v.get('kVolume', -6.0) + levels.get(name, 0.0))), 1)
    lines.append(f'program("{name}", {{' + ', '.join(f'{{{k}, {float(x):.4g}f}}'.replace('.0f}', '.0f}') if '.' in f'{float(x):.4g}' or 'e' in f'{float(x):.4g}' else f'{{{k}, {float(x):.4g}.0f}}' for k, x in v.items()) + '});')
open('/home/claude/ref/sdk/plugins/Golem/src/golem_presets.inc', 'w').write('\n'.join(lines) + '\n')
print(len(P), 'presets')
