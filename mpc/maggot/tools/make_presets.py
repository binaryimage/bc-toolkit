#!/usr/bin/env python3
"""make_presets.py - writes maggot_presets.inc: Init + 99 Maggot programs.

Values are plain parameter values (%, Hz, ms, semitones, choice indices). Anything a preset doesn't
set keeps the parameter's default. Glide doesn't exist in Maggot, so nothing is rubbery.

Cheat sheet
  shapes (O1/O2 Shape): 0 sine, 33 tri, 66 saw, 100 square (anything between morphs)
  noise type: 0 White 1 Pink 2 Crackle 3 Dust      filter: 0 LP 1 BP 2 HP 3 Notch
  dest: 0 Off 1 Pitch 2 Cutoff 3 O1Shape 4 O1Fold 5 O2Shape 6 ResPitch 7 Noise 8 Amp 9 Pan 10 Smear
  wheel: 0 WRECK 1 Cutoff 2 Fold 3 Smear 4 ResMix 5 Vibrato
  lfo shape: 0 Sine 1 Tri 2 Saw 3 Square 4 S&H 5 Smooth
  arp mode: 0 Up 1 Down 2 UpDown 3 Converge 4 Diverge 5 Random 6 Order 7 Chord
  div: 4 1/16  5 1/8T  6 1/16D  7 1/8  8 1/4T  9 1/8D  10 1/4  12 1/4D  13 1/2  15 1/1  16 2/1
"""
import sys

# Short names -> enum constants in Maggot.cpp
K = dict(vol="kVolume", mode="kVoiceMode", bend="kBend", spread="kSpread",
         s1="kO1Shape", fold="kO1Fold", l1v="kO1Level", s2="kO2Shape", semi="kO2Semi", fine="kO2Fine", stack="kO2Stack",
         l2v="kO2Level", fm="kFM", ring="kRing", nt="kNType", nc="kNColor", nl="kNLevel",
         rmix="kResMix", rdec="kResDecay", rtone="kResTone", rpitch="kResPitch",
         ft="kFType", cut="kCutoff", res="kReso", drv="kFDrive", fenv="kFEnv", key="kFKey", ffm="kFFM", fvel="kFVel",
         aa="kAA", ad="kAD", as_="kAS", ar="kAR", fa="kFA", fd="kFD", fs="kFS", fr="kFR",
         l1s="kL1Shape", l1r="kL1Rate", l1sync="kL1Sync", l1div="kL1Div", l1d="kL1Dest", l1a="kL1Amt",
         l2s="kL2Shape", l2r="kL2Rate", l2d="kL2Dest", l2a="kL2Amt",
         ma="kMA", md="kMD", mdest="kMDest", mamt="kMAmt", wheel="kWheelDest", at="kATCut",
         arp="kArpOn", am="kArpMode", adiv="kArpDiv", aoct="kArpOct", gate="kArpGate", swing="kArpSwing", alen="kArpLen",
         rat="kRatchet", ratp="kRatchetProb", prob="kNoteProb", jump="kOctJump", mut="kMutate", hold="kArpHold", acc="kAccent",
         grind="kGrind", bits="kBits", down="kDown", ringf="kRingFreq", ringm="kRingMix", smear="kSmear", smsize="kSmearSize",
         smpitch="kSmearPitch", wow="kWow", stime="kStopTime", stdiv="kStutterDiv", revdiv="kReverseDiv", glitch="kGlitch",
         wreck="kWreck", ddiv="kDlyDiv", dfb="kDlyFb", dmix="kDlyMix", rsize="kRevSize", rmixs="kRevMix", rdamp="kRevDamp")

P = []
def p(name, **kw): P.append((name, kw))

# Shared building blocks
MONO = dict(mode=1)
SPACE_S = dict(rsize=55, rmixs=15)
SPACE_L = dict(rsize=90, rmixs=40, rdamp=50)
DLY = dict(ddiv=9, dfb=40, dmix=18)

