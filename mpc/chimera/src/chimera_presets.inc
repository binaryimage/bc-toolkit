// chimera_presets.inc - built-in programs, included inside Chimera::addPrograms().
// program(name, overrides). Plain values: % for 0-100 knobs, dB, ms, choice indices.
// Delay types: 0 Digital 1 Tape 2 Analog 3 Lo-Fi 4 Mod 5 Reverse 6 Ping-Pong
// Sync: 0 Free 1 1/32 2 1/16T 3 1/16 4 1/16D 5 1/8T 6 1/8 7 1/8D 8 1/4T 9 1/4 10 1/4D 11 1/2 12 1/2D 13 1 Bar
// Reverb: 0 Room 1 Hall 2 Plate 3 Spring 4 Shimmer 5 Cloud 6 Bloom 7 Swell

program("Init", {});

program("Juno Wash", {
    {kChMode, 0}, {kDlOn, 0}, {kRvOn, 0}, {MX(Cho, mLevel), 0.0f}});

program("Juno Vibrato", {
    {kChMode, 2}, {kChDepth, 140}, {kDlOn, 0}, {kRvOn, 0}, {MX(Dry, mLevel), -60.0f}, {MX(Cho, mLevel), 0.0f}});

program("Tape Echo Haunt", {
    {kChOn, 0}, {A(uType), 1}, {A(uSync), 7}, {A(uFb), 55}, {A(uTone), 35}, {A(uMod), 45},
    {kRvType, 3}, {kRvDecay, 2.0f}, {MX(Dly, mLevel), -3.0f}, {MX(Rev, mLevel), -10.0f}});

program("Triple Ping", {
    {kChOn, 0}, {kDlRouting, 1},
    {A(uOn), 1}, {A(uType), 6}, {A(uSync), 7}, {A(uFb), 45}, {A(uLevel), 80},
    {B(uOn), 1}, {B(uType), 0}, {B(uSync), 9}, {B(uFb), 30}, {B(uLevel), 55},
    {Cc(uOn), 1}, {Cc(uType), 2}, {Cc(uSync), 4}, {Cc(uFb), 25}, {Cc(uLevel), 45},
    {kRvOn, 0}, {MX(Dly, mLevel), -2.0f}});

program("Reverse Ghost", {
    {kChOn, 0}, {A(uType), 5}, {A(uSync), 9}, {A(uFb), 50}, {A(uTone), 45},
    {kRvType, 5}, {kRvDecay, 8.0f}, {kRvSize, 85}, {MX(Dly, mLevel), -2.0f}, {MX(Rev, mLevel), -6.0f}});

program("Shimmer Vault", {
    {kChMode, 1}, {kDlOn, 0}, {kRvType, 4}, {kRvDecay, 9.0f}, {kRvSize, 90}, {kRvSpecial, 60},
    {kRvPre, 60.0f}, {MX(Rev, mLevel), -3.0f}});

program("Frozen Cloud", {
    {kChOn, 0}, {kDlOn, 0}, {kRvType, 5}, {kRvDecay, 20.0f}, {kRvSize, 100}, {kRvMod, 70}, {kRvFreeze, 0},
    {MX(Dry, mLevel), -6.0f}, {MX(Rev, mLevel), 0.0f}});

program("Spring Junk", {
    {kChOn, 0}, {A(uType), 3}, {A(uSync), 6}, {A(uFb), 30}, {A(uTone), 30},
    {kRvType, 3}, {kRvDecay, 2.5f}, {kRvSpecial, 75}, {MX(Rev, mLevel), -4.0f}, {kEqHpf, 90.0f}});

program("Lo-Fi Dub", {
    {kChOn, 1}, {kChHiss, 40}, {A(uType), 3}, {A(uSync), 10}, {A(uFb), 70}, {A(uTone), 25}, {A(uMod), 50},
    {kRvType, 3}, {kRvDecay, 3.0f}, {MX(Dly, mLevel), -2.0f}, {MX(Rev, mLevel), -9.0f},
    {kEqHpf, 120.0f}, {kEqLpf, 6000.0f}});

program("Industrial Slap", {
    {kChOn, 0}, {A(uType), 0}, {A(uSync), 0}, {A(uTime), 85.0f}, {A(uFb), 15}, {A(uTone), 70},
    {kRvType, 0}, {kRvDecay, 0.8f}, {kRvSize, 30}, {MX(Dly, mLevel), -6.0f}, {MX(Rev, mLevel), -8.0f},
    {kEqMidF, 2500.0f}, {kEqMidG, 3.0f}});

program("Swell Bloom", {
    {kChMode, 0}, {A(uType), 4}, {A(uSync), 11}, {A(uFb), 40},
    {kRvType, 6}, {kRvDecay, 10.0f}, {kRvSize, 90}, {kRvSpecial, 70}, {MX(Rev, mLevel), -2.0f}});

program("Rhythm Cascade", {
    {kChOn, 0}, {kDlRouting, 0},
    {A(uOn), 1}, {A(uType), 0}, {A(uSync), 3}, {A(uFb), 25},
    {B(uOn), 1}, {B(uType), 2}, {B(uSync), 7}, {B(uFb), 35},
    {Cc(uOn), 1}, {Cc(uType), 1}, {Cc(uSync), 11}, {Cc(uFb), 40},
    {kRvType, 2}, {kRvDecay, 2.0f}, {MX(Rev, mLevel), -12.0f}});

program("Dark Plate Vox", {
    {kChOn, 0}, {A(uType), 2}, {A(uSync), 6}, {A(uFb), 20}, {A(uLevel), 50},
    {kRvType, 2}, {kRvDecay, 2.8f}, {kRvTone, 30}, {kRvPre, 40.0f}, {MX(Rev, mLevel), -6.0f},
    {kEqLpf, 9000.0f}});

program("Wide Chorus Only", {
    {kChMode, 1}, {kChWidth, 100}, {kChDepth, 130}, {kDlOn, 0}, {kRvOn, 0},
    {MX(Cho, mLevel), -1.0f}});

program("Infinite Pad", {
    {kChMode, 0}, {kDlOn, 0}, {kRvType, 1}, {kRvDecay, 30.0f}, {kRvSize, 100}, {kRvMod, 60},
    {MX(Dry, mLevel), -60.0f}, {MX(Rev, mLevel), 0.0f}});

program("Air Raid Echo", {
    {kChOn, 0}, {A(uType), 4}, {A(uSync), 8}, {A(uFb), 80}, {A(uMod), 80}, {A(uTone), 50},
    {kRvType, 1}, {kRvDecay, 6.0f}, {MX(Dly, mLevel), -3.0f}, {MX(Rev, mLevel), -9.0f}});

program("Bunker Room", {
    {kChOn, 0}, {kDlOn, 0}, {kRvType, 0}, {kRvDecay, 1.2f}, {kRvSize, 40}, {kRvTone, 25},
    {MX(Rev, mLevel), -3.0f}, {kEqLoG, 3.0f}, {kEqHiG, -4.0f}});