# ===================== BA - bass (14) =====================
p("BA Larva Sub", **MONO, s1=0, l2v=0, nl=0, cut=300, res=10, fenv=20, fd=200, fs=40, aa=1, ad=400, as_=90, ar=80, grind=15, wheel=1)
p("BA Rot Saw", **MONO, s1=66, s2=66, semi=-12, fine=5, l2v=60, cut=420, res=25, drv=45, fenv=55, fd=180, fs=15, aa=1, ar=70, grind=25)
p("BA Gristle Fold", **MONO, s1=33, fold=35, l2v=0, cut=900, res=15, drv=30, fenv=35, fd=220, fs=20, aa=1, ar=60, l1d=4, l1a=20, l1r=0.4)
p("BA Crackle Gut", **MONO, s1=66, nt=2, nl=35, cut=350, res=30, drv=60, fenv=50, fd=150, fs=10, aa=1, ar=50, grind=30)
p("BA Tritone Grind", **MONO, s1=66, s2=66, semi=6, fine=0, l2v=45, cut=380, res=20, drv=55, fenv=45, fd=200, fs=20, ar=60, grind=35)
p("BA Offbeat Maggot", **MONO, s1=66, l2v=0, cut=250, res=15, fenv=75, key=60, fd=70, fs=0, aa=1, ad=120, as_=0, ar=30)
p("BA Pulse Infestation", **MONO, s1=100, s2=100, semi=-12, fine=4, l2v=55, cut=500, res=30, drv=40, fenv=50, fd=160, fs=10, ar=50, l1d=3, l1a=15, l1r=0.7)
p("BA FM Knuckle", **MONO, s1=0, s2=0, semi=12, fm=55, l2v=10, cut=1200, res=10, fenv=30, fd=200, fs=30, ar=60, mdest=4, mamt=40, ma=1, md=120)
p("BA Res Thump", **MONO, s1=33, rmix=45, rdec=60, rtone=30, cut=600, res=10, fenv=40, fd=150, fs=0, aa=1, ad=300, as_=60, ar=60)
p("BA Ring Gravel", **MONO, s1=66, s2=100, fine=3, ring=40, l2v=30, cut=450, res=20, drv=50, fenv=45, fd=200, fs=20, ar=60)
p("BA Dust Pressure", **MONO, s1=0, s2=33, semi=-12, l2v=60, nt=3, nl=40, cut=280, res=5, fenv=25, ar=90, wow=20)
p("BA Acid Larva", **MONO, s1=66, l2v=0, cut=300, res=78, drv=50, fenv=65, key=30, fd=180, fs=0, ar=50, at=40, wheel=1)
p("BA Stacked Rot", **MONO, s1=66, s2=66, semi=-12, stack=60, l2v=55, cut=700, res=15, drv=50, fenv=35, fd=300, fs=35, ar=80, grind=20)
p("BA Wreck Bass", **MONO, s1=66, fold=20, s2=100, semi=-12, l2v=50, cut=550, res=25, drv=60, fenv=45, fd=200, fs=20, ar=60, grind=30, bits=10, wheel=0)

# ===================== RF - riff / heavy (10) =====================
p("RF Bone Wall", s1=66, s2=100, fine=9, l2v=60, cut=900, res=15, drv=75, fenv=35, fvel=70, fd=140, fs=25, aa=1, ad=200, as_=50, ar=60, grind=50, spread=80)
p("RF Fifth Rot", **MONO, s1=66, s2=66, semi=7, fine=3, l2v=70, cut=1000, drv=65, ar=100, grind=45, wheel=1)
p("RF Chainsaw Hive", s1=66, s2=66, stack=80, semi=-12, l2v=55, cut=1600, res=10, drv=70, ar=150, grind=55, wow=15, spread=100)
p("RF Scream Fold", **MONO, s1=33, fold=60, l2v=0, ft=1, cut=1800, res=45, drv=50, ar=150, l1d=4, l1a=25, l1r=5.5, grind=30, **DLY)
p("RF Rust Riff", s1=100, s2=66, fine=12, l2v=50, cut=700, res=20, drv=80, fenv=40, fvel=60, fd=120, fs=15, aa=1, ad=180, as_=40, ar=50, grind=60, bits=12)
p("RF Siren Swarm", **MONO, s1=66, s2=66, semi=6, l2v=50, ft=0, cut=2200, res=35, ar=600, l2s=1, l2r=0.45, l2d=1, l2a=15, grind=25, **SPACE_S)
p("RF Half-Time Slam", s1=66, s2=66, semi=-12, fine=5, l2v=60, nl=10, cut=600, res=15, drv=70, fenv=65, fd=600, fs=20, aa=1, ad=900, as_=40, ar=400, grind=45, rsize=70, rmixs=25)
p("RF Crushed Stab", s1=100, s2=66, semi=12, l2v=40, cut=1400, res=20, drv=60, fenv=40, fd=150, fs=0, aa=1, ad=300, as_=0, ar=150, grind=40, bits=8, down=3)
p("RF Ringworm", **MONO, s1=66, s2=33, semi=1, ring=60, l2v=20, cut=1500, res=30, drv=55, ar=150, ringm=20, ringf=70, grind=35)
p("RF Furnace", s1=66, s2=66, stack=100, l2v=60, cut=1100, res=15, drv=75, ar=200, grind=50, wow=10, spread=100, wheel=0)

# ===================== PD - pads: beautiful under the grime (14) =====================
p("PD Larval Halo", s1=0, s2=33, semi=12, stack=50, l2v=40, nt=3, nl=10, cut=2500, res=10, fenv=0, aa=1500, ad=2000, as_=90, ar=3500, smear=25, smsize=60, **SPACE_L, wow=10)
p("PD Ash Choir", s1=66, s2=66, fine=11, stack=60, l2v=50, nt=1, nl=12, ft=1, cut=900, res=45, aa=1800, as_=95, ar=3000, l2s=5, l2r=0.12, l2d=2, l2a=25, **SPACE_L)
p("PD Rot Garden", s1=33, fold=15, s2=0, semi=7, l2v=40, rmix=30, rdec=85, rtone=60, cut=3000, aa=1200, as_=90, ar=4000, l1d=4, l1a=15, l1r=0.1, smear=20, **SPACE_L)
p("PD Bleak Glass", s1=0, s2=0, semi=19, fm=20, l2v=20, nt=2, nl=8, cut=6000, aa=1000, as_=85, ar=4000, rsize=95, rmixs=50, smear=30, smpitch=40)
p("PD Fallout Bloom", s1=66, s2=100, semi=12, stack=40, l2v=30, cut=600, res=20, fenv=35, fa=3000, fd=4000, fs=60, aa=2500, as_=100, ar=4000, **SPACE_L, wow=15)
p("PD Sick Strings", s1=66, s2=66, fine=14, stack=70, l2v=55, cut=2400, res=10, aa=900, as_=90, ar=2500, wow=35, **SPACE_L)
p("PD Pupa", s1=33, s2=0, semi=-12, l2v=60, nt=1, nl=6, cut=1200, res=25, aa=2000, as_=100, ar=3500, l1s=5, l1r=0.08, l1d=2, l1a=30, **SPACE_L)
p("PD Hymn Of Flies", s1=0, s2=33, semi=24, l2v=15, nt=3, nl=20, nc=80, ft=1, cut=1800, res=55, aa=1500, as_=95, ar=4000, smear=40, smsize=80, smpitch=60, **SPACE_L)
p("PD Phrygian Swarm", s1=66, s2=66, semi=1, fine=0, stack=50, l2v=45, cut=1300, res=15, aa=1800, as_=95, ar=3000, wheel=1, **SPACE_L)
p("PD Decay Shimmer", s1=0, fold=25, s2=0, semi=12, l2v=30, rmix=40, rdec=90, rtone=80, cut=8000, aa=600, as_=80, ar=5000, smear=35, smpitch=100, smsize=70, rsize=95, rmixs=45)
p("PD Wet Cellar", s1=33, s2=66, semi=-12, l2v=40, nt=1, nl=15, nc=20, cut=700, res=30, aa=2500, as_=100, ar=4000, wow=40, rsize=80, rmixs=35, rdamp=80)
p("PD Tape Ghosts", s1=66, s2=33, semi=7, stack=40, l2v=40, cut=1600, aa=1500, as_=90, ar=3000, wow=60, bits=12, smear=20, **SPACE_L)
p("PD Static Lullaby", s1=0, s2=0, semi=12, l2v=40, nt=2, nl=15, nc=70, cut=4000, aa=1200, as_=85, ar=3500, l1s=0, l1r=4.5, l1d=8, l1a=-20, **SPACE_L)
p("PD Black Velvet Rot", s1=66, s2=66, fine=8, stack=100, l2v=50, cut=900, res=35, drv=30, aa=2000, as_=100, ar=4000, l2s=5, l2r=0.07, l2d=2, l2a=35, wheel=4, rmix=0, **SPACE_L)

# ===================== BL - bells / resonator (10) =====================
p("BL Fly Bell", s1=0, s2=0, semi=19, fm=35, l2v=0, rmix=50, rdec=92, rtone=75, cut=8000, aa=1, ad=2500, as_=0, ar=2500, **SPACE_S)
p("BL Rusted Toll", s1=33, ring=30, s2=0, semi=7, fine=30, rmix=60, rdec=95, rtone=50, rpitch=0, cut=6000, aa=1, ad=4000, as_=0, ar=3500, rsize=85, rmixs=30)
p("BL Carcass Chime", s1=0, nt=2, nl=20, rmix=80, rdec=90, rtone=85, rpitch=12, cut=9000, aa=1, ad=1800, as_=0, ar=2000, dmix=20, ddiv=9, dfb=45)
p("BL Glass Larvae", s1=0, s2=0, semi=31, fm=45, rmix=40, rdec=85, rtone=90, cut=10000, aa=1, ad=1200, as_=0, ar=1500, smear=25, smpitch=50, **SPACE_L)
p("BL Dust Mallet", s1=33, nt=3, nl=40, rmix=70, rdec=75, rtone=40, cut=4000, aa=1, ad=600, as_=0, ar=600, **SPACE_S)
p("BL Prison Bell", s1=0, s2=33, semi=6, fm=25, ring=35, rmix=45, rdec=93, rtone=60, cut=7000, aa=1, ad=3000, as_=0, ar=3000, rsize=90, rmixs=35)
p("BL Bone Kalimba", s1=33, rmix=55, rdec=70, rtone=55, rpitch=12, cut=5000, fenv=30, fd=200, fs=0, aa=1, ad=900, as_=0, ar=700, **DLY)
p("BL Infected Harp", s1=66, nt=1, nl=25, rmix=75, rdec=88, rtone=70, cut=6000, fvel=50, aa=1, ad=1500, as_=0, ar=1800, spread=100, **SPACE_S)
p("BL Thorn Gamelan", s1=0, s2=0, semi=14, fm=60, rmix=50, rdec=90, rtone=65, rpitch=7, cut=7000, aa=1, ad=2000, as_=0, ar=2200, dmix=15, ddiv=7, dfb=35)
p("BL Wreck Bell", s1=0, s2=0, semi=19, fm=40, rmix=55, rdec=92, rtone=70, cut=8000, aa=1, ad=2500, as_=0, ar=2500, wheel=0, rsize=85, rmixs=30)

# ===================== AR - weird arps (16) =====================
ARP = dict(arp=1)
p("AR Converge Rot", **ARP, am=3, adiv=4, aoct=2, gate=50, s1=66, cut=900, res=35, fenv=50, fd=120, fs=0, aa=1, ad=200, as_=20, ar=80, grind=20, **DLY)
p("AR Diverge Swarm", **ARP, am=4, adiv=4, aoct=3, gate=40, ratp=30, rat=2, s1=100, s2=66, fine=9, l2v=40, cut=1200, res=30, fenv=45, fd=100, fs=0, ad=180, as_=0, ar=60, **DLY)
p("AR Maggot Ratchet", **ARP, am=0, adiv=7, aoct=2, gate=35, rat=4, ratp=45, s1=66, cut=700, res=40, fenv=60, fd=90, fs=0, ad=150, as_=0, ar=50, grind=25)
p("AR Broken Order", **ARP, am=6, adiv=4, alen=7, prob=80, mut=40, s1=33, rmix=40, rdec=80, rtone=70, cut=4000, aa=1, ad=500, as_=0, ar=400, **SPACE_S)
p("AR Polymeter 5", **ARP, am=2, adiv=4, aoct=2, alen=5, gate=55, s1=66, s2=100, semi=12, l2v=30, cut=1000, res=30, fenv=50, fd=150, fs=10, ad=250, as_=20, ar=80)
p("AR Octave Leech", **ARP, am=0, adiv=4, aoct=1, jump=45, gate=40, s1=100, fold=20, cut=1400, res=25, fenv=40, fd=120, fs=0, ad=200, as_=0, ar=60, **DLY)
p("AR Random Infest", **ARP, am=5, adiv=3, aoct=3, prob=70, ratp=25, rat=3, s1=0, fm=40, s2=0, semi=19, l2v=10, rmix=40, rdec=80, cut=6000, ad=300, as_=0, ar=200, **SPACE_S)
p("AR Chord Stabber", **ARP, am=7, adiv=7, gate=25, swing=30, s1=66, s2=66, fine=10, l2v=50, cut=1100, res=20, drv=50, fenv=40, fd=120, fs=0, ad=200, as_=0, ar=80, grind=35)
p("AR Mutant Berlin", **ARP, am=0, adiv=4, aoct=2, mut=60, gate=60, s1=66, cut=800, res=45, fenv=55, fd=130, fs=10, ad=200, as_=20, ar=80, l1s=1, l1r=0.08, l1d=2, l1a=25, **DLY)
p("AR Swing Gnaw", **ARP, am=2, adiv=4, swing=55, gate=45, s1=100, s2=66, semi=-12, l2v=40, cut=600, res=35, fenv=60, fd=110, fs=0, ad=160, as_=0, ar=50, grind=30)
p("AR Triplet Larvae", **ARP, am=3, adiv=3, aoct=2, gate=40, s1=33, nt=3, nl=15, rmix=50, rdec=85, rtone=60, cut=3000, ad=400, as_=0, ar=300, **SPACE_S)
p("AR Skipping Bug", **ARP, am=0, adiv=2, aoct=1, prob=55, ratp=40, rat=2, gate=30, s1=66, cut=2500, res=20, fenv=30, fd=60, fs=0, ad=90, as_=0, ar=30, bits=10)
p("AR Glitch Feeder", **ARP, am=6, adiv=4, aoct=2, alen=11, mut=30, s1=66, s2=66, fine=7, l2v=40, cut=1000, res=30, fenv=45, fd=120, fs=0, ad=200, as_=0, ar=60, glitch=35)
p("AR Hold The Rot", **ARP, hold=1, am=4, adiv=7, aoct=3, gate=70, s1=0, s2=33, semi=12, l2v=40, cut=2500, aa=20, ad=600, as_=50, ar=600, smear=20, **SPACE_L)
p("AR Ratchet Rain", **ARP, am=5, adiv=7, aoct=2, rat=4, ratp=70, gate=30, s1=0, rmix=60, rdec=80, rtone=80, cut=8000, ad=200, as_=0, ar=150, **DLY)
p("AR Sick Sequence", **ARP, am=1, adiv=4, aoct=2, alen=13, jump=20, mut=20, prob=90, s1=66, fold=15, cut=900, res=40, drv=40, fenv=55, fd=120, fs=5, ad=180, as_=10, ar=60, grind=20, wheel=0)

# ===================== MG - mangle performance (10) =====================
p("MG Tape Death", s1=66, s2=66, fine=8, stack=50, l2v=50, cut=1500, ar=300, stime=1.5, wow=30, **SPACE_S)
p("MG Stutter Bait", s1=100, s2=66, l2v=40, cut=1200, res=20, ar=200, stdiv=4, glitch=20, **DLY)
p("MG Backwards Hymn", s1=0, s2=33, semi=12, l2v=40, cut=3000, aa=600, as_=90, ar=2500, revdiv=10, smear=20, **SPACE_L)
p("MG Freeze Frame", s1=66, nt=1, nl=15, cut=2000, res=20, ar=800, smear=35, smsize=50, **SPACE_L)
p("MG Full Glitch", s1=66, s2=100, semi=-12, l2v=50, cut=900, res=30, drv=40, ar=150, glitch=70, grind=30)
p("MG Crusher Hive", s1=66, s2=66, stack=70, l2v=50, cut=2500, ar=300, bits=6, down=6, grind=30)
p("MG Ring Torture", **MONO, s1=66, cut=2000, res=30, ar=200, ringm=70, ringf=180, grind=40, l1s=4, l1r=6, l1d=2, l1a=30)
p("MG Smear Cloud", s1=0, s2=0, semi=12, l2v=40, cut=4000, aa=200, as_=90, ar=2000, smear=70, smsize=80, smpitch=50, **SPACE_L)
p("MG Wow Sickness", s1=66, s2=66, fine=12, l2v=50, cut=1800, ar=600, wow=90, rsize=70, rmixs=25)
p("MG WRECK Everything", s1=66, s2=100, l2v=50, nt=2, nl=15, cut=1500, res=25, drv=40, ar=300, wreck=60, wheel=0, **SPACE_S)

# ===================== DR - drones / textures (10) =====================
p("DR Hive Mind", **MONO, s1=66, s2=66, semi=-12, stack=100, l2v=60, nt=3, nl=20, cut=500, res=50, drv=40, aa=2000, as_=100, ar=4000, l1s=5, l1r=0.05, l1d=2, l1a=40, **SPACE_L)
p("DR Rotting Organ", **MONO, s1=33, s2=33, semi=7, l2v=50, nt=1, nl=10, rmix=30, rdec=95, cut=1500, aa=1500, as_=100, ar=4000, wow=30, **SPACE_L)
p("DR Crackle Furnace", **MONO, s1=66, nt=2, nl=60, cut=300, res=70, drv=70, aa=1000, as_=100, ar=3000, grind=40, l2s=5, l2r=0.1, l2d=2, l2a=30)
p("DR Infinite Larva", **MONO, s1=0, fold=40, s2=0, semi=-12, l2v=50, cut=1200, res=30, aa=3000, as_=100, ar=5000, l1s=1, l1r=0.04, l1d=4, l1a=40, smear=30, **SPACE_L)
p("DR Dust Storm", **MONO, l1v=0, l2v=0, nt=3, nl=90, nc=70, ft=1, cut=1500, res=60, aa=1500, as_=100, ar=3000, l1s=4, l1r=2, l1d=2, l1a=40, smear=40, **SPACE_L)
p("DR Res Chapel Hum", **MONO, s1=0, nt=1, nl=40, rmix=80, rdec=97, rtone=40, cut=2500, aa=2000, as_=100, ar=5000, l2s=5, l2r=0.06, l2d=6, l2a=10, **SPACE_L)
p("DR Swarm Engine", **MONO, s1=100, s2=100, semi=1, l2v=50, ring=40, cut=800, res=40, drv=50, aa=1500, as_=100, ar=3000, l1s=0, l1r=0.2, l1d=3, l1a=40, grind=25)
p("DR Static Lung", **MONO, s1=66, nt=0, nl=30, nc=30, ft=3, cut=1200, res=40, aa=2500, as_=100, ar=4000, l1s=5, l1r=0.15, l1d=2, l1a=50, wow=40)
p("DR Sub Hive", **MONO, s1=0, s2=33, semi=-12, l2v=70, nt=1, nl=8, cut=250, res=20, aa=3000, as_=100, ar=4000, wow=20, rsize=80, rmixs=20)
p("DR Glitch Choir", **MONO, s1=66, s2=66, fine=12, stack=80, l2v=50, ft=1, cut=900, res=55, aa=2000, as_=100, ar=4000, glitch=25, smear=30, **SPACE_L)

# ===================== PC - percussion (8) =====================
PERC = dict(aa=1, as_=0)
p("PC Maggot Kick", **MONO, **PERC, s1=0, l2v=0, ad=400, ar=100, cut=2000, mdest=1, mamt=40, ma=1, md=40, grind=40)
p("PC Rot Snare", **PERC, s1=33, nt=0, nl=70, nc=60, ft=1, cut=1800, res=20, ad=180, ar=150, mdest=1, mamt=20, ma=1, md=25, grind=30)
p("PC Crackle Hat", **PERC, l1v=0, l2v=0, nt=2, nl=100, nc=90, ft=2, cut=6000, ad=60, ar=40)
p("PC Bone Clank", **PERC, s1=0, s2=100, semi=7, fine=31, fm=60, ring=60, rmix=40, rdec=80, rtone=80, cut=5000, ad=300, ar=250, grind=20)
p("PC Dust Shaker", **PERC, l1v=0, l2v=0, nt=3, nl=100, nc=85, ft=2, cut=3000, ad=120, ar=80, l1s=4, l1r=18, l1d=8, l1a=-60)
p("PC Metal Larva", **PERC, s1=100, s2=100, semi=13, ring=80, l2v=20, ft=1, cut=2500, res=50, ad=200, ar=200, mdest=1, mamt=-15, ma=1, md=30)
p("PC Gut Tom", **PERC, s1=33, l2v=0, cut=1500, ad=350, ar=200, mdest=1, mamt=30, ma=1, md=80, grind=30, rmix=30, rdec=60)
p("PC Glitch Clap", **PERC, l1v=0, l2v=0, nt=0, nl=80, ft=1, cut=1400, res=30, ad=150, ar=120, l1s=3, l1r=40, l1d=8, l1a=-70, bits=10)

# ===================== FX (7) =====================
p("FX Air Raid Rot", **MONO, s1=66, s2=66, semi=7, l2v=50, cut=2500, res=30, aa=300, as_=100, ar=1500, l2s=0, l2r=0.3, l2d=1, l2a=100, **SPACE_L)
p("FX Geiger Swarm", s1=100, l1v=40, nt=2, nl=50, cut=3000, res=40, aa=10, as_=100, ar=500, l1s=4, l1r=14, l1d=8, l1a=-80, glitch=30)
p("FX Collapse", **MONO, s1=66, nt=0, nl=40, ft=2, cut=40, fenv=80, fa=4000, fs=100, fr=3000, aa=2000, as_=100, ar=4000, **SPACE_L)
p("FX Radio From Hell", s1=100, nt=1, nl=30, ft=1, cut=1800, res=70, aa=100, as_=100, ar=1500, l1s=4, l1r=12, l1d=2, l1a=80, bits=6, down=4, ringm=30, ringf=900)
p("FX Dive Bomb", **MONO, s1=66, s2=66, semi=7, l2v=50, cut=3000, as_=100, ar=800, mdest=1, mamt=-100, ma=1, md=2500, grind=40)
p("FX Swarm Rising", **MONO, s1=66, s2=66, stack=100, l2v=60, cut=300, aa=4000, as_=100, ar=3000, mdest=2, mamt=80, ma=6000, md=10000, smear=30, **SPACE_L)
p("FX Tape Implosion", **MONO, s1=66, fold=40, cut=2500, res=30, ar=1000, stime=3.5, wow=50, glitch=15, **SPACE_S)

assert len(P) == 99, len(P)

# Level-matching pass: Volume trims measured by rendering every preset (levels.json).
import json, os
_lv = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'levels.json')
if os.path.exists(_lv):
    trims = json.load(open(_lv))
    P = [(n, dict(kw, vol=trims[n]) if n in trims else kw) for n, kw in P]

def lit(v):
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    if '.' not in s: s += '.0'
    return s + 'f'

out = ['// maggot_presets.inc - generated by make_presets.py: Init + 99 programs. Edit the script, not this file.\n',
       'program("Init", {});\n']
for name, kw in P:
    pairs = ', '.join('{%s, %s}' % (K[k], lit(float(v))) for k, v in kw.items())
    out.append('program("%s", {%s});\n' % (name, pairs))
open(sys.argv[1], 'w').write(''.join(out))
print('wrote', len(P) + 1, 'programs')
